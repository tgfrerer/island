#include "le_jobs.h"
#include "le_core/le_core.h"

#include <atomic>
#include <mutex>
#include <forward_list>
#include <list>
#include <cstdlib> // for malloc
#include <thread>
#include "assert.h"

#include "private/lockfree_ring_buffer.h"

struct le_fiber_o;
struct le_worker_thread_o;

extern "C" void asm_call_fiber_exit( void );
extern "C" int  asm_switch( le_fiber_o *to, le_fiber_o *from, int switch_to_guest );
extern "C" void asm_fetch_default_control_words( uint64_t * );

struct le_jobs_api::counter_t {
	std::atomic<uint32_t> data{0};
};

using counter_t = le_jobs_api::counter_t;
using le_job_o  = le_jobs_api::le_job_o;

/* NOTE - consider appropriate stack size.
 * 
 * Make sure to set the per-fiber stack size to a value large enough, or jobs will write
 * across their stack boundaries, effectively overwriting heap memory which they don't own.
 *
 * This can lead to some really hard to debug errors, which you can only realistically 
 * trace using data-breakpoints. If heap memory is magically overwritten by another thread
 * - without you wanting it - this is a symptom of stack spill.
 * 
 * We keep the stack size at 8 MB, which seems to be standard on linux. Don't worry about the 
 * potentially large size, memory overcommitting makes sure that physical memory only gets 
 * allocated if you really need it.
 *
 */

constexpr static size_t FIBER_POOL_SIZE         = 128;     // Number of available fibers, each with their own stack
constexpr static size_t FIBER_STACK_SIZE        = 1 << 23; // 2^23 == 8 MB
constexpr static size_t MAX_WORKER_THREAD_COUNT = 16;      // Maximum number of possible, but not necessarily requested worker threads.

enum class FIBER_STATUS : uint64_t {
	eIdle       = 0,
	eProcessing = 1,
};

/* A Fiber is an execution context, in which a job can execute.
 * For this it provides the job with a stack.
 * 
 * A fiber can only have one job going at the same time.
 * 
 * Once a fiber yields or returns, control returns to the worker 
 * thread which dispatches the next fiber. 
 * 
 * A fiber is guaranteed by le_jobs to stay on the same worker
 * thread for as long as it takes until a job completes. This means
 * that jobs resume on the same worker thread on which they did 
 * yield.
 * 
 */
struct le_fiber_o {
	void **                   stack                = nullptr;             // pointer to address of current stack
	void *                    job_param            = nullptr;             // parameter pointer for job
	void *                    stack_bottom         = nullptr;             // allocation address so that it may be freed
	counter_t *               fiber_await_counter  = nullptr;             // owned by le_job_manager, must be nullptr, or counter->data must be zero for fiber to start/resume
	counter_t *               job_complete_counter = nullptr;             // owned by le_job_manager
	uint64_t                  job_complete         = 0;                   // flag whether job was completed.
	std::atomic<FIBER_STATUS> fiber_status         = FIBER_STATUS::eIdle; // flag whether fiber is currently active
	le_fiber_o *              list_prev            = nullptr;             // intrusive list
	le_fiber_o *              list_next            = nullptr;             // intrusive list
	constexpr static size_t   NUM_REGISTERS        = 6;                   // must save RBX, RBP, and R12..R15
};

struct le_job_manager_o {
	std::mutex                     counters_mtx;                // mutex protecting counters list
	std::forward_list<counter_t *> counters;                    // storage for counters, list.
	le_fiber_o *                   fibers[ FIBER_POOL_SIZE ]{}; // pool of available fibers
	lockfree_ring_buffer_t *       job_queue;                   // queue onto which to push jobs
	size_t                         worker_thread_count = 0;     // actual number of initialised worker threads
};

struct le_fiber_list_t {
	le_fiber_o *begin = nullptr;
	le_fiber_o *end   = nullptr;
};

/* 
 * A worker thread is the motor providing execution power for fibers.
 * 
 * Worker threads are pinned to CPUs. 
 * 
 * Worker threads pull in fibers so that that they can execute jobs. 
 * If a fiber yields within a worker thread,
 * it is put on the worker thread's wait_list. If a fiber is ready to 
 * resume, it is taken from the wait_list and put on the ready_list. 
 * 
 */
