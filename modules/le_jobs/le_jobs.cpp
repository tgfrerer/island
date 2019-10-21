#include "le_jobs.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include <atomic>
#include <mutex>
#include <forward_list>
#include <cstdlib> // for malloc
#include <thread>

#include "private/lockfree_ring_buffer.h"

struct le_fiber_o;
struct le_worker_thread_o;

extern "C" void asm_call_fiber_exit( void );
extern "C" int  asm_switch( le_fiber_o *to, le_fiber_o *from, int return_value );

struct le_jobs_api::counter_t {
	std::atomic<uint32_t> data{0};
};

using counter_t = le_jobs_api::counter_t;
using le_job_o  = le_jobs_api::le_job_o;

constexpr static size_t FIBER_POOL_SIZE         = 128; // Number of available fibers, each with their own stack
constexpr static size_t MAX_WORKER_THREAD_COUNT = 16;  // Maximum number of possible, but not necessarily requested worker threads.

enum class FIBER_STATUS : uint32_t {
	eIdle    = 0,
	eRunning = 1,
	eWaiting = 2,
};

/* A Fiber is an execution context, in which a job can execute.
 * For this it provides the job with a stack.
 * A fiber can only have one job going at the same time.
 * Once a fiber yields or returns, control returns to the worker 
 * thread which dispatches the next fiber. 
 */
struct le_fiber_o {
	void **                   stack                = nullptr;             // pointer to address of current stack
	void *                    job_param            = nullptr;             // parameter pointer for job
	void *                    stack_bottom         = nullptr;             // allocation address so that it may be freed
	counter_t *               fiber_await_counter  = nullptr;             // owned by le_job_manager, must be nullptr, or counter->data must be zero for fiber to start/resume
	counter_t *               job_complete_counter = nullptr;             // owned by le_job_manager
	uint32_t                  job_complete         = 0;                   // flag whether job was completed.
	std::atomic<FIBER_STATUS> fiber_status         = FIBER_STATUS::eIdle; // flag whether fiber is currently active
	constexpr static size_t   STACK_SIZE           = 1 << 16;             // 2^16
	constexpr static size_t   NUM_REGISTERS        = 6;                   // must save RBX, RBP, and R12..R15
	std::atomic<le_fiber_o *> next_fiber           = nullptr;             // next fiber if in list
};

struct le_job_manager_o {
	std::mutex                     counters_mtx;
	std::forward_list<counter_t *> counters;
	le_fiber_o *                   fibers[ FIBER_POOL_SIZE ]{};
	lockfree_ring_buffer_t *       job_queue;
	std::atomic<le_fiber_o *>      fibers_ready_list = nullptr; // list/stack of ready fibers (may have been initialised with job or not)
	std::atomic<le_fiber_o *>      fibers_wait_list  = nullptr; // list/stack of waiting fibers - filo
};

/* A worker thread is the motor providing execution power for fibers.
 */
struct le_worker_thread_o {
	le_fiber_o      host_fiber{};          // host context which does the switching
	le_fiber_o *    guest_fiber = nullptr; // linked list of fibers. first one is active, rest are waiting.
	std::thread     thread      = {};
	std::thread::id thread_id   = {};
	uint64_t        stop_thread = 0; // flag, value `1` tells worker to join
};

static le_worker_thread_o *static_worker_threads[ MAX_WORKER_THREAD_COUNT ]{};
static le_job_manager_o *  job_manager = nullptr; ///< job manager singleton, must be initialised via initialise(), and terminated via terminate().

// ----------------------------------------------------------------------
// Creates a fiber object, and allocates memory for this fiber
static le_fiber_o *le_fiber_create() {

	le_fiber_o *fiber = new le_fiber_o();

	/* Create a 16-byte aligned stack */
	static_assert( le_fiber_o::STACK_SIZE % 16 == 0, "stack size must be 16 byte-aligned." );

	fiber->stack_bottom = malloc( le_fiber_o::STACK_SIZE );

	if ( fiber->stack_bottom == nullptr )
		return nullptr;

	return fiber;
}

// ----------------------------------------------------------------------

static void le_fiber_destroy( le_fiber_o *fiber ) {
	free( fiber->stack_bottom );
	delete ( fiber );
}

