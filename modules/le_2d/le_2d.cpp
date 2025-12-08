#include "glm/common.hpp"
#include "le_core.h"
#include "private/le_2d/le_2d_shared.h"

#include <atomic>
#include <cassert>
#include <cstring>
#include <stdlib.h>
#include <unordered_map>
#include <array>
#include "le_renderer.hpp"
#include "le_pipeline_builder.h"
#include "private/le_2d/SpookyV2.h"
#include "le_log.h"
#include "le_backend_vk.h"

constexpr uint32_t PATH_BBOX_WG_SZ        = 256;
constexpr uint32_t FLATTEN_WG_SZ          = 256;
constexpr uint32_t CLIP_REDUCE_WG_SZ      = 256;
constexpr uint32_t TILE_UNIT              = 16;              // tiles are 16x16 pixels
constexpr auto     VK_WHOLE_SIZE          = ( ~0ULL );
constexpr size_t   N_GRADIENT_SAMPLES     = 512;

constexpr uint32_t N_BYTES_READBACK_OFFSET = 512;

using ExtendMode              = le_2d_api::ExtendMode;

static constexpr size_t align_up( size_t n, size_t alignment ) {
	n = n + ( -n & ( alignment - 1 ) );
	return n;
}

static_assert( align_up( 3, 4 ) == 4, "must produce the correct alignment" );
static_assert( align_up( 0, 4 ) == 0, "must produce the correct alignment" );
static_assert( align_up( 12, 4 ) == 12, "must produce the correct alignment" );
static_assert( align_up( 13, 4 ) == 16, "must produce the correct alignment" );

// ----------------------------------------------------------------------
// Decompression functions - these are used to retrieve shader code from inl strings
static unsigned int stb_decompress( unsigned char* output, const unsigned char* i, unsigned int /*length*/ );
static unsigned int stb_decompress_length( const unsigned char* input );
// note decode85 is taken from ImGui
static unsigned int decode_85_byte( char c ) {
	return c >= '\\' ? c - 36 : c - 35;
}
static void decode_85( const unsigned char* src, unsigned char* dst ) {
	while ( *src ) {
		unsigned int tmp =
		    decode_85_byte( src[ 0 ] ) +
		    85 * ( decode_85_byte( src[ 1 ] ) +
		           85 * ( decode_85_byte( src[ 2 ] ) +
		                  85 * ( decode_85_byte( src[ 3 ] ) +
		                         85 * decode_85_byte( src[ 4 ] ) ) ) );
		dst[ 0 ] = ( ( tmp >> 0 ) & 0xFF );
		dst[ 1 ] = ( ( tmp >> 8 ) & 0xFF );
		dst[ 2 ] = ( ( tmp >> 16 ) & 0xFF );
		dst[ 3 ] = ( ( tmp >> 24 ) & 0xFF ); // We can't assume little-endianness.
		src += 5;
		dst += 4;
	}
}
// ----------------------------------------------------------------------

constexpr auto LOG_ID = "le_2d";

// shader spv code compressed, and endoded in base 85.

#include "private/le_2d/inl/backdrop_dyn.inl"
#include "private/le_2d/inl/bbox_clear.inl"
#include "private/le_2d/inl/binning.inl"
#include "private/le_2d/inl/clip_leaf.inl"
#include "private/le_2d/inl/clip_reduce.inl"
#include "private/le_2d/inl/coarse.inl"
#include "private/le_2d/inl/draw_leaf.inl"
#include "private/le_2d/inl/draw_reduce.inl"
#include "private/le_2d/inl/fine_area.inl"
#include "private/le_2d/inl/fine_msaa16.inl"
#include "private/le_2d/inl/flatten.inl"
#include "private/le_2d/inl/path_count.inl"
#include "private/le_2d/inl/path_count_setup.inl"
#include "private/le_2d/inl/pathtag_reduce2.inl"
#include "private/le_2d/inl/pathtag_reduce.inl"
#include "private/le_2d/inl/pathtag_scan1.inl"
#include "private/le_2d/inl/pathtag_scan_large.inl"
#include "private/le_2d/inl/pathtag_scan_small.inl"
#include "private/le_2d/inl/path_tiling.inl"
#include "private/le_2d/inl/path_tiling_setup.inl"
#include "private/le_2d/inl/tile_alloc.inl"

//

static le::Log& logger() {
	static le::Log l( LOG_ID );
	return l;
}

struct RasterizerUboData {
	uint32_t width_in_tiles;
	uint32_t height_in_tiles;
	uint32_t target_width;
	uint32_t target_height;
	uint32_t base_color; /// base background colour applied to the target before any blends
	/// Layout follows
	rasterizer_layout_data_t layout;
	/// Layout ends
	uint32_t lines_size;      /// count of LineSoups in line soup buffer allocation
	uint32_t binning_size;    /// count of uint32_t in binning buffer allocation
	uint32_t tiles_size;      /// count of Tiles in tile buffer allocation
	uint32_t seg_counts_size; /// count of SegmentCounts in segment count buffer allocation
	uint32_t segments_size;   /// count of PathSegments in segment buffer allocation
	uint32_t blend_size;      /// count of uint32_t pixels in blend spill buffer allocation
	uint32_t ptcl_size;       /// count of uint32_t in per-tile command list buffer allocation
	uint32_t padding[ 2 ];
};

struct BufferSizes {
	size_t path_reduced;
	size_t path_reduced2;
	size_t path_reduced_scan;
	size_t path_monoids;
	size_t path_bboxes;
	size_t draw_reduced;
	size_t draw_monoids;
	size_t info;
	size_t clip_inps;
	size_t clip_els;
	size_t clip_bics;
	size_t clip_bboxes;
	size_t draw_bboxes;
	size_t bump_alloc;
	size_t indirect_count;
	size_t bin_headers;
	size_t paths;
	// Bump allocated buffers
	size_t lines;
	size_t bin_data;
	size_t tiles;
	size_t seg_counts;
	size_t segments;
	size_t blend_spill;
	size_t ptcl;
};

struct WorkGroupCounts {
	bool     use_large_path_scan;
	uint32_t path_reduce[ 3 ];
	uint32_t path_reduce2[ 3 ];
	uint32_t path_scan1[ 3 ];
	uint32_t path_scan[ 3 ];
	uint32_t bbox_clear[ 3 ];
	uint32_t flatten[ 3 ];
	uint32_t draw_reduce[ 3 ];
	uint32_t draw_leaf[ 3 ];
	uint32_t clip_reduce[ 3 ];
	uint32_t clip_leaf[ 3 ];
	uint32_t binning[ 3 ];
	uint32_t tile_alloc[ 3 ];
	uint32_t path_count_setup[ 3 ];
	// Note: `path_count` must use an indirect dispatch
	uint32_t backdrop[ 3 ];
	uint32_t coarse[ 3 ];
	uint32_t path_tiling_setup[ 3 ];
	// Note: `path_tiling` must use an indirect dispatch
	uint32_t fine[ 3 ];
};

static WorkGroupCounts get_work_group_counts( rasterizer_layout_data_t const& layout, uint32_t width_in_tiles, uint32_t height_in_tiles ) {

	uint32_t n_path_tags         = sizeof( uint32_t ) * ( layout.path_data_base - layout.path_tag_base );
	uint32_t n_paths             = layout.n_paths;
	uint32_t n_draw_objects      = layout.n_drawobj;
	uint32_t n_clips             = layout.n_clips;
	uint32_t path_tag_padded     = align_up( n_path_tags, 4 * PATH_REDUCE_WG_SZ );
	uint32_t path_tag_wgs        = ( path_tag_padded ) / ( 4 * PATH_REDUCE_WG_SZ );
	bool     use_large_path_scan = path_tag_wgs > PATH_REDUCE_WG_SZ;
	uint32_t reduced_size        = use_large_path_scan ? align_up( path_tag_wgs, PATH_REDUCE_WG_SZ ) : path_tag_wgs;
	uint32_t draw_object_wgs     = ( n_draw_objects + PATH_BBOX_WG_SZ - 1 ) / PATH_BBOX_WG_SZ;
	uint32_t draw_monoid_wgs     = std::min( draw_object_wgs, PATH_BBOX_WG_SZ );
	uint32_t flatten_wgs         = ( n_path_tags + FLATTEN_WG_SZ - 1 ) / FLATTEN_WG_SZ;
	uint32_t clip_reduce_wgs     = std::max<int32_t>( 0, n_clips - 1 ) / CLIP_REDUCE_WG_SZ;
	uint32_t clip_wgs            = ( n_clips + CLIP_REDUCE_WG_SZ - 1 ) / CLIP_REDUCE_WG_SZ;
	uint32_t path_wgs            = ( n_paths + PATH_BBOX_WG_SZ - 1 ) / PATH_BBOX_WG_SZ;
	uint32_t width_in_bins       = ( width_in_tiles + 16 - 1 ) / 16;
	uint32_t height_in_bins      = ( height_in_tiles + 16 - 1 ) / 16;

	WorkGroupCounts wc{
	    .use_large_path_scan = use_large_path_scan,
	    .path_reduce         = { path_tag_wgs, 1, 1 },
	    .path_reduce2        = { PATH_REDUCE_WG_SZ, 1, 1 },
	    .path_scan1          = { reduced_size / PATH_REDUCE_WG_SZ, 1, 1 },
	    .path_scan           = { path_tag_wgs, 1, 1 },
	    .bbox_clear          = { draw_object_wgs, 1, 1 },
	    .flatten             = { flatten_wgs, 1, 1 },
	    .draw_reduce         = { draw_monoid_wgs, 1, 1 },
	    .draw_leaf           = { draw_monoid_wgs, 1, 1 },
	    .clip_reduce         = { clip_reduce_wgs, 1, 1 },
	    .clip_leaf           = { clip_wgs, 1, 1 },
	    .binning             = { draw_object_wgs, 1, 1 },
	    .tile_alloc          = { path_wgs, 1, 1 },
	    .path_count_setup    = { 1, 1, 1 },
	    .backdrop            = { path_wgs, 1, 1 },
	    .coarse              = { width_in_bins, height_in_bins, 1 },
	    .path_tiling_setup   = { 1, 1, 1 },
	    .fine                = { width_in_tiles, height_in_tiles, 1 },
	};

	return wc;
}

// ----------------------------------------------------------------------