struct le_worker_thread_o {
	le_fiber_o      host_fiber{};          // Host context which does the switching
	le_fiber_o *    guest_fiber = nullptr; // current fiber executing inside this worker thread
	std::thread     thread      = {};      //
	std::thread::id thread_id   = {};      //
	le_fiber_list_t wait_list   = {};      // list of fibers which need checking their condition
	le_fiber_list_t ready_list  = {};      // list of fibers ready to resume after yield
	uint64_t        stop_thread = 0;       // flag, value `1` tells worker to join
};

static le_worker_thread_o *static_worker_threads[ MAX_WORKER_THREAD_COUNT ]{};
static le_job_manager_o *  job_manager = nullptr; ///< job manager singleton, must be initialised via initialise(), and terminated via terminate().

static uint64_t DEFAULT_CONTROL_WORDS = 0; // storage for default control words (must be 8 byte, == 2 words)

// ----------------------------------------------------------------------
void fiber_list_push_back( le_fiber_list_t *list, le_fiber_o *element ) {

	if ( nullptr == list->begin ) {
		list->begin        = element;
		element->list_prev = nullptr;
		element->list_next = nullptr;
		list->end          = element;
	} else {
		element->list_prev   = list->end;
		element->list_next   = nullptr;
		list->end->list_next = element;
		list->end            = element;
	}
}

// ----------------------------------------------------------------------
void fiber_list_remove_element( le_fiber_list_t *list, le_fiber_o *element ) {
	// check if element is either start or end element
	// if it is, we must update in list.

	if ( nullptr == element ) {
		return;
	}

	// --------| invariant: element is not nullptr

	if ( list->begin && element == list->begin ) {
		// element is first list element
		list->begin = list->begin->list_next;
		if ( list->begin ) {
			list->begin->list_prev = nullptr;
		}
	}

	if ( list->end && element == list->end ) {
		// element is last list element
		list->end = list->end->list_prev;
		if ( list->end ) {
			list->end->list_next = nullptr;
		}
	}

	if ( element->list_prev ) {
		// element has a previous list element
		element->list_prev->list_next = element->list_next;
	}

	if ( element->list_next ) {
		// element has a next list element
		element->list_next->list_prev = element->list_prev;
	}

	// Mark element as not being part of the list.
	element->list_next = nullptr;
	element->list_prev = nullptr;
}

// ----------------------------------------------------------------------
// Creates a fiber object, and allocates memory for this fiber
static le_fiber_o *le_fiber_create() {

	le_fiber_o *fiber = new le_fiber_o();

	/* Create a 16-byte aligned stack */
	static_assert( FIBER_STACK_SIZE % 16 == 0, "stack size must be 16 byte-aligned." );

	fiber->stack_bottom = malloc( FIBER_STACK_SIZE );

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

	fiber->stack = reinterpret_cast<void **>( static_cast<char *>( fiber->stack_bottom ) + FIBER_STACK_SIZE );
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
	*( --fiber->stack ) = reinterpret_cast<void *>( DEFAULT_CONTROL_WORDS );

	fiber->job_param            = job->fun_param;
	fiber->job_complete         = 0;
	fiber->job_complete_counter = job->complete_counter;
	fiber->fiber_await_counter  = nullptr;
}

// ----------------------------------------------------------------------

static inline int32_t get_current_worker_thread_id() {
	int32_t result         = -1;
	auto    this_thread_id = std::this_thread::get_id();

	int i = 0;
	for ( le_worker_thread_o **t = static_worker_threads; *t != nullptr; ++t, ++i ) {
		if ( this_thread_id == ( *t )->thread_id ) {
			return i;
		}
	}

	return result;
}