// ----------------------------------------------------------------------
// Associate a fiber with a job
static void le_fiber_load_job( le_fiber_o *fiber, le_fiber_o *host_fiber, le_job_o *job ) {

	fiber->stack = reinterpret_cast<void **>( static_cast<char *>( fiber->stack_bottom ) + le_fiber_o::STACK_SIZE );
	//
	// We push host_fiber and guest_fiber (==fiber) onto the stack so
	// that fiber_exit method can retrieve this information via popping
	// the stack.
	//
	*( --fiber->stack ) = reinterpret_cast<void *>( fiber );
	*( --fiber->stack ) = reinterpret_cast<void *>( host_fiber );

	// 4 bytes below 16-byte alignment: mac os x wants return address here
	// so this points to a call instruction.
	*( --fiber->stack ) = reinterpret_cast<void *>( &asm_call_fiber_exit );

	// 8 bytes below 16-byte alignment: will "return" to start this function
	*( --fiber->stack ) = reinterpret_cast<void *>( job->fun_ptr );

	// push NULL words to initialize the registers loaded by asm_switch
	for ( size_t i = 0; i < le_fiber_o::NUM_REGISTERS; ++i ) {
		*( --fiber->stack ) = nullptr;
	}

	// push 8 bytes (=2x 4 bytes) -> these are to store the fpu control words
	*( --fiber->stack ) = nullptr;

	fiber->job_param            = job->fun_param;
	fiber->job_complete         = 0;
	fiber->job_complete_counter = job->complete_counter;
	fiber->fiber_await_counter  = nullptr;
}

// ----------------------------------------------------------------------
// return pointer to current worker thread providing context,
// or nullptr if no current worker thread could be found.
static le_worker_thread_o *get_current_thread() {

	auto this_thread_id = std::this_thread::get_id();

	for ( le_worker_thread_o **t = static_worker_threads; *t != nullptr; ++t ) {
		if ( this_thread_id == ( *t )->thread_id ) {
			return *t;
		}
	}

	return nullptr;
}

// ----------------------------------------------------------------------
// Fiber yield means that the fiber needs to go to sleep and that control needs to return to
// the worker_thread.
// a yield is always back to the worker_thread.
static void le_fiber_yield() {

	// - We need to find out the thread which did yield.
	//
	// We do this by comparing the yielding thread's id
	// with the stored worker thread ids.
	//
	le_worker_thread_o *yielding_thread = get_current_thread();

	assert( yielding_thread ); // must be one of our worker threads. Can't yield from the main thread.

	if ( yielding_thread ) {
		// Call switch method using the fiber information from the yielding thread.
		asm_switch( &yielding_thread->host_fiber, yielding_thread->guest_fiber, 0 );
	}
}

// ----------------------------------------------------------------------

#ifdef __x86_64

// General assembly reference: https://www.felixcloutier.com/x86/

