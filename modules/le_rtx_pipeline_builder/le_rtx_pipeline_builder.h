#ifndef GUARD_le_rtx_pipeline_builder_H
#define GUARD_le_rtx_pipeline_builder_H

#include "le_core.h"

struct le_rtx_pipeline_builder_o;
struct le_rtx_pso_handle_t; // opaque handle for rtx pipleine

LE_OPAQUE_HANDLE( le_rtx_pso_handle );

// clang-format off
struct le_rtx_pipeline_builder_api {

	struct le_rtx_pipeline_builder_interface_t {

		le_rtx_pipeline_builder_o *    ( * create  ) ( );
		void                           ( * destroy ) ( le_rtx_pipeline_builder_o* self );
		le_rtx_pso_handle          ( * build   ) ( le_rtx_pipeline_builder_o* self );

	};

	le_rtx_pipeline_builder_interface_t       le_rtx_pipeline_builder_i;
};
// clang-format on

LE_MODULE( le_rtx_pipeline_builder );
LE_MODULE_LOAD_DEFAULT( le_rtx_pipeline_builder );

#ifdef __cplusplus

namespace le_rtx_pipeline_builder {
static const auto& api                       = le_rtx_pipeline_builder_api_i;
static const auto& le_rtx_pipeline_builder_i = api -> le_rtx_pipeline_builder_i;
} // namespace le_rtx_pipeline_builder

class LeRtxPipelineBuilder : NoCopy, NoMove {

	le_rtx_pipeline_builder_o* self;

  public:
	LeRtxPipelineBuilder()
	    : self( le_rtx_pipeline_builder::le_rtx_pipeline_builder_i.create() ) {
	}

	~LeRtxPipelineBuilder() {
		le_rtx_pipeline_builder::le_rtx_pipeline_builder_i.destroy( self );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif
