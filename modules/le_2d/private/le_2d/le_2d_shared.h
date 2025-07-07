#pragma once

static constexpr uint32_t PATH_REDUCE_WG_SZ = 256;


struct rasterizer_layout_data_t {
	uint32_t n_drawobj;      /// number of draw objects
	uint32_t n_paths;        /// number of paths
	uint32_t n_clips;        /// number of clips
	uint32_t bin_data_start; /// start of binning data
	uint32_t path_tag_base;  /// start of path tag stream
	uint32_t path_data_base; /// start of path data stream
	uint32_t draw_tag_base;  /// start of draw tag stream
	uint32_t draw_data_base; /// start of draw data stream
	uint32_t transform_base; /// start of transform stream
	uint32_t style_base;     /// start of style stream
};