// ----------------------------------------------------------------------
// return pointer to current worker thread providing context,
// or nullptr if no current worker thread could be found.
static le_worker_thread_o *get_current_thread() {
	int32_t worker_thread_id = get_current_worker_thread_id();
	return ( worker_thread_id == -1 ) ? nullptr : static_worker_threads[ worker_thread_id ];
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

/* Called when a fiber exits
 * Note this gets called from asm_call_fiber_exit, not directly.
 */
extern "C" void __attribute__( ( __noreturn__ ) ) fiber_exit( le_fiber_o *host_fiber, le_fiber_o *guest_fiber ) {

	if ( guest_fiber->job_complete_counter ) {
		--guest_fiber->job_complete_counter->data;
	}

	guest_fiber->job_complete = 1;

	// switch back to host thread.
	asm_switch( host_fiber, guest_fiber, 0 );

	/* asm_switch should never return for an exiting fiber. */
	abort();
}

// ----------------------------------------------------------------------

static void le_worker_thread_dispatch( le_worker_thread_o *self ) {

	// -- Check all fibers on the wait list, and add them to the ready list
	// should their condition have become true.
	//
	for ( auto it_f = self->wait_list.begin; it_f != nullptr; ) {

		le_fiber_o *f = it_f;            // We must capture f here,
		it_f          = it_f->list_next; // and increase iterator, since it_f may be invalidated because of remove op

		if ( nullptr == f->fiber_await_counter || 0 == f->fiber_await_counter->data ) {
			fiber_list_remove_element( &self->wait_list, f ); // Must first remove, since list op is intrusive and will update the fiber
			fiber_list_push_back( &self->ready_list, f );     // This will also update the fiber
		}
	}

	// -- If there is any fiber in the ready-list, we must switch to that fiber.
	//
	if ( self->ready_list.begin ) {
		self->guest_fiber = self->ready_list.begin;
		fiber_list_remove_element( &self->ready_list, self->ready_list.begin );
	}

	if ( nullptr == self->guest_fiber ) {

		// find first available idle fiber
		size_t i = 0;
		for ( i = 0; i != FIBER_POOL_SIZE; ++i ) {
			auto fib_idle = FIBER_STATUS::eIdle; // < value to compare against

			if ( job_manager->fibers[ i ]->fiber_status.compare_exchange_weak( fib_idle, FIBER_STATUS::eProcessing ) ) {
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
		assert( false );
		return;
	}

	assert( self->guest_fiber->stack ); // address of stack must not be 0

	// switch to guest fiber
	asm_switch( self->guest_fiber, &self->host_fiber, 1 );

	// If we're back here, this means that the fiber in current_fiber has
	// finished executing for now. This can have two reasons:
	//
	// 1. Fiber did complete
	// 2. Fiber did yield

	if ( 1 == self->guest_fiber->job_complete ) {
		// Fiber was completed: We must return it to the pool
		self->guest_fiber->stack        = nullptr;             // Reset fiber stack
		self->guest_fiber->fiber_status = FIBER_STATUS::eIdle; // return fiber to pool !! do this as the last thing, otherwise other threads will already have taken ownership of it !!
		self->guest_fiber               = nullptr;             // reset current fiber
	} else {
		// Fiber has yielded: We must add it to the wait_list.
		fiber_list_push_back( &self->wait_list, self->guest_fiber );
		self->guest_fiber = nullptr;
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
	assert( num_threads > 0 && "num_threads must be > than 0" );

	assert( nullptr == job_manager );

	asm_fetch_default_control_words( &DEFAULT_CONTROL_WORDS );

	job_manager = new le_job_manager_o();

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

	job_manager->worker_thread_count = num_threads;
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
			// called from the main thread - we must wait until
			// all jobs which affect the counter have completed.
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

LE_MODULE_REGISTER_IMPL( le_jobs, api ) {

	static_cast<le_jobs_api *>( api )->yield                     = le_fiber_yield;
	static_cast<le_jobs_api *>( api )->get_current_worker_id     = get_current_worker_thread_id;
	static_cast<le_jobs_api *>( api )->run_jobs                  = le_job_manager_run_jobs;
	static_cast<le_jobs_api *>( api )->initialize                = le_job_manager_initialize;
	static_cast<le_jobs_api *>( api )->terminate                 = le_job_manager_terminate;
	static_cast<le_jobs_api *>( api )->wait_for_counter_and_free = le_job_manager_wait_for_counter_and_free;

	le_core_load_library_persistently( "libpthread.so" );
}

// ----------------------------------------------------------------------

#ifdef __x86_64

// General assembly reference: https://www.felixcloutier.com/x86/

/* Arguments in rdi, rsi, rdx */
// ;

/* Arguments: asm_switch( next_fiber==rdi, current_fiber==rsi, switch_to_guest==edx )
 * 
 * Save registers on the stack: rbx rbp r12 r13 r14 r15,
 * 
 * Additionally save mxcsr control bits, and x87 status bits on the stack.
 * 
 * Store MXCSR control bits (4byte): `stmxcsr`, load MXCSR control bits: `ldmxcsr`
 * Store x87 status bits (4 byte)  : `fnstcw`, load x87 status bits: `fldcw`
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
 */
asm( R"ASM(

.text
.globl asm_switch
.type asm_switch, @function
.align 16

asm_switch:
     
    mov %edx, %eax          /* Move switch_to_guest into rax */ 

    pushq %rbp
    pushq %rbx

    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12

    leaq  -0x8(%rsp), %rsp  /* Load effective address of rsp (stack pointer) -8 Bytes, into rsp. */
                            /* Meaning: Grow stack by 8 bytes, (stack grows downwards). */
    
                            /* We do this to create a gap so that we can store the
                             * control words for mmx, and x87, which each use 4bytes of space.
                             * We use the "gap" method because this allows us to keep the stack
                             * at the same size, regardless of whether we use that memory or not.
                             */

    stmxcsr  (%rsp)         /* store MMX control- and status-word */
    fnstcw   0x4(%rsp)      /* store x87 control-word */

    movq %rsp, (%rsi)       /* store 'current' stack pointer state into 'current' structure */
    movq (%rdi), %rsp       /* restore 'next' stack pointer state from 'next' structure */

    ldmxcsr  (%rsp)         /* restore MMX control-and status-word */
    fldcw  0x4(%rsp)        /* restore x87 control-word */

    leaq  0x8(%rsp), %rsp   /* jump over 8 bytes used for control-and status words */

    popq %r12               /* restore registers */
    popq %r13
    popq %r14
    popq %r15

    popq %rbx
    popq %rbp

    cmp $0, %rdx            /* if parameter `switch_to_guest` equals 0, don't set function param */
    je 3f

                            /* Load param pointer from "next" fiber and place it in RDI register
                             * (which is register for first argument)
                             *
                             * Data pointer is located at offset +8bytes from address of "next" fiber,
                             * see static assert below.
                             */

    movq 8(%rdi), %rdi      /* set first parameter for function being called through ret */

3:

    ret                     /* return to the "next" fiber and rdi set to next fiber's param pointer. */

    .size asm_switch,.-asm_switch

    // The ret instruction implements a subroutine return mechanism.
    // This instruction first pops a code location off the hardware supported in-memory stack.
    // It then performs an unconditional jump to the retrieved code location.
    // <https://www.cs.virginia.edu/~evans/cs216/guides/x86.html>

)ASM" );

static_assert( offsetof( le_fiber_o, job_param ) == 8, "job_param must be at correct offset for asm_switch to capture it." );

#else
#	error must implement asm_switch for your cpu architecture.
#endif

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
asm( R"ASM(

.globl asm_call_fiber_exit

asm_call_fiber_exit:

    pop %rdi                /* was placed on stack in le_fiber_setup: host_fiber */
    pop %rsi                /* was placed on stack in le_fiber_setup: guest_fiber */

    call fiber_exit

)ASM" );
#else
#	error must implement asm_call_fiber_exit for your cpu architecture.
#endif

#ifdef __x86_64

/* Fetch default control words for mmx and x87 so that we can build
 * a default stack.
 */
asm( R"ASM(

.globl asm_fetch_default_control_words

asm_fetch_default_control_words:

    stmxcsr  (%rdi)         /* store MMX control- and status-word */
    fnstcw   0x4(%rdi)      /* store x87 control-word */

    ret

)ASM" );
#else
#	error must implement asm_fetch_default_control_words for your cpu architecture.
#endif
