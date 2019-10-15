#ifndef GUARD_le_jobs_H
#define GUARD_le_jobs_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_job_manager_o;

void register_le_jobs_api( void *api );

// clang-format off
struct le_jobs_api {
	static constexpr auto id      = "le_jobs";
	static constexpr auto pRegFun = register_le_jobs_api;

	struct counter_t;
	typedef void ( *fun_ptr_t )( void * );
	
	/* A Job is a function pointer with a wait_pointer which may be decreased
	 * once the job is complete.
	 */
	struct le_job_o {
		fun_ptr_t  fun_ptr          = nullptr; // function to execute
		void *     fun_param        = nullptr; // user_data for function
		counter_t *complete_counter = nullptr; // owned by le_job_manager, counter to decrement when job completes
	};

	struct le_job_manager_interface_t {

		le_job_manager_o * ( * create  ) ( size_t num_threads);
		void               ( * destroy ) ( le_job_manager_o* self );
		
		
		void (* run_jobs ) (le_job_manager_o* self, le_job_o* jobs, uint32_t num_jobs, counter_t** counter);
		void ( * wait_for_counter_and_free )( le_job_manager_o* self, counter_t* counter, uint32_t target_value );

	};

	void (* yield)(void);

	le_job_manager_interface_t       le_job_manager_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_jobs {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_jobs_api>( true );
#	else
const auto api = Registry::addApiStatic<le_jobs_api>();
#	endif

using counter_t = le_jobs_api::counter_t;
using job_t     = le_jobs_api::le_job_o;

static const auto &manager_i = api -> le_job_manager_i;
static const auto &yield     = api -> yield;

} // namespace le_jobs

#endif // __cplusplus

#endif