/* Arguments in rdi, rsi, rdx */
// Arguments: asm_switch( next_fiber==rdi, current_fiber==rsi, ret_val==edx );
//
//
asm( ".globl asm_switch"
     "\n.align 16"
     "\n asm_switch:"
     "\n\t .type asm_switch, @function"

     /* Move ret_val into rax */
     "\n\t movq %rdx, %rax\n"

     /* Save registers on the stack: rbx rbp r12 r13 r14 r15,
      * Additionally save mxcsr control bits, and x87 status bits on the stack.
      * Store value of rsp into current fiber,
      * 
      * These registers are callee-saved registers, which means they 
      * must be restored after function call.
      * 
      * Compare the `System V ABI` calling convention: <https://github.com/hjl-tools/x86-psABI/wiki/x86-64-psABI-1.0.pdf>
      * Specifically page 17, and 18.
      * 
      * Note that this calling convention also requires the callee (i.e. us)
      * to store the control bits of the MXCSR register, and the x87 status 
      * word. 
      * 
      * Store MXCSR control bits (4byte): `stmxcsr`, load MXCSR control bits: `ldmxcsr`
      * Store x87 status bits (4 byte)  : `fnstcw`, load x87 status bits: `fldcw`
      */

     "\n\t pushq %rbp"
     "\n\t pushq %rbx"

     "\n\t pushq %r15"
     "\n\t pushq %r14"
     "\n\t pushq %r13"
     "\n\t pushq %r12"

     /* prepare stack for FPU */
     "\n\t leaq  -0x8(%rsp), %rsp" // Load effective address at 8 bytes before rsp into rsp
                                   // meaning: decrease the stack pointer by 8 bytes.

     // We do this to create a gap so that we can store the
     // control words for mmx, and x87, which each use 4bytes of space.
     // We use the "gap" method because this allows us to keep the stack
     // at the same size, regardless of whether we use that memory or not.

     /* test for flag preserve_fpu */
     "\n\t cmp  $0, %rcx"
     "\n\t je  1f"

     // -- 1:
     "\n 1:"
     /* save MMX control- and status-word */
     "\n\t stmxcsr  (%rsp)"
     /* save x87 control-word */
     "\n\t fnstcw   0x4(%rsp)"

     // --- stack switching
     "\n\t movq %rsp, (%rsi)" // store "current" stack pointer state into "current" structure
     "\n\t movq (%rdi), %rsp" // restore "next" stack pointer state from "next" structure
     // ----

     /* test for flag preserve_fpu */
     "\n\t cmp  $0, %rcx"
     "\n\t je  2f"

     /* restore MMX control- and status-word */
     "\n\t ldmxcsr  (%rsp)"
     /* restore x87 control-word */
     "\n\t fldcw  0x4(%rsp)"

     "\n 2:"
     /* prepare stack for FPU */
     "\n\t leaq  0x8(%rsp), %rsp"

     // ----

     /* stack changed. now restore registers */
     "\n\t popq %r12"
     "\n\t popq %r13"
     "\n\t popq %r14"
     "\n\t popq %r15"

     "\n\t popq %rbx"
     "\n\t popq %rbp"

     // Load param pointer from "next" fiber and place it in RDI register
     // (which is register for first argument)
     // data pointer is located at offset +8bytes from address of "next" fiber, see static assert below
     "\n\t movq 8(%rdi), %rdi"

     // return to the "next" fiber with eax set to return_value,
     // and rdi set to next fiber's param pointer.

     "\n\t ret"
     "\n\t.size asm_switch,.-asm_switch"

     // The ret instruction implements a subroutine return mechanism.
     // This instruction first pops a code location off the hardware supported in-memory stack.
     // It then performs an unconditional jump to the retrieved code location.
     // <https://www.cs.virginia.edu/~evans/cs216/guides/x86.html>

);

static_assert( offsetof( le_fiber_o, job_param ) == 8, "job_param must be at correct offset for asm_switch to capture it." );

#else
#	error must implement asm_switch for your cpu architecture.
#endif

// ----------------------------------------------------------------------

/* Called when a fiber exits
 * Note this gets called from asm_call_fiber_exit, not directly.
 */
extern "C" void __attribute__( ( __noreturn__ ) ) fiber_exit( le_fiber_o *host_fiber, le_fiber_o *guest_fiber ) {

	if ( guest_fiber->job_complete_counter ) {
		--guest_fiber->job_complete_counter->data;
	}

	guest_fiber->job_complete = 1;

	// switch back to worker thread.
	asm_switch( host_fiber, guest_fiber, 0 );

	/* asm_switch should never return for an exiting fiber. */
	abort();
}

// ----------------------------------------------------------------------

#ifdef __x86_64

/* Call fiber_exit with `host_fiber` and `guest_fiber` set correctly.
 * Both these values were stored in `guest_fiber`s stack when this fiber
 * was set up.
 * 
 * Note - stack must always be 16byte aligned:
 * 
 *      The call instruction places a return address on the stack, 
 *      making the stack correctly aligned for the fiber_exit function.
 */
asm( ".globl asm_call_fiber_exit"
     "\n asm_call_fiber_exit:"
     "\n\t pop %rdi" // was placed on stack in le_fiber_setup: host_fiber
     "\n\t pop %rsi" // was placed on stack in le_fiber_setup: guest_fiber
     "\n\t call fiber_exit" );
#else
#	error must implement asm_call_fiber_exit for your cpu architecture.
#endif

// ----------------------------------------------------------------------