/// Generate data for 16 sample lookup table - this is used for anti-aliasing
static void generate_msaa16_lut( std::vector<uint8_t>& table ) {

	// Width is number of discrete translations
	static constexpr uint64_t MASK16_WIDTH = 64;
	// Height is the number of discrete slopes
	static constexpr uint64_t MASK16_HEIGHT = 64;

	// This is based on the [D3D11 standard sample pattern].
	//
	// [D3D11 standard sample pattern]: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_standard_multisample_quality_levels
	static constexpr uint8_t PATTERN_16[ 16 ] = { 1, 8, 4, 11, 15, 7, 3, 12, 0, 9, 5, 13, 2, 10, 6, 14 };

	auto one_mask_16 = []( double slope, double translation, bool is_pos ) -> uint16_t {
		if ( is_pos ) {
			translation = 1. - translation;
		}

		uint16_t result = 0;

		int i = 0;
		for ( auto const& item : PATTERN_16 ) {

			double y = ( double( i ) + 0.5 ) * 0.0625;
			double x = ( double( item ) + 0.5 ) * 0.0625;
			if ( !is_pos ) {
				y = 1.0 - y;
			}
			if ( ( x - ( 1.0 - translation ) ) * ( 1. - slope ) - ( y - translation ) * slope >= 0. ) {
				result |= 1 << i;
			}
			i++;
		}

		return result;
	};

	/// Make a lookup table of half-plane masks.
	///
	/// The table is organized into two blocks each with `MASK16_HEIGHT/2` slopes.
	/// The first block is negative slopes (x decreases as y increases),
	/// the second as positive.

	table.reserve( MASK16_WIDTH * MASK16_HEIGHT * 2 );

	for ( int i = 0; i != MASK16_HEIGHT * MASK16_WIDTH; i++ ) {

		const auto HALF_HEIGHT = MASK16_HEIGHT >> 1;
		uint64_t   u           = i % MASK16_WIDTH;
		uint64_t   v           = i / MASK16_WIDTH;

		bool is_pos = v >= HALF_HEIGHT;

		double y = ( double( v % HALF_HEIGHT ) + 0.5 ) * ( 1.0 / double( HALF_HEIGHT ) );
		double x = ( double( u ) + 0.5 ) * ( 1.0 / double( MASK16_WIDTH ) );

		uint16_t mask = one_mask_16( y, x, is_pos );

		table.emplace_back( ( mask >> 8 ) & 0xff ); // big end next
		table.emplace_back( mask & 0xff );          // little end first
	}
}

static void render_gradient( le_2d_colour_stop_t const* colour_stops, size_t colour_stops_count, uint32_t* data ) {
	// render all colour stops into 512 colours
	assert( colour_stops_count > 1 );

	float previous_t = 0;
	float next_t     = 0;

	le_2d::Colour previous_colour = colour_stops->colour;
	le_2d::Colour next_colour     = previous_colour;

	size_t j = 0;

	le_2d::Colour blended_colour{ 0.f, 0.f, 0.f, 0.f };

	for ( size_t i = 0; i != N_GRADIENT_SAMPLES; i++ ) {

		float t = i / float( N_GRADIENT_SAMPLES - 1 );

		while ( t > next_t ) {
			previous_t      = next_t;
			previous_colour = next_colour;
			if ( j + 1 < colour_stops_count ) {
				next_t      = colour_stops[ j + 1 ].offset;
				next_colour = colour_stops[ j + 1 ].colour;
				j++;
			} else {
				break;
			}
		}

		float distance_t = next_t - previous_t;

		if ( distance_t < 1e-9 ) {
			blended_colour = next_colour;
		} else {

			// TODO: Respect colour space when blending

			// we just do linear blending of linear colours for now -- we should implement
			// blending in a nicer colour space - perhaps oklab so that we can get
			// nicer gradients.

			blended_colour.r = previous_colour.r + ( next_colour.r - previous_colour.r ) * ( ( t - previous_t ) / distance_t );
			blended_colour.g = previous_colour.g + ( next_colour.g - previous_colour.g ) * ( ( t - previous_t ) / distance_t );
			blended_colour.b = previous_colour.b + ( next_colour.b - previous_colour.b ) * ( ( t - previous_t ) / distance_t );
			blended_colour.a = previous_colour.a + ( next_colour.a - previous_colour.a ) * ( ( t - previous_t ) / distance_t );
		}

		data[ i ] = blended_colour.to_premult_rgba_u32();
	}
}

// ----------------------------------------------------------------------

struct RampCache {
	static constexpr size_t MAX_ENTRIES = 64;

	struct Entry {
		uint32_t id; // id into cache (line index of gradient cache image)R
		uint32_t epoch;
	};

	uint32_t                            epoch = 0;
	std::unordered_map<uint64_t, Entry> entries;
	std::vector<uint32_t>               data_int32; // raw data for sampled gradients encoded into N_GRADIENT_SAMPLES colour samples (each colour sample encoded to uint32_t)

	void maintain() {

		epoch++;

		// evict any element that has an id that is greater as the max number of entries

		for ( auto it = entries.begin(); it != entries.end(); ) {
			if ( it->second.id > MAX_ENTRIES ) {
				it = entries.erase( it );
				continue;
			}
			it++;
		}

		// truncate data_int32_t if it was greater than MAX_ENTRIES

		if ( data_int32.size() / N_GRADIENT_SAMPLES > MAX_ENTRIES ) {
			data_int32.resize( MAX_ENTRIES * N_GRADIENT_SAMPLES );
		}
	};

	uint32_t add( le_2d_colour_stop_t const* stops_start, le_2d_colour_stop_t const* stops_end ) {

		uint64_t key = SpookyHash::Hash64( stops_start, sizeof( le_2d_colour_stop_t ) * ( stops_end - stops_start ), 0 );

		auto it = entries.find( key );

		if ( it != entries.end() ) {
			// an already existing entry with this key was found
			it->second.epoch = epoch; // mark it as used by the current epoch
			return it->second.id;
		}

		// ----------| no current entry was found

		if ( entries.size() < MAX_ENTRIES ) {
			// We still have space to add another entry

			uint32_t id = data_int32.size() / N_GRADIENT_SAMPLES;

			entries[ key ] = {
			    .id    = id,
			    .epoch = epoch,
			};

			data_int32.insert( data_int32.end(), N_GRADIENT_SAMPLES, 0 );
			render_gradient( stops_start, stops_end - stops_start, data_int32.data() + id * N_GRADIENT_SAMPLES );
			return id;
		}

		// ---------| invariant: cache is full of (potentially old) entries

		// Can we make space for another entry by evicting an entry that is stale?

		// Find the first entry that has an epoch that is 2 frames behind
		for ( it = entries.begin(); it != entries.end(); it++ ) {
			if ( it->second.epoch + 2 < epoch ) {
				break;
			}
		}

		if ( it != entries.end() ) {
			// We found an entry that we can re-use
			uint32_t id      = it->second.id;
			it->second.epoch = epoch;
			render_gradient( stops_start, stops_end - stops_start, data_int32.data() + id * N_GRADIENT_SAMPLES );
			return id;
		}

		// ---------| invariant: No element that can be re-used has been found

		// We must append an extra element (it will be removed by the next call to `maintain()`)

		uint32_t id = data_int32.size() / N_GRADIENT_SAMPLES;

		entries[ key ] = {
		    .id    = id,
		    .epoch = epoch,
		};

		data_int32.insert( data_int32.end(), N_GRADIENT_SAMPLES, 0 );
		render_gradient( stops_start, stops_end - stops_start, data_int32.data() + id * N_GRADIENT_SAMPLES );

		return id;
	};
};

// ----------------------------------------------------------------------

struct Resolver {
	std::vector<ResolvedPatch> resolved_patches;
	RampCache                  ramp_cache;

	void maintain() {
		ramp_cache.maintain();
		resolved_patches.clear();
	}
};

// ----------------------------------------------------------------------

static bool le_2d_encoder_resolve_patches( le_2d_encoder_o* e, Resolver& resources ) {

	resources.maintain();

	for ( auto& patch : e->resources.patches ) {

		switch ( patch.type ) {
		case Patch::Type::Ramp: {
			// if we have a ramp, we want to encode this
			auto const& p = patch.data.as_ramp;

			uint32_t ramp_id = resources.ramp_cache.add(
			    e->resources.colour_stops.data() + p.stops_start,
			    e->resources.colour_stops.data() + p.stops_end );

			ResolvedPatch r{
			    .type = ResolvedPatch::Type::Ramp,
			    .data = {
			        .as_ramp = {
			            .draw_data_offset = p.draw_data_offset,
			            .ramp_id          = ramp_id,
			            .extend           = p.extend,
			        },

			    },
			};

			// Now patch the ramp `index_and_extent` in the draw data stream for this gradient
			//
			// it does not matter whether the gradient is a linear, radial, or sweep gradient
			// as the first field in any encoded gradients is an uint32_t holding the ramp-index-and-extend
			// information.

			uint32_t& gradient_ramp_index_and_extent = e->draw_data[ p.draw_data_offset ];

			// this must have been set to 0 when initially encoding the gradient information into the draw stream.
			assert( gradient_ramp_index_and_extent == 0 );

			gradient_ramp_index_and_extent = uint32_t( ramp_id << 2 ) | uint32_t( p.extend );

			resources.resolved_patches.emplace_back( std::move( r ) );

			break;
		}
		case Patch::Type::Undefined:
		default:
			assert( false ); // unreachable
		}
	}

	return true;
}

// --------------------------------------------------------------------------------
template <typename T>
inline static size_t append_to_stream( T const& src, uint8_t* target ) {
	size_t num_bytes = src.size() * sizeof( typename T::value_type );
	memcpy( target, src.data(), num_bytes );
	return num_bytes;
}

// --------------------------------------------------------------------------------

static bool encoder_encode_to_bytes( le_2d_encoder_o const* e, uint8_t* bytes, size_t* bytes_count, rasterizer_layout_data_t* p_layout ) {

	// TODO: do we need to do something special here if the encoder has patches for ramps and
	// images or glyph runs? (probably we do.)

	assert( e );

	size_t n_path_tags     = e->path_tags.size() + e->n_open_clips;
	size_t path_tag_padded = align_up( n_path_tags, 4 * PATH_REDUCE_WG_SZ );

	// size_t path_tags_size  = n_path_tags * sizeof( decltype( e->path_tags )::value_type );
	size_t path_data_size  = e->path_data.size() * sizeof( decltype( e->path_data )::value_type );
	size_t draw_tags_size  = e->draw_tags.size() * sizeof( decltype( e->draw_tags )::value_type );
	size_t open_clips_size = e->n_open_clips * sizeof( decltype( e->draw_tags )::value_type );
	size_t draw_data_size  = e->draw_data.size() * sizeof( decltype( e->draw_data )::value_type );
	size_t transforms_size = e->transforms.size() * sizeof( decltype( e->transforms )::value_type );
	size_t styles_size     = e->styles.size() * sizeof( decltype( e->styles )::value_type );

	size_t buffer_size =
	    path_tag_padded +
	    path_data_size +
	    draw_tags_size + open_clips_size +
	    draw_data_size +
	    transforms_size +
	    styles_size;

	if ( nullptr == bytes_count ) {
		return false;
	}

	if ( *bytes_count < buffer_size ) {
		*bytes_count = buffer_size;
		return false;
	}

	if ( nullptr == bytes ) {
		return false;
	}

	if ( nullptr == p_layout ) {
		return 0;
	}

	size_t used_bytes = 0;

	rasterizer_layout_data_t& layout = *p_layout;

	layout = {
	    .n_paths = e->n_paths,
	    .n_clips = e->n_clips,
	};

	layout.path_tag_base = used_bytes;

	used_bytes += append_to_stream( e->path_tags, bytes + used_bytes );

	if ( e->n_open_clips ) {
		// Append any open clips as pathtag::Path
		std::vector<PathTag> tmpOpenClips( e->n_open_clips, { PathTag::PATH } );
		used_bytes += append_to_stream( tmpOpenClips, bytes + used_bytes );
	}

	assert( align_up( used_bytes, 4 * PATH_REDUCE_WG_SZ ) == path_tag_padded );

	// ACHTUNG
	// at this point, we can't just jump over the bytes that are not used,
	// because they could contain garbage data which will confuse the gpu
	// -- we must zero out padding.
	memset( bytes + used_bytes, 0, path_tag_padded - used_bytes );

	used_bytes = path_tag_padded;

	layout.path_data_base = used_bytes / sizeof( uint32_t );

	used_bytes += append_to_stream( e->path_data, bytes + used_bytes );

	layout.draw_tag_base = used_bytes / sizeof( uint32_t );

	{
		layout.bin_data_start = 0;

		for ( auto const& t : e->draw_tags ) {
			layout.bin_data_start += draw_tag_get_info_size( t );
		}
	}

	used_bytes += append_to_stream( e->draw_tags, bytes + used_bytes );

	if ( e->n_open_clips ) {

		// Append any open clips as pathtag::Path
		std::vector<DrawTag> tmpOpenClips( e->n_open_clips, { DrawTag::END_CLIP } );

		used_bytes += append_to_stream( tmpOpenClips, bytes + used_bytes );
	}

	// draw data stream

	layout.draw_data_base = used_bytes / sizeof( uint32_t );
	used_bytes += append_to_stream( e->draw_data, bytes + used_bytes );

	layout.transform_base = used_bytes / sizeof( uint32_t );

	used_bytes += append_to_stream( e->transforms, bytes + used_bytes );

	layout.style_base = used_bytes / sizeof( uint32_t );

	used_bytes += append_to_stream( e->styles, bytes + used_bytes );

	layout.n_drawobj = layout.n_paths;

	assert( used_bytes == buffer_size );

#undef vec_count_bytes
#undef append_to_stream

	*bytes_count = used_bytes;
	return true;
}

