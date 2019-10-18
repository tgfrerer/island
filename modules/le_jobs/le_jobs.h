#ifndef GUARD_le_jobs_H
#define GUARD_le_jobs_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

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

	void ( * initialize                ) ( size_t num_threads     );
	void ( * terminate                 ) ( );
	void ( * run_jobs                  ) ( le_job_o* jobs, uint32_t num_jobs, counter_t** counter );
	void ( * wait_for_counter_and_free ) ( counter_t* counter, uint32_t target_value );

	void (* yield)(void);

};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_jobs {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_jobs_api>( false ); // note false: this module may not be reloaded.
#	else
const auto api = Registry::addApiStatic<le_jobs_api>();
#	endif

using counter_t = le_jobs_api::counter_t;
using job_t     = le_jobs_api::le_job_o;

static const auto &initialize                = api -> initialize;
static const auto &terminate                 = api -> terminate;
static const auto &run_jobs                  = api -> run_jobs;
static const auto &wait_for_counter_and_free = api -> wait_for_counter_and_free;

static const auto &yield = api -> yield;

} // namespace le_jobs

#endif // __cplusplus

#endif