static void le_worker_thread_dispatch( le_worker_thread_o *self ) {

	if ( nullptr == self->guest_fiber ) {

		// --------| invariant: this worker thread has no current fiber, active or sleeping.

		// find first available idle fiber
		size_t i = 0;
		for ( ; i != FIBER_POOL_SIZE; ++i ) {
			auto fib_inactive = FIBER_STATUS::eIdle; // < value to compare against

			if ( job_manager->fibers[ i ]->fiber_status.compare_exchange_strong( fib_inactive, FIBER_STATUS::eRunning ) ) {
				// ----------| invariant: `fiber_active` was 0, is now atomically changed to 1
				self->guest_fiber = job_manager->fibers[ i ];
				break;
			}
		}

		if ( i == FIBER_POOL_SIZE ) {
			// we could not find an available fiber, we must return empty-handed.
			return;
		}

		// Pop the job which has been waiting the longest off the job queue

		le_job_o *job = static_cast<le_job_o *>( lockfree_ring_buffer_trypop( job_manager->job_queue ) );

		if ( nullptr == job ) {
			// We couldn't get another job from the queue - this could mean that the queue is empty.
			// anyway, let's wait a little bit before returning...

			self->guest_fiber->fiber_status = FIBER_STATUS::eIdle; // return fiber to pool
			self->guest_fiber               = nullptr;

			std::this_thread::sleep_for( std::chrono::nanoseconds( 100 ) );
			return;
		} else {

			le_fiber_load_job( self->guest_fiber, &self->host_fiber, job );

			// we don't need job anymore after it was passed to fiber_setup
			// and since the ring buffer did own the job, we must delete it
			// here.
			delete ( job );
		}
	}

	// --------| invariant: current_fiber contains a fiber

	// We are only allowed to switch to a fiber if its await counter is zero,
	// or unset. Otherwise this means that child jobs of a fiber are still
	// executing.

	if ( self->guest_fiber->fiber_await_counter && self->guest_fiber->fiber_await_counter->data != 0 ) {
		// This fiber is not ready yet, as its dependent jobs are still executing.
		// we must not process it further, instead place this fiber on the wait list.
		return;
	}

	assert( self->guest_fiber->stack ); // address of stack must not be 0

	// switch to current fiber
	asm_switch( self->guest_fiber, &self->host_fiber, 0 );

	// If we're back here, this means that the fiber in current_fiber has
	// finished executing. This can have two reasons:
	//
	// 1. fiber did complete
	// 2. fiber did yield

	if ( 1 == self->guest_fiber->job_complete ) {
		// fiber was completed.
		// if fiber did complete, we must return it to the pool
		self->guest_fiber->stack        = nullptr;             // Reset fiber stack
		self->guest_fiber->fiber_status = FIBER_STATUS::eIdle; // return fiber to pool !! do this as the last thing, otherwise other threads will already have taken ownership of it !!
		self->guest_fiber               = nullptr;             // reset current fiber

	} else {
		// fiber has yielded.
		// TODO: if fiber did yield, we must add it to the wait_list.
	}
}

// ----------------------------------------------------------------------
// Main loop for each worker thread
//
static void le_worker_thread_loop( le_worker_thread_o *self ) {

	self->thread_id = std::this_thread::get_id();

	while ( 0 == self->stop_thread ) {
		le_worker_thread_dispatch( self );
	}
}

// ----------------------------------------------------------------------

static void le_job_manager_initialize( size_t num_threads ) {

	assert( num_threads <= MAX_WORKER_THREAD_COUNT );
	assert( nullptr == job_manager );

	job_manager = new le_job_manager_o();
	// TODO: we need to create a job queue from which to pick jobs.

	job_manager->job_queue = lockfree_ring_buffer_create( 10 ); // note size is given as a power of 2, so "10" means 1024 elements

	// Allocate a number of fibers to execute jobs in.
	for ( size_t i = 0; i != FIBER_POOL_SIZE; ++i ) {
		job_manager->fibers[ i ] = le_fiber_create();
	}

	// Create a number of worker threads to host fibers in
	for ( size_t i = 0; i != num_threads; ++i ) {

		le_worker_thread_o *w = new le_worker_thread_o();

		w->thread = std::thread( le_worker_thread_loop, w );

		auto      pthread = w->thread.native_handle();
		cpu_set_t mask;
		CPU_ZERO( &mask );
		CPU_SET( i + 1, &mask );
		pthread_setaffinity_np( pthread, sizeof( mask ), &mask );

		// Thread in static ledger of threads so that
		// we may retrieve thread-ids later.
		static_worker_threads[ i ] = w;
	}
}