/*
 *
 * // Bitflags for each stage that can fail allocation.
 * const STAGE_BINNING: u32 = 0x1u;
 * const STAGE_TILE_ALLOC: u32 = 0x2u;
 * const STAGE_FLATTEN: u32 = 0x4u;
 * const STAGE_PATH_COUNT: u32 = 0x8u;
 * const STAGE_COARSE: u32 = 0x10u;
 */
struct vello_bump_allocator_data_t {
	uint32_t failed; // Bitmask of stages that have failed allocation.
	uint32_t binning;
	uint32_t ptcl;
	uint32_t tile;
	uint32_t seg_counts;
	uint32_t segments;
	uint32_t blend;
	uint32_t lines;
};
// ----------------------------------------------------------------------

struct le_2d_o {
	// members

	std::atomic<uint32_t> intrusive_pointer_count = 0; // this object needs to stay alife for the duration of its own lifetime, and for the duration of any callbacks that is spawns. callbacks decrease the intrusive counter at the end of their execution
	le_renderer_o* const  renderer;                    // non-owning

	static constexpr uint8_t transfer_lut_mask   = 0x1;
	static constexpr uint8_t transfer_scene_mask = 0x2;
	static constexpr uint8_t transfer_gradient_cache_mask = 0x4;

	std::array<vello_bump_allocator_data_t, 4> bump_allocator_history; // we keep 4 frames around
	std::atomic<uint32_t>                      bump_allocator_history_ring_buffer_idx = 0;

	uint8_t rasterizer_xfer_flags = transfer_lut_mask | transfer_scene_mask | transfer_gradient_cache_mask; // masked by one of the masks above

	std::vector<uint8_t> scene_bytes;
	uint64_t             previous_scene_hash; // hash of current scene

	std::vector<uint8_t> mask_lut_bytes;

	le_buffer_resource_handle buf_vello_scene    = LE_BUF_RESOURCE( "vello.scene" );
	le_buffer_resource_handle buf_reduced        = LE_BUF_RESOURCE( "vello.reduced_buf" );
	le_buffer_resource_handle buf_reduced2       = LE_BUF_RESOURCE( "vello.reduced2_buf" );
	le_buffer_resource_handle buf_reduced_scan   = LE_BUF_RESOURCE( "vello.reduced_scan_buf" );
	le_buffer_resource_handle buf_tagmonoid      = LE_BUF_RESOURCE( "vello.tagmonoid_buf" );
	le_buffer_resource_handle buf_path_bbox      = LE_BUF_RESOURCE( "vello.path_bbox_buf" );
	le_buffer_resource_handle buf_bump           = LE_BUF_RESOURCE( "vello.bump_buf" );
	le_buffer_resource_handle buf_lines          = LE_BUF_RESOURCE( "vello.lines_buf" );
	le_buffer_resource_handle buf_draw_reduced   = LE_BUF_RESOURCE( "vello.draw_reduced_buf" );
	le_buffer_resource_handle buf_draw_monoid    = LE_BUF_RESOURCE( "vello.draw_monoid_buf" );
	le_buffer_resource_handle buf_info_bin_data  = LE_BUF_RESOURCE( "vello.info_bin_data_buf" );
	le_buffer_resource_handle buf_clip_inp       = LE_BUF_RESOURCE( "vello.clip_inp_buf" );
	le_buffer_resource_handle buf_clip_bic       = LE_BUF_RESOURCE( "vello.clip_bic_buf" );
	le_buffer_resource_handle buf_clip_el        = LE_BUF_RESOURCE( "vello.clip_el_buf" );
	le_buffer_resource_handle buf_clip_bbox      = LE_BUF_RESOURCE( "vello.clip_bbox_buf" );
	le_buffer_resource_handle buf_draw_bbox      = LE_BUF_RESOURCE( "vello.draw_bbox_buf" );
	le_buffer_resource_handle buf_bin_header     = LE_BUF_RESOURCE( "vello.bin_header_buf" );
	le_buffer_resource_handle buf_path           = LE_BUF_RESOURCE( "vello.path_buf" );
	le_buffer_resource_handle buf_tile           = LE_BUF_RESOURCE( "vello.tile_buf" );
	le_buffer_resource_handle buf_indirect_count = LE_BUF_RESOURCE( "vello.indirect_count" );
	le_buffer_resource_handle buf_seg_counts     = LE_BUF_RESOURCE( "vello.seg_counts_buf" );
	le_buffer_resource_handle buf_segments       = LE_BUF_RESOURCE( "vello.segments_buf" );
	le_buffer_resource_handle buf_ptcl           = LE_BUF_RESOURCE( "vello.ptcl_buf" );
	le_buffer_resource_handle buf_blend_spill    = LE_BUF_RESOURCE( "vello.blend_spill" );
	le_buffer_resource_handle buf_mask_lut       = LE_BUF_RESOURCE( "vello.mask_lut" );

	le_buffer_resource_handle buf_bump_cpu = LE_BUF_RESOURCE( "vello.bump_buf_cpu_readback" );

	le_resource_info_t buf_vello_scene_info;
	le_resource_info_t buf_reduced_info;
	le_resource_info_t buf_reduced2_info;
	le_resource_info_t buf_reduced_scan_info;
	le_resource_info_t buf_tagmonoid_info;
	le_resource_info_t buf_path_bbox_info;
	le_resource_info_t buf_bump_info;
	le_resource_info_t buf_lines_info;
	le_resource_info_t buf_draw_reduced_info;
	le_resource_info_t buf_draw_monoid_info;
	le_resource_info_t buf_info_bin_data_info;
	le_resource_info_t buf_clip_inp_info;
	le_resource_info_t buf_clip_bic_info;
	le_resource_info_t buf_clip_el_info;
	le_resource_info_t buf_clip_bbox_info;
	le_resource_info_t buf_draw_bbox_info;
	le_resource_info_t buf_bin_header_info;
	le_resource_info_t buf_path_info;
	le_resource_info_t buf_tile_info;
	le_resource_info_t buf_indirect_count_info;
	le_resource_info_t buf_seg_counts_info;
	le_resource_info_t buf_segments_info;
	le_resource_info_t buf_ptcl_info;
	le_resource_info_t buf_blend_spill_info;
	le_resource_info_t buf_mask_lut_info;

	le_resource_info_t buf_bump_cpu_info;

	le_image_resource_handle img_gradients   = LE_IMG_RESOURCE( "vello.image_gradient" );
	le_image_resource_handle img_image_atlas = LE_IMG_RESOURCE( "vello.image_atlas" );

	le_image_resource_handle img_output = nullptr; // externally set by user

	uint32_t failed; // Bitmask of stages that have failed allocation.
	uint32_t binning;
	uint32_t ptcl;
	uint32_t tile;
	uint32_t seg_counts;
	uint32_t segments;
	uint32_t blend;
	uint32_t lines;

	vello_bump_allocator_data_t bump_alloc_data{
	    .failed     = 0,
	    .binning    = 1 << 21, /// count of uint32_t in binning buffer allocation
	    .ptcl       = 0,       /// count of uint32_t in per-tile command list buffer allocation (NOTE that assumed pre- allocated amount of memory depends on number of tiles, we calculate this on update)
	    .tile       = 1 << 21, /// count of Tiles in tile buffer allocation
	    .seg_counts = 1 << 21, /// count of SegmentCounts in segment count buffer allocation
	    .segments   = 1 << 22, /// count of PathSegments in segment buffer allocation
	    .blend      = 1 << 22, /// count of uint32_t pixels in blend spill buffer allocation
	    .lines      = 1 << 10, /// count of LineSoups in line soup buffer allocation
	};

	RasterizerUboData rasterizer_args = {};

	WorkGroupCounts wg_counts;

	BufferSizes bsz;

	Resolver resource_cache = {};

	bool should_use_msaa = false; // whether to use area or msaa smoothing (false means area, default)

	// -----

	/*
	 * On clear callbacks -- the on clear callback gets called with the data
	 * in callback data when the backend frame that was used to enqueue our drawing
	 * commands has crossed the fence and is about to be cleared.
	 *
	 */

	size_t num_data_frames        = 0; // number of data frames in the backend, we must keep the number
	size_t current_data_frame_idx = 0;

	struct on_backend_clear_callback_data_t {
		le_2d_o* self           = nullptr;
		uint32_t data_frame_idx = 0;
	};

	std::vector<on_backend_clear_callback_data_t> on_clear_callback_data;

	// -----

	static_assert( sizeof( char ) == sizeof( uint8_t ), "char and uint8_t must be the same size." );
};

// ----------------------------------------------------------------------

static le_2d_o* le_2d_create( le_renderer_o* renderer ) {
	auto self = new le_2d_o( 1, renderer );

	return self;
}

// ----------------------------------------------------------------------

static void le_2d_decrement_intrusive_pointer( le_2d_o* self ) {
	uint32_t count = --self->intrusive_pointer_count;

	if ( count == 0 ) {
		delete self;
		logger().info( "Destroyed le_2d Instance: %p", self );
	}
}

// ----------------------------------------------------------------------

static void le_2d_destroy( le_2d_o* self ) {
	le_2d_decrement_intrusive_pointer( self );
}

// ----------------------------------------------------------------------

static bool le_2d_encode_scene( le_2d_o* self, le_2d_encoder_o const* e, le_resource_info_t* out_img_info, uint32_t background_colour_argb ) {

	if ( nullptr == out_img_info ) {
		logger().warn( "out_img_info not set" );
		return false;
	}
	// ---------| invariant: resource is valid

	if ( out_img_info->type != LeResourceType::eImage ) {
		logger().error( "out_img_info is not of resource type Image" );
		return false;
	}

	// ---------| resource type is image

	if ( 0 == out_img_info->image.extent.width * out_img_info->image.extent.height * out_img_info->image.extent.depth ) {
		logger().warn( "out_img_info has no area (one or more of width, height, depth is zero)" );
		return false;
	}

	// ---------| image has valid extent

	{
		// update rasterizer layout data

		size_t num_scene_bytes = self->scene_bytes.size();

		while ( false == encoder_encode_to_bytes( e, self->scene_bytes.data(), &num_scene_bytes, &self->rasterizer_args.layout ) ) {
			self->scene_bytes.resize( num_scene_bytes );
		};

		// snip off any extra bytes that were not used
		self->scene_bytes.resize( num_scene_bytes );

		// self->rasterizer_args.layout       = le_2d_api::le_2d_encoder_i.encode_to_bytes( e, self->scene_bytes );
		// self->rasterizer_args.binning_size = buf_bin_data_num_bytes / sizeof( uint32_t ) - self->rasterizer_args.layout.bin_data_start;

		self->buf_vello_scene_info =
		    le::BufferInfoBuilder()
		        .addUsageFlags( le::BufferUsageFlagBits::eTransferDst |
		                        le::BufferUsageFlagBits::eStorageBuffer )
		        .setSize( std::max<size_t>( self->buf_vello_scene_info.buffer.size, self->scene_bytes.size() ) ) // to prevent re-allocation every time a smaller number of elements is required, we do just keep the buffer at the maximum size
		        .build();

		self->rasterizer_args.base_color      = background_colour_argb;
		self->rasterizer_args.target_width    = out_img_info->image.extent.width;
		self->rasterizer_args.target_height   = out_img_info->image.extent.height;
		self->rasterizer_args.width_in_tiles  = ( self->rasterizer_args.target_width + TILE_UNIT - 1 ) / TILE_UNIT;
		self->rasterizer_args.height_in_tiles = ( self->rasterizer_args.target_height + TILE_UNIT - 1 ) / TILE_UNIT;

		self->wg_counts = get_work_group_counts( self->rasterizer_args.layout, self->rasterizer_args.width_in_tiles, self->rasterizer_args.height_in_tiles );
	}
	{
		auto const& wg              = self->wg_counts;
		size_t      n_paths         = self->rasterizer_args.layout.n_paths;
		size_t      n_draw_objs     = self->rasterizer_args.layout.n_drawobj;
		size_t      n_clips         = self->rasterizer_args.layout.n_clips;
		size_t      path_tag_wgs    = wg.path_reduce[ 0 ];
		size_t      reduced_size    = wg.use_large_path_scan ? align_up( path_tag_wgs, PATH_REDUCE_WG_SZ ) : path_tag_wgs;
		size_t      binning_wgs     = wg.binning[ 0 ];
		size_t      draw_monoid_wgs = wg.draw_reduce[ 0 ];
		size_t      n_paths_aligned = align_up( n_paths, 256 );
		size_t      ptcl_initial_alloc_size = ( self->rasterizer_args.width_in_tiles * self->rasterizer_args.height_in_tiles ) * 64; // 64 corresponds to PTCL_INITIAL_ALLOC

		self->rasterizer_args.binning_size    = self->bump_alloc_data.binning;
		self->rasterizer_args.lines_size      = self->bump_alloc_data.lines;      // number of lines in linesoup, ( sizeof LineSoup == 16 )
		self->rasterizer_args.tiles_size      = self->bump_alloc_data.tile;       // number of tiles, sizeof(Tile) = 8
		self->rasterizer_args.seg_counts_size = self->bump_alloc_data.seg_counts; //
		self->rasterizer_args.segments_size   = self->bump_alloc_data.segments;   // number of segments (sizeof Segment ==8)
		self->rasterizer_args.blend_size      = self->bump_alloc_data.blend;
		self->rasterizer_args.ptcl_size       = ptcl_initial_alloc_size + self->bump_alloc_data.ptcl; // number of bytes available (shared by all tiles) for per-tile command list allocations

		// The `coarse` shader (only user of `bump.ptcl`) assumes a certain amount of memory pre-allocated
		// before it will use `bump.ptcl` for  dynamic memory allocation. We must therefore make sure that
		// the pre-allocated memory amount matches the shader's expectations. The shader calculates pre-
		// allocated memory size as (witdh_in_tiles * height_in_tiles * PTCL_INITIAL_ALLOC); this is then
		// the count of uint32_t that need to be pre-allocated at minimum. Anything that bump.ptcl reports,
		// needs to be added on top of this.
		//

		self->bsz = {
		    // all sizes here are given in bytes.
		    .path_reduced      = 20 * reduced_size,
		    .path_reduced2     = 20 * PATH_REDUCE_WG_SZ,
		    .path_reduced_scan = 20 * reduced_size,
		    .path_monoids      = 20 * path_tag_wgs * PATH_REDUCE_WG_SZ,
		    .path_bboxes       = 24 * n_paths,
		    .draw_reduced      = 20 * draw_monoid_wgs,
		    .draw_monoids      = 20 * n_draw_objs,
		    .info              = self->rasterizer_args.layout.bin_data_start, // does this need to be scaled?
		    .clip_inps         = 8 * n_clips,
		    .clip_els          = 32 * n_clips,
		    .clip_bics         = 8 * ( n_clips / CLIP_REDUCE_WG_SZ ),
		    .clip_bboxes       = 16 * n_clips,
		    .draw_bboxes       = 16 * n_paths,
		    .bump_alloc        = 32,
		    .indirect_count    = 16,
		    .bin_headers       = 8 * binning_wgs * 256,
		    .paths             = 32 * n_paths_aligned,

		    // The following sizes are based on heurisics.
		    //
		    // If you see the indirect count for any of our indirect draws go to x==0, then this means
		    // that the bump allocator failed for one of these categories; and it means that you need
		    // to provide more space for the affected category.
		    //
		    .lines       = 20 * self->rasterizer_args.lines_size,
		    .bin_data    = 4 * ( self->rasterizer_args.binning_size + self->rasterizer_args.layout.bin_data_start ),
		    .tiles       = 8 * self->rasterizer_args.tiles_size,
		    .seg_counts  = 8 * self->rasterizer_args.seg_counts_size,
		    .segments    = 20 * self->rasterizer_args.segments_size,
		    .blend_spill = 4 * self->rasterizer_args.blend_size, // 16 * 16 (1<<8) is one blend spill, so this allows for 4096 spills.
		    .ptcl        = 4 * self->rasterizer_args.ptcl_size,  // given in bytes per-tile command list (this will be split into per-tile segments), initial_alloc + number of allocations,
		};
	}
	return true;
}

// ----------------------------------------------------------------------
// Creates a compute pipeline state object from compressed shader code.
// shader code is provided as a base85 encoded string which contains compressed
// spir-v. Spirv is decompressed via stb_decompress.
//
// We do this so that we can embed shader code directly into this
// compilation unit.
static le_cpso_handle create_cpso_from_compressed_and_encoded_spirv_code( le_pipeline_manager_o* pm, char const* compressed_shader_code, char const* debug_name = "" ) {
	int  compressed_size = ( ( ( int )strlen( compressed_shader_code ) + 4 ) / 5 ) * 4;
	auto decoded_data    = ( uint8_t* )malloc( compressed_size );
	decode_85( ( unsigned char const* )compressed_shader_code, decoded_data );

	const unsigned int    buf_spv_num_bytes = stb_decompress_length( ( const unsigned char* )decoded_data );
	std::vector<uint32_t> buf_spv_code( buf_spv_num_bytes / 4 );
	stb_decompress( ( uint8_t* )buf_spv_code.data(), ( const unsigned char* )decoded_data, ( unsigned int )compressed_size );

	free( decoded_data );

	return LeComputePipelineBuilder( pm )
	    .setShaderStage(
	        LeShaderModuleBuilder( pm )
	            .setShaderStage( le::ShaderStage::eCompute )
	            .setSpirvCode( ( uint32_t* )buf_spv_code.data(), buf_spv_code.size() )
	            .setSourceLanguage( le::ShaderSourceLanguage::eSpirv )
	            .setSourceFilePath( debug_name )
	            .build() )
	    .build();
};

// ----------------------------------------------------------------------

static void on_backend_frame_clear_callback( void* user_data ) {
	auto data = static_cast<le_2d_o::on_backend_clear_callback_data_t*>( user_data );

	// ----------| invariant: callback object is valid

	uint32_t frame_id = data->data_frame_idx;

	// we need to cancel this callback if the object was destroyed.
	// in which case we must not touch anything related to the object.

	// at this point we want to update sizes for allocations
	// we need to be careful because another thread might be
	// currently rendering

	auto backend = le_renderer_api_i->le_renderer_i.get_backend( data->self->renderer );

	char* mapped_data = static_cast<char*>( le_backend_vk_api_i->private_backend_vk_i.frame_get_mapped_data_for_buffer( backend, frame_id, data->self->buf_bump_cpu ) );

	if ( mapped_data ) {
		uint32_t                     bump_idx            = ( data->self->bump_allocator_history_ring_buffer_idx++ ) % data->self->bump_allocator_history.size();
		vello_bump_allocator_data_t* bump_allocator_readback_data = &data->self->bump_allocator_history[ bump_idx ];

		memcpy( bump_allocator_readback_data, mapped_data + N_BYTES_READBACK_OFFSET * frame_id, sizeof( vello_bump_allocator_data_t ) );

		auto&       args = data->self->bump_alloc_data;
		auto const& b    = *bump_allocator_readback_data;

		if ( b.failed != 0 ) {
			if ( args.lines < b.lines ) {
				args.lines *= 2;
			}
			if ( ( b.failed & 0x1 ) || args.binning < b.binning ) {
				args.binning *= 2;
			}
			if ( args.ptcl < b.ptcl ) {
				args.ptcl *= 2;
			}
			if ( args.tile < b.tile ) {
				args.tile *= 2;
			}
			if ( args.seg_counts < b.seg_counts ) {
				args.seg_counts *= 2;
			}
			if ( args.segments < b.segments ) {
				args.segments *= 2;
			}
			if ( args.blend < b.blend ) {
				args.blend *= 2;
			}
			if ( args.lines < b.lines ) {
				args.lines *= 2;
			}
		}
	}

	/* TODO: update size counts -- we could write bump_allocator_data_t to the main object
	 * how to we make sure that we don't overwrite the current frame?
	 *
	 * + we can calculate the max number of allocations here - this is the only invocation
	 *   that has any change in counts.
	 *
	 * + we write the updated change to the frame - we could keep this shared, or make these counts atomic, in which
	 *   case they will just be picked up by the next element.
	 *
	 *   use a ring buffer to record the current value for each of these elements -- keep them in a struct
	 * 	in here, calculate the new max values -- if there is a change, then we want to update the counts in the main object.
	 *
	 *  We need to make sure that the data that we get back from the readback is meaningful - we know which stage has failed, but we don't know
	 *  what size it would require; if we double instantly, then we don't see the effects until the frame has come back, this means
	 *  the next frame might double again and we end up with a buffer that is way too big.
	 *
	 */

	// Tell the le_2d_object that issued the callback that the callback is complete,
	// and that it has one less reason to defer deletion.
	le_2d_decrement_intrusive_pointer( data->self );
}