// ----------------------------------------------------------------------

static void le_job_manager_terminate() {

	assert( job_manager ); // job manager must exist

	// - Send termination signal to all threads.

	for ( le_worker_thread_o **t = &static_worker_threads[ 0 ]; *t != nullptr; ++t ) {
		( *t )->stop_thread = 1;
	}

	// - Join all worker threads

	for ( le_worker_thread_o **t = &static_worker_threads[ 0 ]; *t != nullptr; ++t ) {
		( *t )->thread.join();
		delete ( *t );
		( *t ) = nullptr;
	}

	for ( size_t i = 0; i != FIBER_POOL_SIZE; ++i ) {
		le_fiber_destroy( job_manager->fibers[ i ] );
		job_manager->fibers[ i ] = nullptr;
	}

	// attempt to delete any leftover jobs on the job queue.
	void *ret;
	while ( ( ret = lockfree_ring_buffer_trypop( job_manager->job_queue ) ) ) {
		delete ( static_cast<le_job_o *>( ret ) );
	}

	lockfree_ring_buffer_destroy( job_manager->job_queue );

	{
		std::scoped_lock lock( job_manager->counters_mtx );
		// free all leftover counters.
		for ( auto &c : job_manager->counters ) {
			delete c;
		}

		// clear list of leftover counters.
		job_manager->counters.clear();
	}

	delete job_manager;

	job_manager = nullptr;
}

// ----------------------------------------------------------------------
// polls counter, and will not return until counter == target_value
static void le_job_manager_wait_for_counter_and_free( counter_t *counter, uint32_t target_value ) {

	auto current_worker = get_current_thread();

	if ( nullptr == current_worker ) {
		for ( ; counter->data != target_value; ) {
			// if this was called from a job,
			// we should place this job on the wait queue
			// and update the job's await_counter to be counter.
			std::this_thread::sleep_for( std::chrono::nanoseconds( 100 ) );
		}
	} else {
		// This method has been issued from a job, and not from the main thread.
		// We must issue a yield, but not before we have set the wait_counter for the
		// current worker.
		current_worker->guest_fiber->fiber_await_counter = counter;
		// Switch back to current worker's host fiber
		asm_switch( &current_worker->host_fiber, current_worker->guest_fiber, 0 );
		// If we're back from the switch, this means that the counter has reached
		// zero.
	}

	// --------| invariant: counter must be at zero.
	assert( counter->data == 0 );

	// Remove counter from list of counters owned by job manager
	{
		// we must protect this using a mutex, as other threads might want to add/remove counters
		// and this might lead to a race condition on the forward_list of counters.
		std::scoped_lock lock( job_manager->counters_mtx );
		job_manager->counters.remove( counter );
		// free counter memory
		delete counter;
	}
}

// ----------------------------------------------------------------------
// copies jobs into job queue
static void le_job_manager_run_jobs( le_job_o *jobs, uint32_t num_jobs, counter_t **p_counter ) {

	auto counter  = new counter_t();
	counter->data = num_jobs;

	{
		std::scoped_lock lock( job_manager->counters_mtx );
		job_manager->counters.emplace_front( counter );
	}

	le_job_o *      j        = jobs;
	le_job_o *const jobs_end = jobs + num_jobs;

	for ( ; j != jobs_end; j++ ) {
		// Note that we must store a pointer to counter with each job,
		// which is why we must allocate job objects for each job.
		// Jobs are freed when
		lockfree_ring_buffer_push( job_manager->job_queue, new le_job_o{j->fun_ptr, j->fun_param, counter} );
	}

	// store address back into parameter, so that caller knows about our counter.
	if ( p_counter ) {
		*p_counter = counter;
	}
};

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_jobs_api( void *api ) {

	static_cast<le_jobs_api *>( api )->yield                     = le_fiber_yield;
	static_cast<le_jobs_api *>( api )->run_jobs                  = le_job_manager_run_jobs;
	static_cast<le_jobs_api *>( api )->initialize                = le_job_manager_initialize;
	static_cast<le_jobs_api *>( api )->terminate                 = le_job_manager_terminate;
	static_cast<le_jobs_api *>( api )->wait_for_counter_and_free = le_job_manager_wait_for_counter_and_free;

	Registry::loadLibraryPersistently( "libpthread.so" );
}