// ----------------------------------------------------------------------

static void le_2d_update( le_2d_o* self, le_rendergraph_o* rg, le_2d_encoder_o* encoder_2d, le_image_resource_handle img_output, le_resource_info_t* img_output_info, uint32_t background_colour_argb ) {

	if ( self->num_data_frames == 0 ) {

		// In case num_data_frames is zero, we must query the backend and find out how
		// many backend data frames there are, so that we can provide a corresponding
		// set of on backend frame clear callbacks.
		// We need frame clear callbacks so that we can readback from the GPU and find
		// out whether we provided enough space for the bump allocator.

		auto backend          = le_renderer_api_i->le_renderer_i.get_backend( self->renderer );
		self->num_data_frames = le_backend_vk_api_i->vk_backend_i.get_data_frames_count( backend );

		self->on_clear_callback_data.reserve( self->num_data_frames );

		for ( uint32_t i = 0; i != self->num_data_frames; i++ ) {
			// increase the number of owners to our self object by one --
			// the callback will decrease once it has executed.
			self->on_clear_callback_data.emplace_back( self, i );
		}
	}

	assert( self->num_data_frames != 0 );

	if ( self->mask_lut_bytes.empty() ) {
		generate_msaa16_lut( self->mask_lut_bytes );
	}

	if ( !encoder_2d->resources.patches.empty() ) {

		// In case that there are any late-bound resources,
		// we must resolve (turn them into cache references here)

		// This will update the cache.
		// This means we need to upload any tainted cache resources.

		/*
		 *  Resolving patches works differently for each type of patch
		 *  for now, we are only interested in ramps (and possibly images)
		 *
		 *  1. for all ramp patches: add to the ramp cache
		 *  2. use the ramp cache id to store a ResolvedRamp into encoder.patches
		 *  3. patch gradient draw_data stream entries with the correct ramp_id and extend for each ramp
		 *
		 *  TODO:
		 *  4. allocate gradient cache image resource with rendergraph
		 *  5. upload gradient cache data to gradient image resource
		 *
		 */
		le_2d_encoder_resolve_patches( encoder_2d, self->resource_cache );

		// always set gradient transfer mask
		self->rasterizer_xfer_flags |= self->transfer_gradient_cache_mask;
	}

	bool result = le_2d_encode_scene( self, encoder_2d, img_output_info, background_colour_argb );

	if ( false == result ) {
		assert( false );
		logger().error( "Could not encode scene." );
		return;
	}

	{
		uint64_t scene_hash = SpookyHash::Hash64( self->scene_bytes.data(), self->scene_bytes.size(), 0 );

		if ( scene_hash != self->previous_scene_hash ) {
			// Only transfer data if contents have changed
			self->rasterizer_xfer_flags |= self->transfer_scene_mask;
			self->previous_scene_hash = scene_hash;
			// logger().info( "scene update detected" );
		}

		// FIXME: for now, we update the scene every time.
		self->rasterizer_xfer_flags |= self->transfer_scene_mask;
	}

	self->img_output = img_output;

	// -------

	le::RenderGraph renderGraph( rg );

	auto build_buffer_info = []( size_t num_bytes, le::BufferUsageFlags const& flags ) -> le_resource_info_t {
		// note that we align up to 256 bytes by default, because we don't want to have super small buffers
		// flying around, and it's very likely that small buffers are less performant because they might
		// not fit cache lines that well.
		// we're also making sure that there is at least 256 bytes that are allocated because we don't want any empty
		// allocations
		return le::BufferInfoBuilder().addUsageFlags( flags ).setSize( align_up( std::max<size_t>( 1, num_bytes ), 256 ) ).build();
	};

	self->buf_reduced_info       = build_buffer_info( self->bsz.path_reduced, le::BufferUsageFlagBits::eStorageBuffer | le::BufferUsageFlagBits::eTransferDst );
	self->buf_reduced2_info      = build_buffer_info( self->bsz.path_reduced2, le::BufferUsageFlagBits::eStorageBuffer | le::BufferUsageFlagBits::eTransferDst );
	self->buf_reduced_scan_info  = build_buffer_info( self->bsz.path_reduced_scan, le::BufferUsageFlagBits::eStorageBuffer | le::BufferUsageFlagBits::eTransferDst );
	self->buf_tagmonoid_info     = build_buffer_info( self->bsz.path_monoids, le::BufferUsageFlagBits::eStorageBuffer | le::BufferUsageFlagBits::eTransferDst );
	self->buf_path_bbox_info     = build_buffer_info( self->bsz.path_bboxes, le::BufferUsageFlagBits::eStorageBuffer | le::BufferUsageFlagBits::eTransferDst );
	self->buf_bump_info          = build_buffer_info( self->bsz.bump_alloc, le::BufferUsageFlagBits::eStorageBuffer | le::BufferUsageFlagBits::eTransferDst | le::BufferUsageFlagBits::eTransferSrc ); // the size for this buffer is determined by how many threads want to allocate concurrently
	self->buf_draw_reduced_info  = build_buffer_info( self->bsz.draw_reduced, le::BufferUsageFlagBits::eStorageBuffer | le::BufferUsageFlagBits::eTransferDst );                                       // the size for this buffer is determined by how many threads want to allocate concurrently
	self->buf_lines_info         = build_buffer_info( self->bsz.lines, le::BufferUsageFlagBits::eStorageBuffer | le::BufferUsageFlagBits::eTransferDst);
	self->buf_draw_monoid_info   = build_buffer_info( self->bsz.draw_monoids, le::BufferUsageFlagBits::eStorageBuffer );
	self->buf_info_bin_data_info = build_buffer_info( self->bsz.bin_data, le::BufferUsageFlagBits::eStorageBuffer );

	self->buf_bump_cpu_info = build_buffer_info( self->bsz.bump_alloc, le::BufferUsageFlagBits::eTransferDst ); // this buffer is the cpu readback buffer for bump

	self->buf_bump_cpu_info.buffer.allocation_memory_preferred_flags = 0; // host visible
	self->buf_bump_cpu_info.buffer.allocation_memory_required_flags  = 2; // host visible
	self->buf_bump_cpu_info.buffer.allocation_memory_usage           = 9; // prefer host
	self->buf_bump_cpu_info.buffer.size                              = self->num_data_frames * N_BYTES_READBACK_OFFSET;

	self->buf_clip_inp_info  = build_buffer_info( self->bsz.clip_inps, le::BufferUsageFlagBits::eStorageBuffer ); // size for this needs to be determined by what?
	self->buf_clip_bbox_info = build_buffer_info( self->bsz.clip_bboxes, le::BufferUsageFlagBits::eStorageBuffer | le::BufferUsageFlagBits::eTransferDst  );
	self->buf_clip_el_info   = build_buffer_info( self->bsz.clip_els, le::BufferUsageFlagBits::eStorageBuffer );
	self->buf_clip_bic_info  = build_buffer_info( self->bsz.clip_bics, le::BufferUsageFlagBits::eStorageBuffer );

	self->buf_draw_bbox_info      = build_buffer_info( self->bsz.draw_bboxes, le::BufferUsageFlagBits::eStorageBuffer );
	self->buf_bin_header_info     = build_buffer_info( self->bsz.bin_headers, le::BufferUsageFlagBits::eStorageBuffer );
	self->buf_path_info           = build_buffer_info( self->bsz.paths, le::BufferUsageFlagBits::eStorageBuffer );
	self->buf_mask_lut_info       = build_buffer_info( 8196, le::BufferUsageFlagBits::eStorageBuffer ); // size is constant because this is constant data
	self->buf_tile_info           = build_buffer_info( self->bsz.tiles, le::BufferUsageFlagBits::eStorageBuffer );
	self->buf_seg_counts_info     = build_buffer_info( self->bsz.seg_counts, le::BufferUsageFlagBits::eStorageBuffer );
	self->buf_ptcl_info           = build_buffer_info( self->bsz.ptcl, le::BufferUsageFlagBits::eStorageBuffer );
	self->buf_segments_info       = build_buffer_info( self->bsz.segments, le::BufferUsageFlagBits::eStorageBuffer );
	self->buf_indirect_count_info = build_buffer_info( sizeof( uint32_t ) * 3, le::BufferUsageFlagBits::eStorageBuffer | le::BufferUsageFlagBits::eIndirectBuffer );
	self->buf_blend_spill_info    = build_buffer_info( self->bsz.blend_spill, le::BufferUsageFlagBits::eStorageBuffer );

	img_output_info->image.usage |=
	    le::ImageUsageFlagBits::eStorage | le::ImageUsageFlagBits::eTransferDst | le::ImageUsageFlagBits::eSampled;

	// update image gradients info based on current number of gradient cache entries (up to max number of entries)
	const auto img_gradients_info =
	    le::ImageInfoBuilder()
	        .setExtent( N_GRADIENT_SAMPLES, std::max<uint32_t>( self->resource_cache.ramp_cache.data_int32.size() / N_GRADIENT_SAMPLES, 1 ), 1 )
	        .addUsageFlags( le::ImageUsageFlagBits::eTransferDst | le::ImageUsageFlagBits::eSampled )
	        .setFormat( le::Format::eR8G8B8A8Unorm )
	        .build();

	const auto img_image_atlas_info =
	    le::ImageInfoBuilder()
	        .setExtent( 1, 1 )
	        .addUsageFlags( le::ImageUsageFlagBits::eTransferDst | le::ImageUsageFlagBits::eSampled )
	        .setFormat( le::Format::eR8G8B8A8Unorm )
	        .build();

	renderGraph
	    .declareResource( self->buf_vello_scene, self->buf_vello_scene_info )
	    .declareResource( self->buf_reduced, self->buf_reduced_info )

	    .declareResource( self->buf_reduced2, self->buf_reduced2_info )         // only used when large path numbers
	    .declareResource( self->buf_reduced_scan, self->buf_reduced_scan_info ) // only used when large path numbers

	    .declareResource( self->buf_tagmonoid, self->buf_tagmonoid_info )
	    .declareResource( self->buf_path_bbox, self->buf_path_bbox_info )

	    .declareResource( self->buf_bump, self->buf_bump_info )
	    .declareResource( self->buf_lines, self->buf_lines_info )
	    .declareResource( self->buf_draw_monoid, self->buf_draw_monoid_info )

	    .declareResource( self->buf_bump_cpu, self->buf_bump_cpu_info )

	    .declareResource( self->buf_draw_reduced, self->buf_draw_reduced_info )

	    .declareResource( self->buf_clip_inp, self->buf_clip_inp_info )
	    .declareResource( self->buf_clip_bbox, self->buf_clip_bbox_info )
	    .declareResource( self->buf_clip_el, self->buf_clip_el_info )
	    .declareResource( self->buf_clip_bic, self->buf_clip_bic_info )

	    .declareResource( self->buf_info_bin_data, self->buf_info_bin_data_info )
	    .declareResource( self->buf_draw_bbox, self->buf_draw_bbox_info )

	    .declareResource( self->buf_bin_header, self->buf_bin_header_info )
	    .declareResource( self->buf_path, self->buf_path_info )
	    .declareResource( self->buf_tile, self->buf_tile_info )
	    .declareResource( self->buf_indirect_count, self->buf_indirect_count_info )
	    .declareResource( self->buf_seg_counts, self->buf_seg_counts_info )
	    .declareResource( self->buf_ptcl, self->buf_ptcl_info )
	    .declareResource( self->buf_blend_spill, self->buf_blend_spill_info )

	    .declareResource( self->buf_segments, self->buf_segments_info )
	    .declareResource( self->buf_mask_lut, self->buf_mask_lut_info )

	    .declareResource( img_output, *img_output_info )
	    .declareResource( self->img_gradients, img_gradients_info )
	    .declareResource( self->img_image_atlas, img_image_atlas_info )

	    ;

	auto rp_xfer_gradients_cache =
	    le::RenderPass( "rp_xfer_gradients_cache", le::QueueFlagBits::eTransfer )
	        .setSetupCallback( self, []( le_renderpass_o* rp_, void* user_data ) -> bool {
		        le::RenderPass rp{ rp_ };
		        auto           ctx = ( le_2d_o* )( user_data );
		        if ( ( ctx->rasterizer_xfer_flags & ctx->transfer_gradient_cache_mask ) ) {
			        rp.useImageResource( ctx->img_gradients, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eTransferWrite );
			        ctx->rasterizer_xfer_flags &= ( ~ctx->transfer_gradient_cache_mask );
			        return true;
		        }
		        return false;
	        } )
	        .setExecuteCallback( self, []( le_command_buffer_encoder_o* e_, void* user_data ) {
		        auto                         app     = ( le_2d_o const* )( user_data );
		        auto                         encoder = le::TransferEncoder( e_ );

		        le_write_to_image_settings_t write_info =
		            le::WriteToImageSettingsBuilder()
		                .setImageW( N_GRADIENT_SAMPLES )
		                .setImageH( std::max<uint32_t>( 1, app->resource_cache.ramp_cache.data_int32.size() / N_GRADIENT_SAMPLES ) )
		                .build();

		        size_t          num_bytes = app->resource_cache.ramp_cache.data_int32.size() * sizeof( uint32_t );
		        uint32_t const* data      = app->resource_cache.ramp_cache.data_int32.data();

		        std::array<uint32_t, N_GRADIENT_SAMPLES> placeholder_data = {};

				// If there are no bytes to copy, we will copy at least the placeholder data
				// (it is guaranteed that the image is at least N_GRADIENT_SAMPLES wide)

				if ( num_bytes == 0 ) {
			        num_bytes = placeholder_data.size() * sizeof( uint32_t );
			        data      = placeholder_data.data();
		        }

		        encoder.writeToImage( app->img_gradients, write_info, data, num_bytes );

	        } );

	auto rp_xfer_lut =
	    le::RenderPass( "rp_xfer_lut", le::QueueFlagBits::eTransfer )
	        .setSetupCallback( self, []( le_renderpass_o* rp_, void* user_data ) -> bool {
		        le::RenderPass rp{ rp_ };
		        auto           ctx = ( le_2d_o* )( user_data );
		        if ( ctx->rasterizer_xfer_flags & ctx->transfer_lut_mask ) {
			        rp.useBufferResource( ctx->buf_mask_lut, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eTransferWrite );
			        // unset transfer lut mask
			        ctx->rasterizer_xfer_flags &= ( ~ctx->transfer_lut_mask );
			        return true;
		        }
		        return false;
	        } )
	        .setExecuteCallback( self, []( le_command_buffer_encoder_o* e_, void* user_data ) {
		        auto ctx     = ( le_2d_o const* )( user_data );
		        auto encoder = le::TransferEncoder( e_ );
		        encoder.writeToBuffer( ctx->buf_mask_lut, 0, ctx->mask_lut_bytes.data(), ctx->mask_lut_bytes.size() );
	        } );

	auto rp_xfer_scene =
	    le::RenderPass( "rp_xfer_2d_scene", le::QueueFlagBits::eTransfer )
	        .setSetupCallback( self, []( le_renderpass_o* rp_, void* user_data ) -> bool {
		        le::RenderPass rp{ rp_ };
		        auto           ctx = ( le_2d_o* )( user_data );
		        if ( ctx->rasterizer_xfer_flags & le_2d_o::transfer_scene_mask ) {
			        rp.useBufferResource( ctx->buf_vello_scene, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eTransferWrite );
			        ctx->rasterizer_xfer_flags &= ( ~ctx->transfer_scene_mask );
			        return true;
		        }
		        return false;
	        } )
	        .setExecuteCallback( self, []( le_command_buffer_encoder_o* e_, void* user_data ) {
		        auto ctx     = ( le_2d_o const* )( user_data );
		        auto encoder = le::TransferEncoder( e_ );
		        // Upload scene data to GPU buffer
		        // first null out scene buffer
		        encoder.fillBuffer( ctx->buf_vello_scene, 0, VK_WHOLE_SIZE, 0 );

		        encoder.bufferMemoryBarrier(
		            le::PipelineStageFlagBits2::eTransfer,
		            le::PipelineStageFlagBits2::eTransfer,
		            le::AccessFlagBits2::eTransferWrite,
		            le::AccessFlagBits2::eTransferWrite,
		            ctx->buf_vello_scene );

		        // TODO: do we really need this barrier?
		        //
		        // make sure that the fill operation was completed before we do the next operation
		        // this should not be necessary, as filling and writing the buffer should use the same
		        // memory caches and these therefore should not need to be flushed!

		        encoder.writeToBuffer( ctx->buf_vello_scene, 0, ctx->scene_bytes.data(), ctx->scene_bytes.size() );
	        } );

	auto rp_clear_images =
	    le::RenderPass( "img_clear_src_img", le::QueueFlagBits::eGraphics )
	        .setWidth( 1 )
	        .setHeight( 1 )
	        .addColorAttachment(
	            self->img_image_atlas,
	            le::ImageAttachmentInfoBuilder()
	                .setLoadOp( le::AttachmentLoadOp::eClear )
	                .build() ) //
	    ;

	auto rp_rasterize =
	    le::RenderPass( "rasterize_2d_scene", le::QueueFlagBits::eCompute );

	if ( self->should_use_msaa ) {
		rp_rasterize.useBufferResource( self->buf_mask_lut, le::AccessFlagBits2::eShaderRead, le::AccessFlagBits2::eNone );
	}

	rp_rasterize
	    .useBufferResource( self->buf_vello_scene, le::AccessFlagBits2::eShaderRead )
	    .useBufferResource( self->buf_reduced, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )

	    .useBufferResource( self->buf_reduced2, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )     // only when large path numbers
	    .useBufferResource( self->buf_reduced_scan, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite ) // only when large path numbers

	    .useBufferResource( self->buf_tagmonoid, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_path_bbox, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_bump, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_lines, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_draw_reduced, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_draw_monoid, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )

	    .useBufferResource( self->buf_info_bin_data, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )

	    .useBufferResource( self->buf_clip_inp, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_clip_bbox, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_clip_el, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_clip_bic, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )

	    .useBufferResource( self->buf_draw_bbox, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_bin_header, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_path, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_tile, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_indirect_count, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_seg_counts, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_ptcl, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_segments, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )
	    .useBufferResource( self->buf_blend_spill, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderWrite )

	    .useImageResource( self->img_output, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eShaderStorageWrite )
	    .useImageResource( self->img_image_atlas, le::AccessFlagBits2::eShaderSampledRead, le::AccessFlagBits2::eNone )
	    .useImageResource( self->img_gradients, le::AccessFlagBits2::eShaderSampledRead, le::AccessFlagBits2::eNone )

	    .setExecuteCallback( self, []( le_command_buffer_encoder_o* e_, void* user_data ) {
		    auto ctx = ( le_2d_o* )user_data;

		    auto const& wg = ctx->wg_counts;

		    /*
		     * witdh and height and number of tiles is dependent on the dimensions of the target framebuffer.
		     *
		     * data in layout depends on the scene
		     *
		     * then we have the number of allowed allocations based on how much data we allocated
		     *
		     */

		    auto encoder = le::ComputeEncoder( e_ );

		    static auto pm = encoder.getPipelineManager();

		    { // Zero out any buffers that need to be reset
			    assert( ctx->buf_bump_info.buffer.size % 4 == 0 && "bump buffer size must be multiple of 4" );

			    auto zero_out_buffer = [ &encoder ]( le_buffer_resource_handle buf ) {
				    encoder.fillBuffer( buf, 0, VK_WHOLE_SIZE, 0 );

				    encoder.bufferMemoryBarrier(
				        le::PipelineStageFlagBits2::eTransfer,
				        le::PipelineStageFlagBits2::eComputeShader,
				        le::AccessFlagBits2::eTransferWrite,
				        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
				        buf );
			    };

			    encoder.fillBuffer( ctx->buf_tagmonoid, 0, VK_WHOLE_SIZE, 0 );

			    // If we don't zero out this buffer, we may end with NAN's in
			    // segments, because leftover path data may result in zero-length
			    // paths getting processed.
			    zero_out_buffer( ctx->buf_path );

			    zero_out_buffer( ctx->buf_bump );
			    zero_out_buffer( ctx->buf_lines );
			    zero_out_buffer( ctx->buf_clip_bbox );

			    // zero out clip buffers --
			    // zero_out_buffer( ctx->buf_clip_bbox );
			    // zero_out_buffer( ctx->buf_clip_bic );
			    // zero_out_buffer( ctx->buf_clip_el );
			    // zero_out_buffer( ctx->buf_clip_inp );
		    }

		    {

			    encoder.bufferMemoryBarrier(
			        le::PipelineStageFlagBits2::eTransfer,
			        le::PipelineStageFlagBits2::eComputeShader,
			        le::AccessFlagBits2::eTransferWrite,
			        le::AccessFlagBits2::eShaderRead,
			        ctx->buf_vello_scene );

			    static auto pso_pathtag_reduce =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, pathtag_reduce_compressed_data_base85, "pathtag_reduce" );

			    encoder.bindComputePipeline( pso_pathtag_reduce )
			        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_vello_scene, 0 )
			        .bindArgumentBufferExplicit( 0, 2, ctx->buf_reduced, 0 )
			        .dispatch( wg.path_reduce[ 0 ], wg.path_reduce[ 1 ], wg.path_reduce[ 2 ] );
			    //
		    }

		    // ----------

		    if ( wg.use_large_path_scan ) {
			    // dispatch reduce2

			    {

				    static auto pso_pathtag_reduce2 =
				        create_cpso_from_compressed_and_encoded_spirv_code( pm, pathtag_reduce2_compressed_data_base85, "pathtag_reduce2" );

				    encoder.bindComputePipeline( pso_pathtag_reduce2 )
				        .bindArgumentBufferExplicit( 0, 0, ctx->buf_reduced, 0 )  // r
				        .bindArgumentBufferExplicit( 0, 1, ctx->buf_reduced2, 0 ) // rw
				        .dispatch( wg.path_reduce2[ 0 ], wg.path_reduce2[ 1 ], wg.path_reduce2[ 2 ] );
				    //
			    }
			    {

				    encoder.bufferMemoryBarrier(
				        le::PipelineStageFlagBits2::eComputeShader,
				        le::PipelineStageFlagBits2::eComputeShader,
				        le::AccessFlagBits2::eShaderWrite,
				        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
				        ctx->buf_reduced2 );

				    static auto pso_pathtag_scan1 =
				        create_cpso_from_compressed_and_encoded_spirv_code( pm, pathtag_scan1_compressed_data_base85, "pathtag_scan1" );

				    encoder.bindComputePipeline( pso_pathtag_scan1 )
				        .bindArgumentBufferExplicit( 0, 0, ctx->buf_reduced, 0 )      // r
				        .bindArgumentBufferExplicit( 0, 1, ctx->buf_reduced2, 0 )     // r
				        .bindArgumentBufferExplicit( 0, 2, ctx->buf_reduced_scan, 0 ) // rw
				        .dispatch( wg.path_scan1[ 0 ], wg.path_scan1[ 1 ], wg.path_scan1[ 2 ] );
				    //
			    }
		    }

		    // ---------

		    {

			    le_buffer_resource_handle reduced_buf = ctx->buf_reduced;

			    if ( wg.use_large_path_scan ) {
				    reduced_buf = ctx->buf_reduced_scan;
			    }

			    encoder.bufferMemoryBarrier(
			        le::PipelineStageFlagBits2::eComputeShader,
			        le::PipelineStageFlagBits2::eComputeShader,
			        le::AccessFlagBits2::eShaderWrite,
			        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
			        reduced_buf );

			    encoder.bufferMemoryBarrier(
			        le::PipelineStageFlagBits2::eTransfer,
			        le::PipelineStageFlagBits2::eComputeShader,
			        le::AccessFlagBits2::eTransferWrite,
			        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
			        ctx->buf_tagmonoid );

			    static auto pso_pathtag_scan_large =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, pathtag_scan_large_compressed_data_base85, "pathtag_scan_large" );
			    static auto pso_pathtag_scan_small =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, pathtag_scan_small_compressed_data_base85, "pathtag_scan_small" );

			    encoder.bindComputePipeline( wg.use_large_path_scan ? pso_pathtag_scan_large : pso_pathtag_scan_small )
			        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_vello_scene, 0 ) // r
			        .bindArgumentBufferExplicit( 0, 2, reduced_buf, 0 )          // r
			        .bindArgumentBufferExplicit( 0, 3, ctx->buf_tagmonoid, 0 )   // w
			        .dispatch( wg.path_scan[ 0 ], wg.path_scan[ 1 ], wg.path_scan[ 2 ] );
		    }

		    // -----------

		    {
			    static auto pso_path_bbox_clear =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, bbox_clear_compressed_data_base85, "bbox_clear" );

			    encoder.bindComputePipeline( pso_path_bbox_clear )
			        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_path_bbox ) // w
			        .dispatch( wg.bbox_clear[ 0 ], wg.bbox_clear[ 1 ], wg.bbox_clear[ 2 ] );
		    }

		    {

			    encoder.bufferMemoryBarrier(
			        le::PipelineStageFlagBits2::eComputeShader,
			        le::PipelineStageFlagBits2::eComputeShader,
			        le::AccessFlagBits2::eShaderWrite,
			        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
			        ctx->buf_tagmonoid );
			    encoder.bufferMemoryBarrier(
			        le::PipelineStageFlagBits2::eComputeShader,
			        le::PipelineStageFlagBits2::eComputeShader,
			        le::AccessFlagBits2::eShaderWrite,
			        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
			        ctx->buf_path_bbox );

			    static auto pso_flatten =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, flatten_compressed_data_base85, "flatten" );

			    encoder.bindComputePipeline( pso_flatten )
			        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_vello_scene ) // readonly
			        .bindArgumentBufferExplicit( 0, 2, ctx->buf_tagmonoid )   // readonly
			        .bindArgumentBufferExplicit( 0, 3, ctx->buf_path_bbox )   // rw
			        .bindArgumentBufferExplicit( 0, 4, ctx->buf_bump )        // rw
			        .bindArgumentBufferExplicit( 0, 5, ctx->buf_lines )       // w
			        .dispatch( wg.flatten[ 0 ], wg.flatten[ 1 ], wg.flatten[ 2 ] );
		    }
		    //
		    {
			    static auto pso_draw_reduce =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, draw_reduce_compressed_data_base85, "draw_reduce" );

			    encoder.bindComputePipeline( pso_draw_reduce )
			        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_vello_scene, 0 ) // readonly
			        .bindArgumentBufferExplicit( 0, 2, ctx->buf_draw_reduced )   // w
			        .dispatch( wg.draw_reduce[ 0 ], wg.draw_reduce[ 1 ], wg.draw_reduce[ 2 ] );
		    }

		    // make sure that buf_path_bbox is available
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_path_bbox );

		    // make sure that buf_reduced is available
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_reduced );

		    {
			    static auto pso_draw_leaf =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, draw_leaf_compressed_data_base85, "draw_leaf" );

			    encoder.bindComputePipeline( pso_draw_leaf )
			        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_vello_scene, 0 )   // r
			        .bindArgumentBufferExplicit( 0, 2, ctx->buf_draw_reduced )     // r
			        .bindArgumentBufferExplicit( 0, 3, ctx->buf_path_bbox, 0 )     // r
			        .bindArgumentBufferExplicit( 0, 4, ctx->buf_draw_monoid, 0 )   // w
			        .bindArgumentBufferExplicit( 0, 5, ctx->buf_info_bin_data, 0 ) // w
			        .bindArgumentBufferExplicit( 0, 6, ctx->buf_clip_inp, 0 )      // w
			        .dispatch( wg.draw_leaf[ 0 ], wg.draw_leaf[ 1 ], wg.draw_leaf[ 2 ] );
		    }

		    // make sure that buf_clip_inp is available
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead,
		        ctx->buf_clip_inp );

		    if ( wg.clip_reduce[ 0 ] > 0 ) {
			    // clip_reduce
			    static auto pso_clip_reduce =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, clip_reduce_compressed_data_base85, "clip_reduce" );

			    encoder.bindComputePipeline( pso_clip_reduce )
			        .bindArgumentBufferExplicit( 0, 0, ctx->buf_clip_inp, 0 )  // r
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_path_bbox, 0 ) // r
			        .bindArgumentBufferExplicit( 0, 2, ctx->buf_clip_bic, 0 )  // rw
			        .bindArgumentBufferExplicit( 0, 3, ctx->buf_clip_el, 0 )   // rw
			        .dispatch( wg.clip_reduce[ 0 ], wg.clip_reduce[ 1 ], wg.clip_reduce[ 2 ] );

			    encoder.bufferMemoryBarrier(
			        le::PipelineStageFlagBits2::eComputeShader,
			        le::PipelineStageFlagBits2::eComputeShader,
			        le::AccessFlagBits2::eShaderWrite,
			        le::AccessFlagBits2::eShaderRead,
			        ctx->buf_clip_bic );

			    encoder.bufferMemoryBarrier(
			        le::PipelineStageFlagBits2::eComputeShader,
			        le::PipelineStageFlagBits2::eComputeShader,
			        le::AccessFlagBits2::eShaderWrite,
			        le::AccessFlagBits2::eShaderRead,
			        ctx->buf_clip_el );
		    }

		    if ( wg.clip_leaf[ 0 ] > 0 ) {
			    // clip_leaf
			    static auto pso_clip_leaf =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, clip_leaf_compressed_data_base85, "clip_leaf" );

			    encoder.bindComputePipeline( pso_clip_leaf )
			        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_clip_inp, 0 )    // r
			        .bindArgumentBufferExplicit( 0, 2, ctx->buf_path_bbox, 0 )   // r
			        .bindArgumentBufferExplicit( 0, 3, ctx->buf_clip_bic, 0 )    // r
			        .bindArgumentBufferExplicit( 0, 4, ctx->buf_clip_el, 0 )     // r
			        .bindArgumentBufferExplicit( 0, 5, ctx->buf_draw_monoid, 0 ) // rw
			        .bindArgumentBufferExplicit( 0, 6, ctx->buf_clip_bbox, 0 )   // rw

			        .dispatch( wg.clip_leaf[ 0 ], wg.clip_leaf[ 1 ], wg.clip_leaf[ 2 ] );
		    }

		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_bump );
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_info_bin_data );

		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_draw_monoid );
		    {
			    static auto pso_binning =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, binning_compressed_data_base85, "binning" );

			    encoder.bindComputePipeline( pso_binning )
			        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_draw_monoid, 0 )   // w
			        .bindArgumentBufferExplicit( 0, 2, ctx->buf_path_bbox, 0 )     // r
			        .bindArgumentBufferExplicit( 0, 3, ctx->buf_clip_bbox, 0 )     // r
			        .bindArgumentBufferExplicit( 0, 4, ctx->buf_draw_bbox, 0 )     // r
			        .bindArgumentBufferExplicit( 0, 5, ctx->buf_bump )             // rw
			        .bindArgumentBufferExplicit( 0, 6, ctx->buf_info_bin_data, 0 ) // w
			        .bindArgumentBufferExplicit( 0, 7, ctx->buf_bin_header, 0 )    // w
			        .dispatch( wg.binning[ 0 ], wg.binning[ 1 ], wg.binning[ 2 ] );
		    }
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_bump );

		    // last possible time to wait on buf path to be cleared.
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eTransfer,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eTransferWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_path );

		    {

			    static auto pso_tile_alloc =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, tile_alloc_compressed_data_base85, "tile_alloc" );

			    encoder.bindComputePipeline( pso_tile_alloc )
			        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_vello_scene, 0 ) // r
			        .bindArgumentBufferExplicit( 0, 2, ctx->buf_draw_bbox, 0 )   // r
			        .bindArgumentBufferExplicit( 0, 3, ctx->buf_bump )           // rw
			        .bindArgumentBufferExplicit( 0, 4, ctx->buf_path, 0 )        // w
			        .bindArgumentBufferExplicit( 0, 5, ctx->buf_tile, 0 )        // w
			        .dispatch( wg.tile_alloc[ 0 ], wg.tile_alloc[ 1 ], wg.tile_alloc[ 2 ] );
		    }

		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_bump );

		    {

			    static auto pso_path_count_setup =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, path_count_setup_compressed_data_base85, "path_count_setup" );

			    encoder.bindComputePipeline( pso_path_count_setup )
			        .bindArgumentBufferExplicit( 0, 0, ctx->buf_bump )              // rw
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_indirect_count, 0 ) // w
			        .dispatch( wg.path_count_setup[ 0 ], wg.path_count_setup[ 1 ], wg.path_count_setup[ 2 ] );
		    }

		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eDrawIndirect,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eIndirectCommandRead,
		        ctx->buf_indirect_count,
		        0 );

		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_bump );
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_lines );
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_path );
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_tile );
		    {

			    static auto pso_path_count =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, path_count_compressed_data_base85, "path_count" );

			    encoder.bindComputePipeline( pso_path_count )
			        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_bump )       // rw
			        .bindArgumentBufferExplicit( 0, 2, ctx->buf_lines )      // r
			        .bindArgumentBufferExplicit( 0, 3, ctx->buf_path )       // r
			        .bindArgumentBufferExplicit( 0, 4, ctx->buf_tile )       // rw
			        .bindArgumentBufferExplicit( 0, 5, ctx->buf_seg_counts ) // rw
			        .dispatchIndirect( ctx->buf_indirect_count );            // r
		    }
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_tile,
		        0 );
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_bump,
		        0 );
		    {

			    static auto pso_backdrop_dyn =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, backdrop_dyn_compressed_data_base85, "backdrop_dyn" );

			    encoder.bindComputePipeline( pso_backdrop_dyn )
			        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_bump ) // rw
			        .bindArgumentBufferExplicit( 0, 2, ctx->buf_path ) // r
			        .bindArgumentBufferExplicit( 0, 3, ctx->buf_tile ) // rw
			        .dispatch( wg.backdrop[ 0 ], wg.backdrop[ 1 ], wg.backdrop[ 2 ] );
		    }

		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_bump,
		        0 );

		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_tile,
		        0 );
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_info_bin_data,
		        0 );
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_draw_monoid );

		    {

			    static auto pso_coarse =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, coarse_compressed_data_base85, "coarse" );

			    encoder.bindComputePipeline( pso_coarse )
			        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_vello_scene )   // r
			        .bindArgumentBufferExplicit( 0, 2, ctx->buf_draw_monoid )   // r
			        .bindArgumentBufferExplicit( 0, 3, ctx->buf_bin_header )    // r
			        .bindArgumentBufferExplicit( 0, 4, ctx->buf_info_bin_data ) // r
			        .bindArgumentBufferExplicit( 0, 5, ctx->buf_path )          // r
			        .bindArgumentBufferExplicit( 0, 6, ctx->buf_tile )          // rw
			        .bindArgumentBufferExplicit( 0, 7, ctx->buf_bump )          // rw
			        .bindArgumentBufferExplicit( 0, 8, ctx->buf_ptcl )          // rw
			        .dispatch( wg.coarse[ 0 ], wg.coarse[ 1 ], wg.coarse[ 2 ] );
		    }

		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_bump,
		        0 );
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_ptcl,
		        0 );

		    // protect indirect buffer from write while it is still being used for dispatch
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eDrawIndirect,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eIndirectCommandRead,
		        le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_indirect_count,
		        0 );
		    {

			    static auto pso_path_tiling_setup =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, path_tiling_setup_compressed_data_base85, "path_tiling_setup" );

			    encoder.bindComputePipeline( pso_path_tiling_setup )
			        .bindArgumentBufferExplicit( 0, 0, ctx->buf_bump )              // rw
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_indirect_count, 0 ) // w
			        .bindArgumentBufferExplicit( 0, 2, ctx->buf_ptcl )              // rw
			        .dispatch( wg.path_tiling_setup[ 0 ], wg.path_tiling_setup[ 1 ], wg.path_tiling_setup[ 2 ] );
		    }

		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eDrawIndirect,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eIndirectCommandRead,
		        ctx->buf_indirect_count,
		        0 );
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_tile,
		        0 );
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_bump,
		        0 );
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_seg_counts,
		        0 );

		    {

			    static auto pso_path_tiling =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, path_tiling_compressed_data_base85, "path_tiling" );

			    encoder.bindComputePipeline( pso_path_tiling )
			        .bindArgumentBufferExplicit( 0, 0, ctx->buf_bump )          // rw
			        .bindArgumentBufferExplicit( 0, 1, ctx->buf_seg_counts, 0 ) // r
			        .bindArgumentBufferExplicit( 0, 2, ctx->buf_lines )         // r
			        .bindArgumentBufferExplicit( 0, 3, ctx->buf_path )          // r
			        .bindArgumentBufferExplicit( 0, 4, ctx->buf_tile )          // r
			        .bindArgumentBufferExplicit( 0, 5, ctx->buf_segments )      // rw
			        .dispatchIndirect( ctx->buf_indirect_count );               // r
		    }
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_segments,
		        0 );
		    encoder.bufferMemoryBarrier(
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::PipelineStageFlagBits2::eComputeShader,
		        le::AccessFlagBits2::eShaderWrite,
		        le::AccessFlagBits2::eShaderRead | le::AccessFlagBits2::eShaderWrite,
		        ctx->buf_ptcl,
		        0 );

		    {

			    static auto pso_fine_msaa_16 =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, fine_msaa16_compressed_data_base85, "fine_msaa16" );

			    static auto pso_fine_area =
			        create_cpso_from_compressed_and_encoded_spirv_code( pm, fine_area_compressed_data_base85, "fine_area" );

			    if ( ctx->should_use_msaa ) {
				    encoder.bindComputePipeline( pso_fine_msaa_16 )
				        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
				        .bindArgumentBufferExplicit( 0, 1, ctx->buf_segments )      // rw
				        .bindArgumentBufferExplicit( 0, 2, ctx->buf_ptcl )          // rw
				        .bindArgumentBufferExplicit( 0, 3, ctx->buf_info_bin_data ) // r
				        .bindArgumentBufferExplicit( 0, 4, ctx->buf_blend_spill )   // r
				        .setArgumentImageExplicit( 0, 5, ctx->img_output, 0 )       // w
				        .setArgumentImageExplicit( 0, 6, ctx->img_gradients, 0 )    // r
				        .setArgumentImageExplicit( 0, 7, ctx->img_image_atlas, 0 )  // r
				        .bindArgumentBufferExplicit( 0, 8, ctx->buf_mask_lut )      // r
				        .dispatch( wg.fine[ 0 ], wg.fine[ 1 ], wg.fine[ 2 ] );
			    } else {
				    encoder.bindComputePipeline( pso_fine_area )
				        .setArgumentDataExplicit( 0, 0, &ctx->rasterizer_args, sizeof( ctx->rasterizer_args ) )
				        .bindArgumentBufferExplicit( 0, 1, ctx->buf_segments )      // rw
				        .bindArgumentBufferExplicit( 0, 2, ctx->buf_ptcl )          // rw
				        .bindArgumentBufferExplicit( 0, 3, ctx->buf_info_bin_data ) // r
				        .bindArgumentBufferExplicit( 0, 4, ctx->buf_blend_spill )   // r
				        .setArgumentImageExplicit( 0, 5, ctx->img_output, 0 )       // w
				        .setArgumentImageExplicit( 0, 6, ctx->img_gradients, 0 )    // r
				        .setArgumentImageExplicit( 0, 7, ctx->img_image_atlas, 0 )  // r
				        .dispatch( wg.fine[ 0 ], wg.fine[ 1 ], wg.fine[ 2 ] );
			    }
		    }
	    } );

	/*
	 * Copy the contents of bump into bump_cpu -- the latter is a cpu visible and coherent buffer.
	 * we do this so that we can see if there was an issue with allocation - if there was an issue
	 * this gives le_2d the opportunity to allocate greater amounts of memory for the bump
	 * allocator.
	 *
	 * If there was an issue with the bump allocator running out of memory, it will have set the
	 * indirect draw count to x==0, which means that nothing gets drawn -- or the last frame just
	 * gets drawn again.
	 *
	 */
	auto rp_xfer_bump =
	    le::RenderPass( "copy_bump_buf", le::QueueFlagBits::eTransfer )
	        .useBufferResource( self->buf_bump, le::AccessFlagBits2::eTransferRead )
	        .useBufferResource( self->buf_bump_cpu, le::AccessFlagBits2::eNone, le::AccessFlagBits2::eTransferWrite )
	        .setExecuteCallback( self, []( le_command_buffer_encoder_o* encoder_, void* user_data ) {
		        auto ctx = ( le_2d_o* )user_data;

		        /*
		         * struct BumpAllocators {
		         * 	failed: atomic<u32>, 	// Bitmask of stages that have failed allocation.
		         * 	binning: atomic<u32>,
		         * 	ptcl: atomic<u32>,
		         * 	tile: atomic<u32>,
		         * 	seg_counts: atomic<u32>,
		         * 	segments: atomic<u32>,
		         * 	blend: atomic<u32>,
		         * 	lines: atomic<u32>,
		         * }
		         */

		        auto encoder = le::TransferEncoder( encoder_ );

		        // Copy to the readback buffer.
		        // Note that the readback buffer is shared for all draw commands,
		        // but is partitioned into N_BYTES_READBACK_OFFSET partitions, so that
		        // a frame will only read and write into its own partition of the buffer.
		        //

		        encoder.bufferMemoryBarrier( le::PipelineStageFlagBits2::eComputeShader,
		                                     le::PipelineStageFlagBits2::eCopy,
		                                     le::AccessFlagBits2::eShaderStorageWrite,
		                                     le::AccessFlagBits2::eTransferRead,
		                                     ctx->buf_bump, 0, ctx->buf_bump_info.buffer.size );

		        // this should make sure that any writes to buf_bump have been flushed before we copy over to
		        // the cpu visible buffer.

		        encoder.copyToBuffer( ctx->buf_bump_cpu, N_BYTES_READBACK_OFFSET * ctx->current_data_frame_idx, ctx->buf_bump, 0, ctx->buf_bump_info.buffer.size );

		        ctx->current_data_frame_idx = ( ctx->current_data_frame_idx + 1 ) % ctx->num_data_frames;
	        } )
	        .setIsRoot( true );

	renderGraph
	    .addRenderPass( rp_xfer_gradients_cache )
	    .addRenderPass( rp_xfer_lut )
	    .addRenderPass( rp_xfer_scene )
	    .addRenderPass( rp_clear_images )
	    .addRenderPass( rp_rasterize )
	    .addRenderPass( rp_xfer_bump );

	auto t = &le_2d_api_i->le_2d_backend_callback_i.on_backend_frame_clear_cb;

	le_on_frame_clear_callback_data_t cb_data{
	    .cb_fun    = le_2d_api_i->le_2d_backend_callback_i.on_backend_frame_clear_cb,
	    .user_data = &self->on_clear_callback_data[ self->current_data_frame_idx ],
	};

	// increase the number of owners to this objects just before we add a callback
	// -- the callback will remove itself upon completion.
	self->intrusive_pointer_count++;

	// So that we may collect the data that was written into our readback buffer,
	// we must add a callback to when the backend frame has cycled through rendering
	// and is about to be cleared. At this point the frame will have crossed its
	// frame fence and this means that we are guaranteed that the gpu->cpu write
	// operation (our readback) has completed.
	le_renderer_api_i->le_rendergraph_i.add_on_frame_clear_callbacks( renderGraph, &cb_data, 1 );
}

// ----------------------------------------------------------------------

static void le_2d_set_should_use_msaa( le_2d_o* self, bool should_use_msaa ) {
	self->should_use_msaa = should_use_msaa;
}

// ----------------------------------------------------------------------
#include "private/le_2d/inl/stb_decompress.inl"
// ----------------------------------------------------------------------

extern void register_le_2d_encoder_api( void* api_ );

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_2d, api ) {
	auto& le_2d_i = static_cast<le_2d_api*>( api )->le_2d_i;

	le_2d_i.create  = le_2d_create;
	le_2d_i.destroy = le_2d_destroy;
	le_2d_i.update  = le_2d_update;
	le_2d_i.set_should_use_msaa = le_2d_set_should_use_msaa;

	auto& le_2d_backend_callback_i                     = static_cast<le_2d_api*>( api )->le_2d_backend_callback_i;

	le_2d_backend_callback_i.on_backend_frame_clear_cb = on_backend_frame_clear_callback;

	register_le_2d_encoder_api( api );
}
