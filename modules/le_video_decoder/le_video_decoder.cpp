#include "le_video_decoder.h"
#include "le_core.h"
#include "le_log.h"
#include "util/volk/volk.h"

#include "le_backend_vk.h"
#include "le_renderer.h"
#include "le_renderer.hpp"
#include "util/vk_mem_alloc/vk_mem_alloc.h"

#include <vector>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <atomic>
#include <algorithm> // for std::sort

#include <le_timebase.h>
#include <chrono>
#include <private/le_timebase/le_timebase_ticks_type.h>

#include <assert.h>

// If in doubt about anything related to h.264, refer to the
// h.264 standard document as seen at: <https://www.itu.int/rec/T-REC-H.264-202108-I/>

#define H264_IMPLEMENTATION
#include "3rdparty/h264/h264.h"

#define MINIMP4_IMPLEMENTATION
#include "3rdparty/minimp4/minimp4.h"

#ifdef PLUGINS_DYNAMIC
// ----------------------------------------------------------------------
// We must include volk here, so that it can load vulkan entry points for
// us in case we are running this module as a dynamic library.
// If we are running as a unified release executable, the backend will
// load all vulkan function pointers and they will therefore be available
// here anyway.
#	define VOLK_IMPLEMENTATION
#endif
#include "util/volk/volk.h"

static constexpr char const* vk_err_to_c_str( const VkResult& tp );                                                                                  // ffdecl
static void                  post_reload_hook( le_backend_o* backend );                                                                              // ffdecl
static std::vector<uint8_t>  load_file( const std::filesystem::path& file_path, bool* success );                                                     // ffdecl
static int                   demux_h264_data( std::ifstream& input_file, size_t input_size, struct le_video_data_h264_t* video, MP4D_demux_t* mp4 ); // ffdecl
// ----------------------------------------------------------------------

static auto LOGGER_LABEL = "le_video_decoder";
static auto logger       = []() -> auto{
	      static auto logger = LeLog( LOGGER_LABEL );
		  return logger;
}
();

static constexpr bool SHOULD_PRINT_LOG_MESSAGES = false;

#define ARRAY_SIZE( x ) \
	( sizeof( x ) / sizeof( x[ 0 ] ) )

// ----------------------------------------------------------------------

static constexpr size_t align_to( size_t sz, size_t alignment ) {
	return ( ( sz - 1 ) / alignment + 1 ) * alignment;
};
// unit tests for align_to
static_assert( align_to( 3, 4 ) == 4, "must be 4" );
static_assert( align_to( 0, 4 ) == 0, "must be 0" );
static_assert( align_to( 7, 4 ) == 8, "must be 8" );
static_assert( align_to( 8, 4 ) == 8, "must be 8" );
static_assert( align_to( 9, 4 ) == 12, "must be 12" );
static_assert( align_to( 9, 3 ) == 9, "must be 9" );

static bool should_use_queries() {
	LE_SETTING( const bool, LE_SETTING_SHOULD_USE_VIDEO_STATUS_QUERIES, true );
	return *LE_SETTING_SHOULD_USE_VIDEO_STATUS_QUERIES;
}

#define USE_YCBCR_SAMPLER

// ----------------------------------------------------------------------

struct le_video_gpu_bitstream_buffer_t {
	VmaAllocationInfo allocation_info;
	VmaAllocation     allocation;
	VkBuffer          buffer;
};

enum class FrameType : uint8_t {
	eFrameTypeUnknown = 0,
	eFrameTypeIntra,
	eFrameTypePredictive,
};

struct pic_order_count_state_t {
	int pic_order_cnt_lsb = 0;
	int pic_order_cnt_msb = 0;
	int poc_cycle         = -1;
	int frame_num         = 0;
	int frame_offset      = 0;
};

struct frame_info_t {
	FrameType frame_type             = FrameType::eFrameTypeUnknown;
	uint8_t   nal_unit_type          = 0; // network abstraction layer unit type
	uint32_t  nal_ref_idc            = 0; // network abstraction layer reference
	int       poc                    = 0; // picture order count (==TopFieldOrderCount)
	int       bottom_field_order_cnt = 0;
	int       top_field_order_cnt    = 0;
	int       gop                    = 0; // group of pictures
	int       display_order          = 0;

	size_t   pts_in_timescale_units;      // presentation time stamp, in video timescale units
	uint32_t duration_in_timescale_units; // in video timescale units

	h264::SliceHeader slice_header;
};

struct le_video_data_h264_t {
	uint32_t             video_track_id = 0; // index of video track in mp4_demux->tracks
	uint64_t             num_frames     = 0;
	std::string          title;
	std::string          album;
	std::string          artist;
	std::string          year;
	std::string          comment;
	std::string          genre;
	uint32_t             padded_width  = 0;
	uint32_t             padded_height = 0;
	uint32_t             width         = 0;
	uint32_t             height        = 0;
	uint32_t             bit_rate      = 0;
	std::vector<uint8_t> sps_bytes;
	std::vector<uint8_t> pps_bytes;
	uint32_t             sps_count = 0;
	uint32_t             pps_count = 0;
	enum class VideoProfile : uint32_t {
		VideoProfileUnknown = 0,
		VideoProfileAvc,  // h264
		VideoProfileHevc, // h265 (not implemented)
	};

	VideoProfile video_profile = VideoProfile::VideoProfileUnknown;

	float     average_frames_per_second   = 0;
	float     duration_in_seconds         = 0;  // duration for the whole movie
	uint64_t  duration_in_timescale_units = 0;  // duration for the whole movie
	le::Ticks duration_in_ticks           = {}; // duration for the whole movie

	uint64_t timescale = 1; //< inverse scale factor for time. 1 second = (1 / one_over_timescale)

	struct data_frame_info_t {
		uint64_t  src_offset      = 0; // offset into original stream
		uint64_t  src_frame_bytes = 0; // number of bytes used by this frame in original stream
		uint64_t  size            = 0;
		le::Ticks timestamp_in_ticks;
		le::Ticks duration_in_ticks;

		frame_info_t info; // frame info (contains slice header)
	};
	uint64_t            max_memory_frame_size_bytes; // max required size to capture one frame of data - this needs to be aligned!
	std::vector<size_t> frame_display_order;
	uint32_t            num_dpb_slots          = 0;
	uint32_t            max_reference_pictures = 0;
	uint32_t            poc_interval           = 0; // distance between two neighbouring POC elements (determined via heuristic in create_video, used for PTS calculation)
};

struct le_video_decoder_o {
	std::atomic<size_t> reference_count; // intrusive pointer - once this is at zero, object will be destroyed.

	enum PlaybackState : uint32_t {
		eInitial = 0,
		ePause, // Pause also signals that the player is ready to play at any moment
		ePlay,
		eSeeking,
		eError,
	};

	PlaybackState playback_state          = PlaybackState::eInitial;
	bool          is_playback_not_looping = false; // if true, we will stop after playback. Seek to 0 (or any other position) to re-allow playback. We define this as a negative because the default it to loop.

	le::Ticks ticks_at_start       = {}; // start point for time calculations
	le::Ticks ticks_at_last_update = {}; // ticks at last update
	le::Ticks ticks_at_playhead    = {}; // current playhead position [0..duration_in_ticks[
	le::Ticks ticks_seek_offset    = {}; // current seek offset - usually 0

	uint32_t flags = 0;
	enum FlagBits : uint32_t {
		eInitialResetIssued = 1 << 0, // we must issue a reset operation on the very first decode
	};

	le_backend_o*     backend;                             // weak, non-owning
	VkDevice          device;                              // weak, non-owning
	le_device_o*      le_device;                           // weak, non-owning
	VkPhysicalDevice  physical_device;                     // weak, non-owning
	BackendQueueInfo* backend_default_graphics_queue_info; // weak, non-owning
	uint32_t          backend_video_decoder_queue_family_index = 0;

	// Video profile - initialised from file metadata, and linked up when creating video decoder
	// this is used when querying capabilities, and also when allocating memory for bitstream buffers
	struct settings_t {
		VkVideoDecodeH264ProfileInfoKHR decode_h264_profile_info;
		VkVideoProfileInfoKHR           profile_info;
		VkVideoProfileListInfoKHR       profile_list_info;
	} settings;

	struct decoder_query_result_t {
		// video capabilities chain - initialised, and linked up before querying
		VkVideoDecodeH264CapabilitiesKHR decode_h264_capabilities;
		VkVideoDecodeCapabilitiesKHR     decode_capabilities;
		VkVideoCapabilitiesKHR           capabilities;
		VkVideoFormatPropertiesKHR       format_properties = {}; ///< queried via video_format_properties
		VkImageUsageFlags                usage_flags_dpb_image;
		VkImageUsageFlags                usage_flags_out_image;

		bool do_dpb_and_out_images_coincide = false; // should be false by default
	} properties;                                    //< queried info. updated once on creation, const after that.

	// data describing the video, frames, ordering, offsets, anything that we can parse from the video file
	le_video_data_h264_t* video_data; // strong, owning

	VkQueryPool                 vk_query_pool               = nullptr;
	VkVideoSessionKHR           vk_video_session            = nullptr; // strong, owning
	VkVideoSessionParametersKHR vk_video_session_parameters = nullptr;

	std::vector<VmaAllocation> session_memory_allocations; // allocations made for the video session

	struct distinct_dst_image_info_t {
		// Separate dst images are only needed if the implementation does not support dst and dpb images to coincide.
		VkImage           dst_image;      // strong, owning
		VkImageView       dst_image_view; // strong, owning
		VmaAllocation     dst_image_allocation;
		VmaAllocationInfo dst_image_allocation_info;
	};

	struct video_decoder_memory_frame {
		// similar to our general backend, the video decoder has a concept of frame-local memory
		// anything that is touched by the callback in a non-read-only manner should only be interacted with via
		// the memory frame.
		// You place data in here during the record pass (renderer)
		// You read data from here during the execute pass (backend)
		// Data is freed when the frame comes around.
		uint32_t            id;      // memory frame index, query index
		le_video_decoder_o* decoder; // weak reference

		le_img_resource_handle rendergraph_image_resource; // The image resource belonging to the rendergraph into which we copy the decoded frame
		uint32_t               flags          = 0;
		le::Ticks              ticks_pts      = {}; ///< presentation time stamp, relative to video start time
		le::Ticks              ticks_duration = {};

		enum State : uint32_t {
			eIdle = 0,
			eRecording,
			eDecodeSuccess,
			eDecodeFailed,
		};

		State state = State::eIdle;

		enum FlagBits : uint32_t {
			eQueryIssued = 1 << 0,
		};

		distinct_dst_image_info_t* maybe_dst_image_info; // only used if dst and dpb images do not coincide.

		size_t   gpu_bitstream_offset;           ///< offset into the bitstream buffer to reach this slice
		size_t   gpu_bitstream_capacity;         ///< total bytes for this slice of the bitstream buffer
		size_t   gpu_bitstream_used_bytes_count; ///< used bytes for this slice
		uint8_t* gpu_bitstream_slice_mapped_memory_address = nullptr;
		size_t   decoded_frame_index; ///< index of this frame in the video stream

		frame_info_t frame_info;
	};

	std::vector<video_decoder_memory_frame> memory_frames;
	int32_t                                 memory_frame_idx_recording                  = -1; ///< index of memory frame that is currently recording (currently active in the execute callback)
	int32_t                                 latest_memory_frame_available_for_rendering = -1; // -1 means none.

	pic_order_count_state_t pic_order_count_state = {}; // current pic order count state FIXME: what happens to this when we seek, or rewind?

	struct dpb_image_array_t {
		VkImage           image;      // this is an array image
		VkImageView       image_view; // this is the image view associated with the above image.
		VmaAllocation     allocation;
		VmaAllocationInfo allocation_info;
	};

	struct dpb_state_t {
		int32_t                         slot_idx;  // could be -1 to signal that this slot is free
		uint16_t                        frame_num; // do we really need to store this?
		StdVideoDecodeH264ReferenceInfo reference_info;
	};

	std::vector<dpb_image_array_t> dpb_image_array; // "picture decode buffer image array", these are the reference pictures (and one decoded picture) for the video decoder are stored

	std::deque<dpb_state_t> dpb_state;
	uint32_t                dpb_target_slot_idx = 0; // current slot index into which to place the reconstructed picture

	std::ifstream mp4_filestream;
	MP4D_demux_t  mp4_demux;

	frame_info_t last_i_frame_info; // FIXME: this is decoder state, we should place this somewhere better, ideally

	// video bitstream buffer - this is what the gpu reads, and the cpu writes to
	le_video_gpu_bitstream_buffer_t gpu_bitstream_buffer{};
	size_t                          current_decoded_frame = 0; // the index of the frame being decoded, used to offset into bitstream buffer.

	void*                                                  on_playback_complete_callback_userdata = nullptr;
	le_video_decoder_api::on_video_playback_complete_fun_t on_playback_complete_callback          = nullptr;
};

using MemoryFrameFlagBits = le_video_decoder_o::video_decoder_memory_frame::FlagBits;
using MemoryFrameState    = le_video_decoder_o::video_decoder_memory_frame::State;

// ----------------------------------------------------------------------

static le::Ticks video_time_to_ticks( uint64_t video_time_units, uint64_t time_scale ) {
	uint64_t full_seconds = video_time_units / time_scale;
	double   tu_rest      = ( video_time_units - ( time_scale * full_seconds ) ) / double( time_scale );
	return ( std::chrono::seconds( full_seconds ) + std::chrono::round<le::Ticks>( std::chrono::duration<double>( tu_rest ) ) );
}

// ----------------------------------------------------------------------

static uint64_t video_time_to_ticks_count( uint64_t video_time_units, uint64_t time_scale ) {
	return video_time_to_ticks( video_time_units, time_scale ).count();
}

// ----------------------------------------------------------------------

static void le_video_decoder_init() {
	//	// adding this during initialisation means there is no way for the application
	//	// to start if it does not support the correct extension
	bool result = true;

	result &= le_backend_vk::settings_i.add_required_device_extension(
	    VK_KHR_VIDEO_QUEUE_EXTENSION_NAME );
	result &= le_backend_vk::settings_i.add_required_device_extension(
	    VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME );
	result &= le_backend_vk::settings_i.add_required_device_extension(
	    VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME );
	result &= le_backend_vk::settings_i.add_required_device_extension(
	    VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME );
	assert( result && "We must successfully require vk extensions for video" );

	VkQueueFlags queue_capablitities[] = {
	    VK_QUEUE_VIDEO_DECODE_BIT_KHR | VK_QUEUE_TRANSFER_BIT, // video queues must also support transfer, and we want a video decode queue
	};

	if ( false == le_backend_vk::settings_i.add_requested_queue_capabilities( queue_capablitities, ARRAY_SIZE( queue_capablitities ) ) ) {
		logger.error( "Could not request queue capabilities required for video decode." );
	}

	if ( false ) { // Request some vk11 features to be switched on.
		auto vk_features_chain = le_backend_vk::settings_i.get_physical_device_features_chain();

		struct GenericVkStruct {
			VkStructureType sType;
			void*           pNext;
		};

		GenericVkStruct* features_struct = reinterpret_cast<GenericVkStruct*>( vk_features_chain );

		while ( features_struct->pNext ) {

			if ( features_struct->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES ) {
				// we found the struct that contains settings for Vulkan1.1
				auto vk11_features = reinterpret_cast<VkPhysicalDeviceVulkan11Features*>( features_struct );
				// we require samplerycbrconversion to be enabled
				vk11_features->samplerYcbcrConversion = true;
				break;
			}

			features_struct = static_cast<GenericVkStruct*>( features_struct->pNext );
		}
	}
}

// ----------------------------------------------------------------------
static le_video_decoder_o* le_video_decoder_create( le_renderer_o* renderer, char const* file_path ) {
	auto self = new le_video_decoder_o();

	self->reference_count++;

	// we can grab the backend via the renderer - maybe this should be something
	// that the video player does, and not us...
	le_backend_o* backend = le_renderer_api_i->le_renderer_i.get_backend( renderer );

	if ( backend ) {
		post_reload_hook( backend );
		using namespace le_backend_vk;
		self->backend                             = backend;
		self->device                              = private_backend_vk_i.get_vk_device( backend );
		self->le_device                           = private_backend_vk_i.get_le_device( backend );
		self->physical_device                     = private_backend_vk_i.get_vk_physical_device( backend );
		self->backend_default_graphics_queue_info = private_backend_vk_i.get_default_graphics_queue_info( backend );
	} else {
		logger.error( "Fatal: Could not get hold of backend." );
		exit( -1 );
	}

	size_t num_memory_frames = 0;

	self->latest_memory_frame_available_for_rendering = -1; // signal that there are no frames available for rendering yet
	self->last_i_frame_info                           = {}; // internal state for copy_video_frame

	// ----------------------------------------------------------------------
	// Fill in templates for info structures that we will need to query device
	// capabilities.

	VkVideoProfileListInfoKHR video_profile_list_info = {
	    .sType        = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR,
	    .pNext        = nullptr,
	    .profileCount = 0,
	    .pProfiles    = nullptr,
	};

	self->properties.decode_h264_capabilities = {
	    .sType                  = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR,
	    .pNext                  = nullptr,
	    .maxLevelIdc            = {},
	    .fieldOffsetGranularity = {},
	};

	self->properties.decode_capabilities = {
	    .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR,
	    .pNext = &self->properties.decode_h264_capabilities,
	    .flags = 0,
	};

	self->properties.capabilities = {
	    .sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,
	    .pNext = &self->properties.decode_capabilities,
	};

	self->settings.decode_h264_profile_info = {
	    .sType         = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR,
	    .pNext         = nullptr,
	    .stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_BASELINE,
	    .pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_INTERLACED_INTERLEAVED_LINES_BIT_KHR,
	};
	self->settings.profile_info = {
	    .sType               = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
	    .pNext               = &self->settings.decode_h264_profile_info,
	    .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
	    .chromaSubsampling   = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR, // TODO: we should set these settings based on parsing the video file, perhaps
	    .lumaBitDepth        = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
	    .chromaBitDepth      = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
	};
	self->settings.profile_list_info = {
	    .sType        = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR,
	    .pNext        = nullptr, // optional
	    .profileCount = 1,       // optional
	    .pProfiles    = &self->settings.profile_info,
	};

	{
		auto result = vkGetPhysicalDeviceVideoCapabilitiesKHR( self->physical_device, &self->settings.profile_info, &self->properties.capabilities );

		if ( result != VK_SUCCESS ) {
			logger.error( "vulkan error: %s", vk_err_to_c_str( result ) );
			exit( -1 );
		}
	}

	{
		// query video format properties
		uint32_t format_properties_count = 0;

		// Create a copy of video profile info, so that we can change the
		// pNext to point to capabilities - we must name the decode capablities,
		// and the h264 decode capabilities

		VkPhysicalDeviceVideoFormatInfoKHR format_info = {
		    .sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR,
		    .pNext      = &self->settings.profile_list_info,
		    .imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_SRC_BIT_KHR |
		                  VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
		                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		                  VK_IMAGE_USAGE_SAMPLED_BIT |
		                  VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR,
		};

		VkResult                                result = VK_SUCCESS;
		std::vector<VkVideoFormatPropertiesKHR> video_format_properties;
		result = vkGetPhysicalDeviceVideoFormatPropertiesKHR( self->physical_device, &format_info, &format_properties_count, nullptr );
		assert( VK_SUCCESS == result );
		video_format_properties.resize( format_properties_count, { .sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR } );
		result = vkGetPhysicalDeviceVideoFormatPropertiesKHR( self->physical_device, &format_info, &format_properties_count, video_format_properties.data() );
		assert( VK_SUCCESS == result );

		assert( !video_format_properties.empty() );

		if ( video_format_properties.empty() ) {
			logger.error( "Could not query video format properties" );
			return nullptr; // todo: we should cleanup anything that was allocated until here.
		}

		// --------| invariant : we have at least one format that we can use.
		// use the first given format

		self->properties.format_properties = video_format_properties.front();
	}
	// ----------| invariant: we managed to successfully query capabilities.

	// * picture format
	// * reference picture format

	bool  success = false;
	void* pData   = nullptr;

	self->mp4_filestream =
	    std::ifstream( file_path, std::ios::in | std::ios::binary | std::ios::ate );

	if ( self->mp4_filestream.is_open() == false ) {
		logger.error( "Unable to open file: '%s'", std::filesystem::canonical( file_path ).c_str() );
	} else {

		self->mp4_demux = {};

		size_t mp4_filestream_size = self->mp4_filestream.tellg(); // get size for file
		self->mp4_filestream.seekg( 0, std::ios::beg );            // rewind filestream to start
		self->video_data = new le_video_data_h264_t{};
		demux_h264_data(
		    self->mp4_filestream,
		    mp4_filestream_size,
		    self->video_data,
		    &self->mp4_demux );

		// We now know the max number of bytes per bitstream data frame, now
		// we must make this number fit the alignment criteria

		size_t buffer_sz = align_to( self->video_data->max_memory_frame_size_bytes,
		                             self->properties.capabilities.minBitstreamBufferOffsetAlignment );
		buffer_sz        = align_to( buffer_sz,
		                             self->properties.capabilities.minBitstreamBufferSizeAlignment );

		self->video_data->max_memory_frame_size_bytes = buffer_sz;
	}

	{
		// we want to have one more memory frame than dpb slots
		num_memory_frames = self->video_data->num_dpb_slots + 1;
	}

	{

		//	Allocate memory (host-mapped) where we can
		//	store all the frame data before feeding it to the gpu.

		{
			// Find out the queue family index for the video decoder queue
			self->backend_video_decoder_queue_family_index =
			    le_backend_vk::private_backend_vk_i.find_queue_family_index_from_requirements( self->backend, VK_QUEUE_VIDEO_DECODE_BIT_KHR );
			if ( self->backend_video_decoder_queue_family_index == uint32_t( -1 ) ) {
				logger.error( "could not find queue family index for video queue" );
			}
		}

		// Allocate a buffer which we use to feed the frame data to the GPU.
		// internally, we split this buffer into num_dpb_slots even memory frames.
		// Each memory frame holds data for one video frame to be decoded.
		//
		// The buffer to hold all this data needs to be host-visible as we memory-
		// map it, and keep it mapped throughout, so that we can stream data to the
		// GPU.
		using namespace le_backend_vk;

		//

		size_t buffer_sz =
		    self->video_data->max_memory_frame_size_bytes *
		    num_memory_frames;

		VkBufferCreateInfo bufferCreateInfo{
		    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		    .pNext                 = &self->settings.profile_list_info,
		    .flags                 = 0, // optional
		    .size                  = buffer_sz,
		    .usage                 = VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR,
		    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		    .queueFamilyIndexCount = 0, // optional
		    .pQueueFamilyIndices   = 0,
		};

		VmaAllocationCreateInfo allocationCreateInfo{};
		allocationCreateInfo.flags         = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		allocationCreateInfo.usage         = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		allocationCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

		auto bufAllocationResult = VkResult(
		    private_backend_vk_i.allocate_buffer(
		        self->backend,
		        &bufferCreateInfo,
		        &allocationCreateInfo,
		        &self->gpu_bitstream_buffer.buffer,
		        &self->gpu_bitstream_buffer.allocation,
		        &self->gpu_bitstream_buffer.allocation_info ) );
		logger.info( "Allocated video Bitstream buffer: %d bytes.", self->gpu_bitstream_buffer.allocation_info.size );
		if ( bufAllocationResult != VK_SUCCESS ) {
			logger.error( "could not allocate memory for bitstream buffers" );
		}

		if ( VK_SUCCESS ==
		     VkResult(
		         private_backend_vk_i.map_gpu_memory(
		             self->backend,
		             self->gpu_bitstream_buffer.allocation,
		             &pData ) ) ) {

			// ----------| invariant: Mapped memory is now available at pData, we can write to it.
		} else {
			logger.error( "could not allocate memory for bitstream buffers" );
		}
	}

	{
		// We must create a video session
		if ( self->video_data->num_dpb_slots > self->properties.capabilities.maxDpbSlots ) {
			logger.error( "Number of requested dpb slots is %d, but device can only provide a maximum of %d",
			              self->video_data->num_dpb_slots,
			              self->properties.capabilities.maxDpbSlots );
		}

		// ---------| invariant: self->video_data->num_dpb_slots <= self->video_capabilities.maxDpbSlots

		// Spec: https://docs.vulkan.org/spec/latest/chapters/video_extensions.html#dpb
		self->video_data->max_reference_pictures =
		    std::min(
		        self->video_data->num_dpb_slots, // "each dpb slot can refer to up to two reference pictures"
		        self->properties.capabilities.maxActiveReferencePictures );

		VkVideoSessionCreateInfoKHR session_create_info = {
		    .sType            = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR,
		    .pNext            = nullptr,
		    .queueFamilyIndex = self->backend_video_decoder_queue_family_index,
		    .flags            = 0, // must be 0
		    .pVideoProfile    = &self->settings.profile_info,
		    .pictureFormat    = self->properties.format_properties.format, // queried earlier
		    .maxCodedExtent   = {
		          .width  = std::min( self->video_data->width, self->properties.capabilities.maxCodedExtent.width ),
		          .height = std::min( self->video_data->height, self->properties.capabilities.maxCodedExtent.height ),
            },
		    .referencePictureFormat     = self->properties.format_properties.format,
		    .maxDpbSlots                = self->video_data->num_dpb_slots,
		    .maxActiveReferencePictures = self->video_data->max_reference_pictures,
		    .pStdHeaderVersion          = &self->properties.capabilities.stdHeaderVersion,
		};

		VkResult res = vkCreateVideoSessionKHR( self->device, &session_create_info, nullptr, &self->vk_video_session );
		logger.info( "Created Video Session: %p", self->vk_video_session );
		assert( VK_SUCCESS == res );
	}

	{
		// query and allocate memory for video session

		VkResult res               = VK_SUCCESS;
		uint32_t requirement_count = 0;

		res = vkGetVideoSessionMemoryRequirementsKHR( self->device, self->vk_video_session, &requirement_count, nullptr );
		assert( res == VK_SUCCESS );
		std::vector<VkVideoSessionMemoryRequirementsKHR> requirements( requirement_count, { .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR } );
		res = vkGetVideoSessionMemoryRequirementsKHR( self->device, self->vk_video_session, &requirement_count, requirements.data() );
		assert( res == VK_SUCCESS );

		self->session_memory_allocations.resize( requirement_count );

		std::vector<VkBindVideoSessionMemoryInfoKHR> bind_session_memory_infos( requirement_count );

		for ( uint32_t i = 0; i < requirement_count; ++i ) {
			VkVideoSessionMemoryRequirementsKHR& video_req = requirements[ i ];
			VmaAllocationInfo                    alloc_info{};
			VmaAllocationCreateInfo              alloc_create_info = {};
			alloc_create_info.memoryTypeBits                       = video_req.memoryRequirements.memoryTypeBits;

			res = VkResult(
			    le_backend_vk::private_backend_vk_i.allocate_gpu_memory(
			        self->backend,
			        &alloc_create_info,
			        &video_req.memoryRequirements,
			        &self->session_memory_allocations[ i ],
			        &alloc_info ) );

			assert( res == VK_SUCCESS );

			VkBindVideoSessionMemoryInfoKHR& bind_info = bind_session_memory_infos[ i ];

			bind_info.sType           = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
			bind_info.memory          = alloc_info.deviceMemory;
			bind_info.memoryOffset    = alloc_info.offset;
			bind_info.memorySize      = alloc_info.size;
			bind_info.memoryBindIndex = video_req.memoryBindIndex;
		}
		res = vkBindVideoSessionMemoryKHR(
		    self->device,
		    self->vk_video_session,
		    requirement_count,
		    bind_session_memory_infos.data() );
		assert( res == VK_SUCCESS );
	}
	{

		std::vector<StdVideoH264PictureParameterSet> pps_array_h264( self->video_data->pps_count );
		std::vector<StdVideoH264ScalingLists>        pps_scaling_lists_array_h264( self->video_data->pps_count );

		for ( size_t i = 0; i != self->video_data->pps_count; i++ ) {

			// TRANSLATE ANY DATA WE GATHER FROM MP4 to VK

			const h264::PPS& pps = *( reinterpret_cast<const h264::PPS*>( self->video_data->pps_bytes.data() ) + i );

			auto& sl = pps_scaling_lists_array_h264[ i ];
			sl       = {};
			{
				decltype( sl.scaling_list_present_mask ) j;
				for ( j = 0; j != ARRAY_SIZE( pps.pic_scaling_list_present_flag ); j++ ) {
					sl.scaling_list_present_mask |=
					    static_cast<decltype( j )>( pps.pic_scaling_list_present_flag[ j ] ) << j;
				}
			}
			{
				decltype( sl.use_default_scaling_matrix_mask ) j;
				for ( j = 0; j != ARRAY_SIZE( pps.UseDefaultScalingMatrix4x4Flag ); j++ ) {
					sl.use_default_scaling_matrix_mask |=
					    static_cast<decltype( j )>( pps.UseDefaultScalingMatrix4x4Flag[ j ] ) << j;
				}
			}

			for ( size_t list_idx = 0;
			      list_idx < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_LISTS &&
			      list_idx < ARRAY_SIZE( pps.ScalingList4x4 );
			      list_idx++ ) {
				for ( size_t el_idx = 0;
				      el_idx < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_ELEMENTS &&
				      el_idx < ARRAY_SIZE( pps.ScalingList4x4[ 0 ] );
				      el_idx++ ) {
					sl.ScalingList4x4[ list_idx ][ el_idx ] = pps.ScalingList4x4[ list_idx ][ el_idx ];
				}
			}

			for ( size_t list_idx = 0;
			      list_idx < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_LISTS &&
			      list_idx < ARRAY_SIZE( pps.ScalingList8x8 );
			      list_idx++ ) {
				for ( size_t el_idx = 0;
				      el_idx < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_ELEMENTS &&
				      el_idx < ARRAY_SIZE( pps.ScalingList8x8[ 0 ] );
				      el_idx++ ) {
					sl.ScalingList8x8[ list_idx ][ el_idx ] = pps.ScalingList8x8[ list_idx ][ el_idx ];
				}
			}

			pps_array_h264[ i ] = {
			    .flags = {
			        .transform_8x8_mode_flag                      = uint32_t( pps.transform_8x8_mode_flag ),
			        .redundant_pic_cnt_present_flag               = uint32_t( pps.redundant_pic_cnt_present_flag ),
			        .constrained_intra_pred_flag                  = uint32_t( pps.constrained_intra_pred_flag ),
			        .deblocking_filter_control_present_flag       = uint32_t( pps.deblocking_filter_control_present_flag ),
			        .weighted_pred_flag                           = uint32_t( pps.weighted_pred_flag ),
			        .bottom_field_pic_order_in_frame_present_flag = uint32_t( pps.pic_order_present_flag ),
			        .entropy_coding_mode_flag                     = uint32_t( pps.entropy_coding_mode_flag ),
			        .pic_scaling_matrix_present_flag              = uint32_t( pps.pic_scaling_matrix_present_flag ),
			    },
			    .seq_parameter_set_id                 = uint8_t( pps.seq_parameter_set_id ),
			    .pic_parameter_set_id                 = uint8_t( pps.pic_parameter_set_id ),
			    .num_ref_idx_l0_default_active_minus1 = uint8_t( pps.num_ref_idx_l0_active_minus1 ),
			    .num_ref_idx_l1_default_active_minus1 = uint8_t( pps.num_ref_idx_l1_active_minus1 ),
			    .weighted_bipred_idc                  = StdVideoH264WeightedBipredIdc( pps.weighted_bipred_idc ),
			    .pic_init_qp_minus26                  = int8_t( pps.pic_init_qp_minus26 ),
			    .pic_init_qs_minus26                  = int8_t( pps.pic_init_qs_minus26 ),
			    .chroma_qp_index_offset               = int8_t( pps.chroma_qp_index_offset ),
			    .second_chroma_qp_index_offset        = int8_t( pps.second_chroma_qp_index_offset ),
			    .pScalingLists                        = &pps_scaling_lists_array_h264[ i ],
			};
		}

		std::vector<StdVideoH264SequenceParameterSet>    sps_array_h264( self->video_data->sps_count );
		std::vector<StdVideoH264SequenceParameterSetVui> sps_array_h264_vui( self->video_data->sps_count );
		std::vector<StdVideoH264ScalingLists>            sps_array_h264_scaling_lists( self->video_data->sps_count );
		std::vector<StdVideoH264HrdParameters>           sps_array_h264_vk_hrd( self->video_data->sps_count );

		for ( size_t i = 0; i != self->video_data->sps_count; i++ ) {

			const h264::SPS& sps = *( reinterpret_cast<const h264::SPS*>( self->video_data->sps_bytes.data() ) + i );

			auto get_chroma_format = []( int const& profile, int const& chroma ) -> StdVideoH264ChromaFormatIdc {
				if ( profile < STD_VIDEO_H264_PROFILE_IDC_HIGH ) {
					// If profile is less than HIGH chroma format will not be explicitly given. (A.2)
					// If chroma format is not present, it shall be inferred to be equal to 1 (4:2:0) (7.4.2.1.1)
					return StdVideoH264ChromaFormatIdc::STD_VIDEO_H264_CHROMA_FORMAT_IDC_420;
				} else {
					// If Profile is greater than High, then we assume chroma to be explicitly specified.
					return StdVideoH264ChromaFormatIdc( chroma );
				}
			};

			sps_array_h264[ i ] = {
			    .flags = {
			        .constraint_set0_flag                 = uint32_t( sps.constraint_set0_flag ),
			        .constraint_set1_flag                 = uint32_t( sps.constraint_set1_flag ),
			        .constraint_set2_flag                 = uint32_t( sps.constraint_set2_flag ),
			        .constraint_set3_flag                 = uint32_t( sps.constraint_set3_flag ),
			        .constraint_set4_flag                 = uint32_t( sps.constraint_set4_flag ),
			        .constraint_set5_flag                 = uint32_t( sps.constraint_set5_flag ),
			        .direct_8x8_inference_flag            = uint32_t( sps.direct_8x8_inference_flag ),
			        .mb_adaptive_frame_field_flag         = uint32_t( sps.mb_adaptive_frame_field_flag ),
			        .frame_mbs_only_flag                  = uint32_t( sps.frame_mbs_only_flag ),
			        .delta_pic_order_always_zero_flag     = uint32_t( sps.delta_pic_order_always_zero_flag ),
			        .separate_colour_plane_flag           = uint32_t( sps.separate_colour_plane_flag ),
			        .gaps_in_frame_num_value_allowed_flag = uint32_t( sps.gaps_in_frame_num_value_allowed_flag ),
			        .qpprime_y_zero_transform_bypass_flag = uint32_t( sps.qpprime_y_zero_transform_bypass_flag ),
			        .frame_cropping_flag                  = uint32_t( sps.frame_cropping_flag ),
			        .seq_scaling_matrix_present_flag      = uint32_t( sps.seq_scaling_matrix_present_flag ),
			        .vui_parameters_present_flag          = uint32_t( sps.vui_parameters_present_flag ),
			    },
			    .profile_idc                           = StdVideoH264ProfileIdc( sps.profile_idc ),
			    .level_idc                             = StdVideoH264LevelIdc( sps.level_idc ),
			    .chroma_format_idc                     = get_chroma_format( sps.profile_idc, sps.chroma_format_idc ),
			    .seq_parameter_set_id                  = uint8_t( sps.seq_parameter_set_id ),
			    .bit_depth_luma_minus8                 = uint8_t( sps.bit_depth_luma_minus8 ),
			    .bit_depth_chroma_minus8               = uint8_t( sps.bit_depth_chroma_minus8 ),
			    .log2_max_frame_num_minus4             = uint8_t( sps.log2_max_frame_num_minus4 ),
			    .pic_order_cnt_type                    = StdVideoH264PocType( sps.pic_order_cnt_type ),
			    .offset_for_non_ref_pic                = int32_t( sps.offset_for_non_ref_pic ),
			    .offset_for_top_to_bottom_field        = int32_t( sps.offset_for_top_to_bottom_field ),
			    .log2_max_pic_order_cnt_lsb_minus4     = uint8_t( sps.log2_max_pic_order_cnt_lsb_minus4 ),
			    .num_ref_frames_in_pic_order_cnt_cycle = uint8_t( sps.num_ref_frames_in_pic_order_cnt_cycle ),
			    .max_num_ref_frames                    = uint8_t( sps.num_ref_frames ),
			    .reserved1                             = 0,
			    .pic_width_in_mbs_minus1               = uint32_t( sps.pic_width_in_mbs_minus1 ),
			    .pic_height_in_map_units_minus1        = uint32_t( sps.pic_height_in_map_units_minus1 ),
			    .frame_crop_left_offset                = uint32_t( sps.frame_crop_left_offset ),
			    .frame_crop_right_offset               = uint32_t( sps.frame_crop_right_offset ),
			    .frame_crop_top_offset                 = uint32_t( sps.frame_crop_top_offset ),
			    .frame_crop_bottom_offset              = uint32_t( sps.frame_crop_bottom_offset ),
			    .reserved2                             = 0,
			    .pOffsetForRefFrame                    = nullptr, // todo:?
			    .pScalingLists                         = &sps_array_h264_scaling_lists[ i ],
			    .pSequenceParameterSetVui              = &sps_array_h264_vui[ i ],
			};

			// VUI stands for "Video Usablility Information"
			auto& vui = sps.vui;

			sps_array_h264_vui[ i ] = {
			    .flags = {
			        .aspect_ratio_info_present_flag  = uint32_t( vui.aspect_ratio_info_present_flag ),
			        .overscan_info_present_flag      = uint32_t( vui.overscan_info_present_flag ),
			        .overscan_appropriate_flag       = uint32_t( vui.overscan_appropriate_flag ),
			        .video_signal_type_present_flag  = uint32_t( vui.video_signal_type_present_flag ),
			        .video_full_range_flag           = uint32_t( vui.video_full_range_flag ),
			        .color_description_present_flag  = uint32_t( vui.colour_description_present_flag ),
			        .chroma_loc_info_present_flag    = uint32_t( vui.chroma_loc_info_present_flag ),
			        .timing_info_present_flag        = uint32_t( vui.timing_info_present_flag ),
			        .fixed_frame_rate_flag           = uint32_t( vui.fixed_frame_rate_flag ),
			        .bitstream_restriction_flag      = uint32_t( vui.bitstream_restriction_flag ),
			        .nal_hrd_parameters_present_flag = uint32_t( vui.nal_hrd_parameters_present_flag ),
			        .vcl_hrd_parameters_present_flag = uint32_t( vui.vcl_hrd_parameters_present_flag ),
			    }, // StdVideoH264SpsVuiFlags
			    .aspect_ratio_idc                    = StdVideoH264AspectRatioIdc( vui.aspect_ratio_idc ),
			    .sar_width                           = uint16_t( vui.sar_width ),
			    .sar_height                          = uint16_t( vui.sar_height ),
			    .video_format                        = uint8_t( vui.video_format ),
			    .colour_primaries                    = uint8_t( vui.colour_primaries ),
			    .transfer_characteristics            = uint8_t( vui.transfer_characteristics ),
			    .matrix_coefficients                 = uint8_t( vui.matrix_coefficients ),
			    .num_units_in_tick                   = uint32_t( vui.num_units_in_tick ),
			    .time_scale                          = uint32_t( vui.time_scale ),
			    .max_num_reorder_frames              = uint8_t( vui.num_reorder_frames ),
			    .max_dec_frame_buffering             = uint8_t( vui.max_dec_frame_buffering ),
			    .chroma_sample_loc_type_top_field    = uint8_t( vui.chroma_sample_loc_type_top_field ),
			    .chroma_sample_loc_type_bottom_field = uint8_t( vui.chroma_sample_loc_type_bottom_field ),
			    .reserved1                           = 0,
			    .pHrdParameters                      = &sps_array_h264_vk_hrd[ i ],
			};
			{
				StdVideoH264HrdParameters& vk_hrd = sps_array_h264_vk_hrd[ i ];

				auto const& hrd = sps.hrd;
				vk_hrd          = {
				             .cpb_cnt_minus1                          = uint8_t( hrd.cpb_cnt_minus1 ),
				             .bit_rate_scale                          = uint8_t( hrd.bit_rate_scale ),
				             .cpb_size_scale                          = uint8_t( hrd.cpb_size_scale ),
				             .reserved1                               = uint8_t(),
				             .bit_rate_value_minus1                   = {}, // array uint32_t
				             .cpb_size_value_minus1                   = {}, // array uint32_t
				             .cbr_flag                                = {}, // array uint8_t
				             .initial_cpb_removal_delay_length_minus1 = uint32_t( hrd.initial_cpb_removal_delay_length_minus1 ),
				             .cpb_removal_delay_length_minus1         = uint32_t( hrd.cpb_removal_delay_length_minus1 ),
				             .dpb_output_delay_length_minus1          = uint32_t( hrd.dpb_output_delay_length_minus1 ),
				             .time_offset_length                      = uint32_t( hrd.time_offset_length ),
                };

				// Sigh, nobody said it was easy ...
				for ( int j = 0; j != STD_VIDEO_H264_CPB_CNT_LIST_SIZE; j++ ) {
					vk_hrd.bit_rate_value_minus1[ j ] = hrd.bit_rate_value_minus1[ j ]; // array
					vk_hrd.cpb_size_value_minus1[ j ] = hrd.cpb_size_value_minus1[ j ]; // array
					vk_hrd.cbr_flag[ j ]              = hrd.cbr_flag[ j ];              // array uint8_t(),
				}
			}

			{ // Now fill in the Scaling Lists
				StdVideoH264ScalingLists& sl = sps_array_h264_scaling_lists[ i ];
				sl                           = {};
				{
					decltype( sl.scaling_list_present_mask ) j;
					for ( j = 0; j != ARRAY_SIZE( sps.seq_scaling_list_present_flag ); j++ ) {
						sl.scaling_list_present_mask |=
						    static_cast<decltype( j )>( sps.seq_scaling_list_present_flag[ j ] ) << j;
					}
				}
				{
					decltype( sl.use_default_scaling_matrix_mask ) j;
					for ( j = 0; j != ARRAY_SIZE( sps.UseDefaultScalingMatrix4x4Flag ); j++ ) {
						sl.use_default_scaling_matrix_mask |=
						    static_cast<decltype( j )>( sps.UseDefaultScalingMatrix4x4Flag[ j ] ) << j;
					}
				}

				for ( size_t list_idx = 0;
				      list_idx < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_LISTS &&
				      list_idx < ARRAY_SIZE( sps.ScalingList4x4 );
				      list_idx++ ) {
					for ( size_t el_idx = 0;
					      el_idx < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_ELEMENTS &&
					      el_idx < ARRAY_SIZE( sps.ScalingList4x4[ 0 ] );
					      el_idx++ ) {
						sl.ScalingList4x4[ list_idx ][ el_idx ] = sps.ScalingList4x4[ list_idx ][ el_idx ];
					}
				}

				for ( size_t list_idx = 0;
				      list_idx < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_LISTS &&
				      list_idx < ARRAY_SIZE( sps.ScalingList8x8 );
				      list_idx++ ) {
					for ( size_t el_idx = 0;
					      el_idx < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_ELEMENTS &&
					      el_idx < ARRAY_SIZE( sps.ScalingList8x8[ 0 ] );
					      el_idx++ ) {
						sl.ScalingList8x8[ list_idx ][ el_idx ] = sps.ScalingList8x8[ list_idx ][ el_idx ];
					}
				}
			}
		}

		VkVideoDecodeH264SessionParametersAddInfoKHR session_parameters_add_info_h264 = {
		    .sType       = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR,
		    .pNext       = nullptr,
		    .stdSPSCount = self->video_data->sps_count,
		    .pStdSPSs    = sps_array_h264.data(),
		    .stdPPSCount = self->video_data->pps_count,
		    .pStdPPSs    = pps_array_h264.data(),
		};
		VkVideoDecodeH264SessionParametersCreateInfoKHR video_decode_h264_session_paramers_create_info = {
		    .sType              = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR,
		    .pNext              = nullptr,
		    .maxStdSPSCount     = self->video_data->pps_count,
		    .maxStdPPSCount     = self->video_data->sps_count,
		    .pParametersAddInfo = &session_parameters_add_info_h264,
		};
		VkVideoSessionParametersCreateInfoKHR video_session_parameters_create_info = {
		    .sType                          = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR,
		    .pNext                          = &video_decode_h264_session_paramers_create_info,
		    .flags                          = 0,
		    .videoSessionParametersTemplate = nullptr,
		    .videoSession                   = self->vk_video_session,
		};

		VkResult res =
		    vkCreateVideoSessionParametersKHR(
		        self->device,
		        &video_session_parameters_create_info,
		        nullptr,
		        &self->vk_video_session_parameters );

		assert( VK_SUCCESS == res );
	}

	{
		// detect whether output images coincide

		if ( self->properties.decode_capabilities.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR ) {
			logger.info( "NOTE: video decode: dpb and output coincide" );
			self->properties.do_dpb_and_out_images_coincide = true;
		} else {
			self->properties.do_dpb_and_out_images_coincide = false;
			logger.info( "NOTE: video decode: dpb and output NOT coincide" );
		}
		if ( self->properties.decode_capabilities.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR ) {
			self->properties.do_dpb_and_out_images_coincide = false;
			logger.info( "NOTE: video decode: dpb and output distinct" );
		} else {
			self->properties.do_dpb_and_out_images_coincide = true;
			logger.info( "NOTE: video decode: dpb and output NOT distinct" );
		}

		if ( self->properties.do_dpb_and_out_images_coincide ) {
			self->properties.usage_flags_dpb_image =
			    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			    VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
			    VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR //
			    ;

			self->properties.usage_flags_out_image =
			    VK_IMAGE_USAGE_SAMPLED_BIT |
			    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			    VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		} else {
			self->properties.usage_flags_dpb_image =
			    VK_IMAGE_USAGE_SAMPLED_BIT |
			    VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;

			self->properties.usage_flags_out_image =
			    VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR |
			    VK_IMAGE_USAGE_SAMPLED_BIT |
			    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}
	}

	{

		// Allocate an image array to store decoded pictures in  -
		// we need an array with (num_reference_frames) + 1 elements.
		// the +1 is for the frame that is currently being decoded.
		//
		// we know there will be at max 17 images (16+1) as 16 is the max by the standard.

		using namespace le_backend_vk;
		VmaAllocationCreateInfo allocation_create_info{};
		allocation_create_info.flags          = {}; // default flags
		allocation_create_info.usage          = VMA_MEMORY_USAGE_GPU_ONLY;
		allocation_create_info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

		VkImageCreateInfo image_create_info = {
		    .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,       // VkStructureType
		    .pNext                 = &self->settings.profile_list_info,         //
		    .flags                 = 0,                                         // VkImageCreateFlags, optional
		    .imageType             = VkImageType::VK_IMAGE_TYPE_2D,             // VkImageType
		    .format                = self->properties.format_properties.format, // VkFormat
		    .extent                = { .width  = self->video_data->width,
		                               .height = self->video_data->height,
		                               .depth  = 1 },                               // VkExtent3D
		    .mipLevels             = 1,                                            // uint32_t
		    .arrayLayers           = 1,                                            // uint32_t
		    .samples               = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT, // VkSampleCountFlagBits
		    .tiling                = VK_IMAGE_TILING_OPTIMAL,                      // VkImageTiling
		    .usage                 = self->properties.usage_flags_dpb_image,       // VkImageUsageFlags
		    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,                    // VkSharingMode
		    .queueFamilyIndexCount = 0,                                            // uint32_t, optional
		    .pQueueFamilyIndices   = nullptr,                                      // uint32_t const *
		    .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,                    // VkImageLayout

		};

		self->dpb_image_array.resize( self->video_data->max_reference_pictures );

		for ( auto& array_element : self->dpb_image_array ) {
			VkResult result =
			    VkResult(
			        private_backend_vk_i.allocate_image(
			            self->backend,
			            &image_create_info,
			            &allocation_create_info,
			            &array_element.image,
			            &array_element.allocation,
			            &array_element.allocation_info ) );
			if ( VK_SUCCESS != result ) {
				logger.error( "Could not allocate images for decoded picture buffer" );
			};

			// we create one image view for each image
			VkImageViewCreateInfo image_view_create_info = {
			    .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,  // VkStructureType
			    .pNext            = nullptr,                                   // void *, optional
			    .flags            = 0,                                         // VkImageViewCreateFlags, optional
			    .image            = array_element.image,                       // VkImage
			    .viewType         = VK_IMAGE_VIEW_TYPE_2D,                     // VkImageViewType
			    .format           = self->properties.format_properties.format, // VkFormat
			    .components       = {},                                        // {} means identity, VkComponentMapping
			    .subresourceRange = {
			        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT, // VkImageAspectFlags
			        .baseMipLevel   = 0,                         // uint32_t
			        .levelCount     = 1,                         // uint32_t
			        .baseArrayLayer = 0,                         // uint32_t
			        .layerCount     = 1,                         // uint32_t
			    },                                               // VkImageSubresourceRange
			};

			result = vkCreateImageView( self->device, &image_view_create_info, nullptr, &array_element.image_view );

			if ( VK_SUCCESS != result ) {
				logger.error( "Could not create ImageView for decoded picture buffer" );
			};
		}
	}

	{
		// Allocate memory frames for each decoded frame - on each invocation of the decoder,
		// we generate another frame. each frame holds one image that contains the decoded
		// frame and that can be shared with other queues.
		//
		// Each memory frame also reserves 1/nth of the available mapped memory for gpu transfers.

		using namespace le_backend_vk;
		VmaAllocationCreateInfo allocation_create_info{};
		allocation_create_info.flags          = {}; // default flags
		allocation_create_info.usage          = VMA_MEMORY_USAGE_GPU_ONLY;
		allocation_create_info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		VkImageCreateInfo image_create_info   = {
		      .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,       // VkStructureType
		      .pNext                 = &self->settings.profile_list_info,         // , optional
		      .flags                 = 0,                                         // VkImageCreateFlags, optional
		      .imageType             = VkImageType::VK_IMAGE_TYPE_2D,             // VkImageType
		      .format                = self->properties.format_properties.format, // VkFormat
		      .extent                = { .width  = self->video_data->width,
		                                 .height = self->video_data->height,
		                                 .depth  = 1 },                               // VkExtent3D
		      .mipLevels             = 1,                                            // uint32_t
		      .arrayLayers           = 1,                                            // uint32_t
		      .samples               = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT, // VkSampleCountFlagBits
		      .tiling                = VK_IMAGE_TILING_OPTIMAL,                      // VkImageTiling
		      .usage                 = self->properties.usage_flags_out_image,       // VkImageUsageFlags
		      .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,                    // VkSharingMode
		      .queueFamilyIndexCount = 0,                                            // uint32_t, optional
		      .pQueueFamilyIndices   = nullptr,                                      // uint32_t const *
		      .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,                    // VkImageLayout

        };

        self->memory_frames.resize( num_memory_frames );

		int i = 0;

		/*
		 * Assign 1/n th of the allocated mapped memory to each memory frame -
		 * The important thing to know at this point is the number of bytes
		 * needed to transfer the largest raw data frame.
		 *
		 * This information is known via self->video_data->max_memory_frame_size_bytes
		 * but if we are running this as a stream decoder we might need a different
		 * heuristic; and we might even need to do adjust this during runtime.
		 *
		 */

		for ( auto& f : self->memory_frames ) {

			f.id                         = i;
			f.decoder                    = self;
			f.rendergraph_image_resource = le::Renderer::produceImageHandle( nullptr );

			if ( false == self->properties.do_dpb_and_out_images_coincide ) {

				f.maybe_dst_image_info = new le_video_decoder_o::distinct_dst_image_info_t();

				// if images are distinct- then
				// allocate dst images and store them with frame memory -
				//	this are the images that are shared with other queues
				// and used to display the frame on screen.
				// allocate dst image
				VkResult result =
				    VkResult(
				        private_backend_vk_i.allocate_image(
				            self->backend,
				            &image_create_info,
				            &allocation_create_info,
				            &f.maybe_dst_image_info->dst_image,
				            &f.maybe_dst_image_info->dst_image_allocation,
				            &f.maybe_dst_image_info->dst_image_allocation_info ) );
				if ( VK_SUCCESS != result ) {
					logger.error( "Could not allocate dst images" );
				};

				VkSamplerYcbcrConversionInfo* p_sampler_conversion_info =
				    le_backend_vk::private_backend_vk_i.get_sampler_ycbcr_conversion_info( self->backend );

				// we create one image view for each image
				VkImageViewCreateInfo image_view_create_info = {
				    .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,  // VkStructureType
				    .pNext            = p_sampler_conversion_info,                 // samplerConversionInfo
				    .flags            = 0,                                         // VkImageViewCreateFlags, optional
				    .image            = f.maybe_dst_image_info->dst_image,         // VkImage
				    .viewType         = VK_IMAGE_VIEW_TYPE_2D,                     // VkImageViewType
				    .format           = self->properties.format_properties.format, // VkFormat
				    .components       = {},                                        // {} means identity, VkComponentMapping
				    .subresourceRange = {
				        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT, // VkImageAspectFlags
				        .baseMipLevel   = 0,                         // uint32_t
				        .levelCount     = 1,                         // uint32_t
				        .baseArrayLayer = 0,                         // uint32_t
				        .layerCount     = 1,                         // uint32_t
				    },                                               // VkImageSubresourceRange
				};

				result = vkCreateImageView( self->device, &image_view_create_info, nullptr, &f.maybe_dst_image_info->dst_image_view );

				if ( VK_SUCCESS != result ) {
					logger.error( "Could not create ImageView for decoded picture buffer" );
				};
			}

			// Assign one/nth of the bitstream buffer capacity to this frame

			f.gpu_bitstream_capacity                    = self->video_data->max_memory_frame_size_bytes;
			f.gpu_bitstream_offset                      = i * self->video_data->max_memory_frame_size_bytes;
			f.gpu_bitstream_used_bytes_count            = 0; // initially no bytes are used
			f.gpu_bitstream_slice_mapped_memory_address = ( ( uint8_t* )pData ) + f.gpu_bitstream_offset;

			i++;
		}
	}

	if ( should_use_queries() ) {
		VkQueryPoolCreateInfo pool_create_info = {
		    .sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, // VkStructureType
		    .pNext              = &self->settings.profile_info,             // void *, optional
		    .flags              = 0,                                        // VkQueryPoolCreateFlags, optional
		    .queryType          = VK_QUERY_TYPE_RESULT_STATUS_ONLY_KHR,     // VkQueryType
		    .queryCount         = uint32_t( self->memory_frames.size() ),   // uint32_t
		    .pipelineStatistics = 0,                                        // VkQueryPipelineStatisticFlags, optional
		};
		vkCreateQueryPool( self->device, &pool_create_info, nullptr, &self->vk_query_pool );
	}

	self->mp4_filestream.seekg( 0, std::ios::beg ); // rewind filestream to start

	return self;
}

// ----------------------------------------------------------------------

static void le_video_decoder_destroy( le_video_decoder_o* self ) {
	using namespace le_backend_vk;

	// Decrease reference count

	size_t num = --self->reference_count;

	// Once reference count has hit 0, we destroy the object.

	if ( num == 0 && self ) {

		if ( self->mp4_filestream.is_open() ) {
			self->mp4_filestream.close();
		}

		{
			// destroy any memory frames - and the content that they own
			for ( auto& m : self->memory_frames ) {
				// destroy any vulkan objects associated with memory frames
				if ( false == self->properties.do_dpb_and_out_images_coincide ) {
					if ( m.maybe_dst_image_info->dst_image_view ) {
						// destroy image view
						vkDestroyImageView( self->device, m.maybe_dst_image_info->dst_image_view, nullptr );
					}
					if ( m.maybe_dst_image_info->dst_image ) {
						// destroy image, and its allocation
						private_backend_vk_i.destroy_image(
						    self->backend,
						    m.maybe_dst_image_info->dst_image,
						    m.maybe_dst_image_info->dst_image_allocation );
					}
					delete m.maybe_dst_image_info;
				}
			}
			self->memory_frames.clear();
		}

		if ( self->vk_query_pool ) {
			vkDestroyQueryPool( self->device, self->vk_query_pool, nullptr );
			self->vk_query_pool = nullptr;
		}

		for ( auto& el : self->dpb_image_array ) {

			// destroy image view
			vkDestroyImageView( self->device, el.image_view, nullptr );

			// destroy image, and its allocation
			private_backend_vk_i.destroy_image(
			    self->backend,
			    el.image,
			    el.allocation );
		}
		self->dpb_image_array.clear();

		if ( self->vk_video_session_parameters ) {
			vkDestroyVideoSessionParametersKHR( self->device, self->vk_video_session_parameters, nullptr );
			logger.info( "Destroyed Video Session Parameters %p", self->vk_video_session_parameters );
			self->vk_video_session_parameters = nullptr;
		}

		// if any pdb allocations were made, we must undo these here
		for ( auto& allocation : self->session_memory_allocations ) {
			if ( allocation ) {
				private_backend_vk_i.free_gpu_memory( self->backend, allocation );
				logger.info( "Freed Video Session Allocation: %p", allocation );
				allocation = nullptr;
			}
		}

		// destroy the video session: note: part-ownership of the session should go to the
		// frame so that the frame may keep the session alife until it has been reset.
		// destroying the video session should be triggered by the backend frame reset once
		// a session is in-flight.

		vkDestroyVideoSessionKHR( self->device, self->vk_video_session, nullptr );
		logger.info( "Destroyed Video Session: %p", self->vk_video_session );
		self->vk_video_session = nullptr;

		// destroy frame data container
		delete self->video_data;

		// destroy bitstream buffer
		if ( self->gpu_bitstream_buffer.allocation ) {

			// memory remains mapped until we don't need it anymore
			private_backend_vk_i.unmap_gpu_memory( self->backend, self->gpu_bitstream_buffer.allocation );

			// de-allocate bitstream buffer
			private_backend_vk_i.destroy_buffer( self->backend, self->gpu_bitstream_buffer.buffer, self->gpu_bitstream_buffer.allocation );
			self->gpu_bitstream_buffer = {};
		}

		MP4D_close( &self->mp4_demux );

		delete self;

		logger.info( "Destroyed Video Decoder %p", self );

		self = nullptr;
	}
}

// ----------------------------------------------------------------------
// We could use this to free the image that has been handed off to display,
// too - not just the backend frame. this image may have more than one re-
// ference though, in case the video decoder has less fps than our screen.
//
// This callback gets called once the decoding frame has passed the Fence, meaning
// that all its resources can be safely re-used, as they are not referenced by the
// backend anymore.
static void le_video_decoder_on_backend_frame_clear_cb( void* user_data ) {
	auto     cp        = static_cast<le_video_decoder_o::video_decoder_memory_frame*>( user_data );
	auto     decoder   = cp->decoder;
	uint32_t frame_num = cp->id;

	if ( cp->flags & le_video_decoder_o::video_decoder_memory_frame::eQueryIssued ) {
		// Query result status for the frame at frame_num

		VkQueryResultStatusKHR status;
		VkResult               result = vkGetQueryPoolResults(
		                  decoder->device, decoder->vk_query_pool, frame_num, 1,
		                  sizeof( status ), &status, sizeof( status ),
		                  VK_QUERY_RESULT_WITH_STATUS_BIT_KHR );

		if ( status == VK_QUERY_RESULT_STATUS_NOT_READY_KHR /* 0 */ ) {
			// Query result not ready yet
		} else if ( status > 0 ) {
			// Video coding operation was successful, enum values indicate specific success status code
			cp->state = MemoryFrameState::eDecodeSuccess;
			// logger.info( "mem_frame[% 3d], decoded frame: %d", frame_num, decoder->memory_frames[ frame_num ].decoded_frame_index );

		} else if ( status < 0 ) {
			// Video coding operation was unsuccessful, enum values indicate specific failure status code
			cp->state = MemoryFrameState::eDecodeFailed;
		}

		cp->flags &= ~le_video_decoder_o::video_decoder_memory_frame::eQueryIssued;
	} else {
		// In case queries are not used, we assume all went well.
		cp->state = MemoryFrameState::eDecodeSuccess;
	}

	// We can also de-allocate any data that we had stored with the bitstream buffer at this slice.
	//
	// This works similar to arena allocation: resetting the size de-allocates the whole range for
	// the current gpu_bitstream.
	cp->gpu_bitstream_used_bytes_count = 0;

	// Now, we must decrease the reference count to decoder,
	// as we did increase it earlier.
	if ( decoder ) {
		le_video_decoder_destroy( decoder );
	}
}

// ----------------------------------------------------------------------
// This happens during backend::process
//
// NOTE: `decoder_memory_frame` is the current memory frame of the decoder and
// has nothing to do with video frames. We use it to limit memory
// accesses to the decoder to only things that are held within
// the memory frame indexed by self_frame_num.
static void video_decode( le_video_decoder_o* decoder, VkCommandBuffer cmd, le_video_decoder_o::video_decoder_memory_frame* decoder_memory_frame, void const* backend_frame_data ) {

	size_t decoded_frame_index = decoder_memory_frame->decoded_frame_index; // running sample num in stream - monotonically increasing

	uint32_t dpb_target_slot_idx = decoder->dpb_target_slot_idx;

	auto const& frame_info = decoder_memory_frame->frame_info;

	// use slice header from frame instead
	h264::SliceHeader const* slice_header = &decoder_memory_frame->frame_info.slice_header;

	auto pps_array = ( h264::PPS* )decoder->video_data->pps_bytes.data();
	auto sps_array = ( h264::SPS* )decoder->video_data->sps_bytes.data();

	const h264::PPS& pps = pps_array[ slice_header->pic_parameter_set_id ];
	const h264::SPS& sps = sps_array[ pps.seq_parameter_set_id ];

	size_t num_dpb_slots = decoder->video_data->num_dpb_slots;

	bool frame_is_reference = false;

	std::deque<le_video_decoder_o::dpb_state_t> dpb_state;

	VkImage rendergraph_dst_image =
	    le_backend_vk::private_backend_vk_i
	        .frame_data_get_image_from_le_resource_id(
	            ( BackendFrameData const* )backend_frame_data,
	            decoder_memory_frame->rendergraph_image_resource );

	if ( frame_info.nal_unit_type == 5 ) {
		// If this picture has an IDR (immediate decoder reset) flag,
		// this means that we must delete all previous reference images
		// logger.warn( "idr frame detected" );
		dpb_state           = {};
		dpb_target_slot_idx = 0;
	} else {
		dpb_state = decoder->dpb_state; // NOTE: we're taking a copy.
	}

	const VkVideoPictureResourceInfoKHR VK_VIDEO_PICTURE_RESOURCE_INFO_KHR_TEMPLATE = {
	    .sType       = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR, // VkStructureType
	    .pNext       = nullptr,                                           // , optional
	    .codedOffset = { .x = 0, .y = 0 },                                // VkOffset2D
	    .codedExtent = {
	        .width  = decoder->video_data->width,
	        .height = decoder->video_data->height,
	    },                           // VkExtent2D
	    .baseArrayLayer   = 0,       // uint32_t, Image Layer Index - you would have to set this if you were not using individual images, but arrays
	    .imageViewBinding = nullptr, // set later
	};

	const VkVideoReferenceSlotInfoKHR VK_VIDEO_REFERENCE_SLOT_INFO_KHR_TEMPLATE = {
	    .sType            = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR, // VkStructureType
	    .pNext            = nullptr,                                         // must be a VkVideoReferenceSlotInfoKHR
	    .slotIndex        = -1,                                              // -1 means no reference associated with this slot
	    .pPictureResource = nullptr,                                         // VkVideoPictureResourceInfoKHR const *
	};

	std::vector<VkVideoPictureResourceInfoKHR> picture_resource_infos( num_dpb_slots, VK_VIDEO_PICTURE_RESOURCE_INFO_KHR_TEMPLATE );
	std::vector<VkVideoReferenceSlotInfoKHR>   reference_slots_info( num_dpb_slots, VK_VIDEO_REFERENCE_SLOT_INFO_KHR_TEMPLATE );

	{
		// set slot index to -1 for the target dpb element
		picture_resource_infos[ dpb_target_slot_idx ].imageViewBinding = decoder->dpb_image_array[ dpb_target_slot_idx ].image_view;
		picture_resource_infos[ dpb_target_slot_idx ].baseArrayLayer   = 0; // TODO: change if dpb images held in image array
		reference_slots_info[ dpb_target_slot_idx ].pPictureResource   = &picture_resource_infos[ dpb_target_slot_idx ];
		reference_slots_info[ dpb_target_slot_idx ].slotIndex          = -1; // -1 means that there is no reference currently associated with this slot.
	}

	for ( size_t i = 1; i != num_dpb_slots; i++ ) {
		uint32_t frame_id = ( i + dpb_target_slot_idx ) % num_dpb_slots;

		picture_resource_infos[ frame_id ].imageViewBinding = decoder->dpb_image_array[ frame_id ].image_view;
		picture_resource_infos[ frame_id ].baseArrayLayer   = 0; // TODO: change if dpb images held in image array
		reference_slots_info[ frame_id ].pPictureResource   = &picture_resource_infos[ frame_id ];
		reference_slots_info[ frame_id ].slotIndex          = ( frame_id <= dpb_state.size() ? frame_id : -1 );
	}

	VkVideoBeginCodingInfoKHR begin_video_coding_info = {
	    .sType                  = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR, // VkStructureType
	    .pNext                  = nullptr,                                       // , optional
	    .flags                  = 0,                                             // reserved for future use
	    .videoSession           = decoder->vk_video_session,                     // VkVideoSessionKHR
	    .videoSessionParameters = decoder->vk_video_session_parameters,          // VkVideoSessionParametersKHR
	    .referenceSlotCount     = uint32_t( reference_slots_info.size() ),       // uint32_t, optional
	    .pReferenceSlots        = reference_slots_info.data(),                   // VkVideoReferenceSlotInfoKHR const *
	};

	// Reset query for this frame so that we may use it afresh
	if ( should_use_queries() ) {
		vkCmdResetQueryPool( cmd, decoder->vk_query_pool, decoder_memory_frame->id, 1 );
	}

	if constexpr ( SHOULD_PRINT_LOG_MESSAGES ) {
		logger.info( "Begin video coding: (dpb target slot idx: %d)", dpb_target_slot_idx );
		for ( size_t i = 0; i != begin_video_coding_info.referenceSlotCount; i++ ) {
			auto& slot = begin_video_coding_info.pReferenceSlots[ i ];
			logger.info(
			    "slot [ % 2d ] - slotIndex: [ % 2d ] - image: %p",
			    i,
			    slot.slotIndex,
			    slot.pPictureResource->imageViewBinding );
		}
		logger.info( "------" );
	}

	/*
	 * When you begin a session you need to specify which references are currently bound
	 * and which references you will use - in case a slot is not yet valid, you place -1 int the slotId.
	 * Once a picture has been decoded the slot it has been decoded into becomes a valid reference
	 * in case this slot was declared as a reference. in case it was not declared as a reference,
	 * the picture gets rendered into the correct image array layer, but there will be no valid reference to it.
	 */
	vkCmdBeginVideoCodingKHR( cmd, &begin_video_coding_info );

	{
		// Initialize the Video Decode Session

		if ( false == ( decoder->flags & le_video_decoder_o::eInitialResetIssued ) ) {
			VkVideoCodingControlInfoKHR video_coding_control_info = {
			    .sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR, // VkStructureType
			    .pNext = nullptr,                                         // void *, optional
			    .flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR,           // VkVideoCodingControlFlagsKHR
			};

			vkCmdControlVideoCodingKHR( cmd, &video_coding_control_info );

			// Signal that initial reset was issued
			decoder->flags |= le_video_decoder_o::eInitialResetIssued;
		}
	}

	// IMAGE LAYOUT TRANSFORM: transfer target image to video_decode_dpb layout
	{

		// Transfer image memory for the image that will receive the decoded picture
		// so that it is in the correct layout: video_decode_dpb
		std::vector<VkImageMemoryBarrier2> img_barriers = {
		    {
		        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		        .pNext               = nullptr,                                  // optional
		        .srcStageMask        = VK_PIPELINE_STAGE_2_NONE,                 // wait for nothing
		        .srcAccessMask       = 0,                                        // flush nothing
		        .dstStageMask        = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR, // block on any video decode stage
		        .dstAccessMask       = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR,   // make memory visible to decode stage (after layout transition)
		        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,                // transition from undefined - as in we don't care...
		        .newLayout           = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR,     // to video decode
		        .srcQueueFamilyIndex = decoder->backend_video_decoder_queue_family_index,
		        .dstQueueFamilyIndex = decoder->backend_video_decoder_queue_family_index,
		        .image               = decoder->dpb_image_array[ dpb_target_slot_idx ].image,
		        .subresourceRange    = {
		               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		               .baseMipLevel   = 0,
		               .levelCount     = 1,
		               .baseArrayLayer = 0,
		               .layerCount     = 1,
                },
		    },

		};

		if ( false == decoder->properties.do_dpb_and_out_images_coincide ) {
			// add a barrier for the dst image in case it is distinct
			img_barriers.push_back(
			    {
			        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			        .pNext               = nullptr,                                  // optional
			        .srcStageMask        = VK_PIPELINE_STAGE_2_NONE,                 // wait for nothing
			        .srcAccessMask       = 0,                                        // flush nothing
			        .dstStageMask        = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR, // block on any video decode stage
			        .dstAccessMask       = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR,   // make memory visible to decode stage (after layout transition)
			        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,                // transition from undefined - as in we don't care...
			        .newLayout           = VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR,     // to video decode
			        .srcQueueFamilyIndex = decoder->backend_video_decoder_queue_family_index,
			        .dstQueueFamilyIndex = decoder->backend_video_decoder_queue_family_index,
			        .image               = decoder_memory_frame->maybe_dst_image_info->dst_image,
			        .subresourceRange    = {
			               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			               .baseMipLevel   = 0,
			               .levelCount     = 1,
			               .baseArrayLayer = 0,
			               .layerCount     = 1,
                    },
			    } );
		}

		VkDependencyInfo info{
		    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		    .pNext                    = nullptr, // optional
		    .dependencyFlags          = 0,       // optional
		    .memoryBarrierCount       = 0,       // optional
		    .pMemoryBarriers          = 0,
		    .bufferMemoryBarrierCount = 0, // optional
		    .pBufferMemoryBarriers    = 0,
		    .imageMemoryBarrierCount  = uint32_t( img_barriers.size() ), // optional
		    .pImageMemoryBarriers     = img_barriers.data(),
		};

		vkCmdPipelineBarrier2( cmd, &info );
	}

	{

		/*
		 * VkSpec:
		 *
		 * "The offsets provided in VkVideoDecodeH264PictureInfoKHR::pSliceOffsets
		 * should specify the starting offsets corresponding to each slice
		 * header within the video bitstream buffer range."
		 *
		 * If we were feeding more than one slice into the decoder, this is where
		 * we would do it - one offset into the gpu buffer per slice.
		 *
		 * otherwise we can just use the general srcBufferOffset
		 *
		 */

		uint32_t slice_offsets[] = {
		    0,
		};

		// BUFFER BARRIER: issue a barrier - buffer data must be available for decoding when we decode.
		{

			std::vector<VkBufferMemoryBarrier2> buffer_memory_barriers(
			    {
			        {
			            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,            // VkStructureType
			            .pNext               = nullptr,                                              // void *, optional
			            .srcStageMask        = VK_PIPELINE_STAGE_2_HOST_BIT,                         // VkPipelineStageFlags2, optional
			            .srcAccessMask       = VK_ACCESS_2_HOST_WRITE_BIT,                           // VkAccessFlags2, optional
			            .dstStageMask        = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,             // VkPipelineStageFlags2, optional
			            .dstAccessMask       = VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR,                // VkAccessFlags2, optional
			            .srcQueueFamilyIndex = decoder->backend_video_decoder_queue_family_index,    // uint32_t
			            .dstQueueFamilyIndex = decoder->backend_video_decoder_queue_family_index,    // uint32_t
			            .buffer              = decoder->gpu_bitstream_buffer.buffer,                 // VkBuffer
			            .offset              = decoder_memory_frame->gpu_bitstream_offset,           // VkDeviceSize
			            .size                = decoder_memory_frame->gpu_bitstream_used_bytes_count, // VkDeviceSize
			        },
			    } );

			VkDependencyInfo info{
			    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			    .pNext                    = nullptr, // optional
			    .dependencyFlags          = 0,       // optional
			    .memoryBarrierCount       = 0,       // optional
			    .pMemoryBarriers          = 0,
			    .bufferMemoryBarrierCount = uint32_t( buffer_memory_barriers.size() ), // optional
			    .pBufferMemoryBarriers    = buffer_memory_barriers.data(),
			    .imageMemoryBarrierCount  = 0, // optional
			    .pImageMemoryBarriers     = 0,
			};

			vkCmdPipelineBarrier2( cmd, &info );
		}

		if ( should_use_queries() ) {
			vkCmdBeginQuery( cmd, decoder->vk_query_pool, decoder_memory_frame->id, 0 );
		}

		{

			// find out whether the current frame should be used for reference -

			StdVideoDecodeH264ReferenceInfo dst_std_video_decode_h264_reference_info =
			    {
			        .flags = {
			            .top_field_flag               = uint32_t( slice_header->field_pic_flag && !slice_header->bottom_field_flag ), // uint32_t // > either this
			            .bottom_field_flag            = uint32_t( slice_header->field_pic_flag && slice_header->bottom_field_flag ),  // uint32_t // > or this
			            .used_for_long_term_reference = 0,                                                                            // uint32_t
			            .is_non_existing              = 0,                                                                            // uint32_t
			        },                                                                                                                // StdVideoDecodeH264ReferenceInfoFlags
			        .FrameNum    = uint16_t( slice_header->frame_num ),                                                               // uint16_t
			        .reserved    = 0,                                                                                                 // uint16_t
			        .PicOrderCnt = {
			            frame_info.poc, // top field
			            frame_info.poc, // bottom field
			        },                  // int32_t[] // pic order count
			    };

			const VkVideoDecodeH264DpbSlotInfoKHR VK_VIDEO_DECODE_H264_DPB_SLOT_INFO_TEMPLATE =
			    {
			        .sType             = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR, // VkStructureType
			        .pNext             = nullptr,                                               // , optional
			        .pStdReferenceInfo = nullptr,                                               // StdVideoDecodeH264ReferenceInfo const *
			    };

			VkVideoDecodeH264DpbSlotInfoKHR dst_dpb_slot_info = VK_VIDEO_DECODE_H264_DPB_SLOT_INFO_TEMPLATE;

			dst_dpb_slot_info.pStdReferenceInfo = &dst_std_video_decode_h264_reference_info;

			VkVideoPictureResourceInfoKHR dst_picture_resource_info =
			    {
			        .sType       = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR, // VkStructureType
			        .pNext       = nullptr,                                           // must be nullptr
			        .codedOffset = {
			            .x = 0,
			            .y = 0 }, // VkOffset2D
			        .codedExtent = {
			            .width  = decoder->video_data->width,
			            .height = decoder->video_data->height,
			        },                     // VkExtent2D
			        .baseArrayLayer   = 0, // uint32_t, Image Layer Index
			        .imageViewBinding = decoder->dpb_image_array[ dpb_target_slot_idx ].image_view,
			    };

			VkVideoReferenceSlotInfoKHR dst_reference_slot_info =
			    {
			        .sType            = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR, // VkStructureType
			        .pNext            = &dst_dpb_slot_info,                              // must be a VkVideoReferenceSlotInfoKHR
			        .slotIndex        = int32_t( dpb_target_slot_idx ),                  // -1 means no reference associated with this slot
			        .pPictureResource = &dst_picture_resource_info,                      // VkVideoPictureResourceInfoKHR const *, optional

			    };

			// this comes from the slice header
			StdVideoDecodeH264PictureInfo std_picture_info = {
			    .flags = {
			        .field_pic_flag = uint32_t( slice_header->field_pic_flag ),
			        .is_intra       = uint32_t(
			                  frame_info.frame_type == FrameType::eFrameTypeIntra ),
			        .IdrPicFlag               = uint32_t( frame_info.nal_unit_type == 5 ), // IdrPicFlag = ( ( nal_unit_type = = 5 ) ? 1 : 0 )"  // ITU-T H.264 Spec
			        .bottom_field_flag        = uint32_t( slice_header->field_pic_flag && slice_header->bottom_field_flag ),
			        .is_reference             = uint32_t( frame_info.nal_ref_idc != 0 ), // following vkSpec: "as defined in section 3.136 of the ITU-T H.264 Specification "
			        .complementary_field_pair = uint32_t( 0 ),                           // FIXME: see function which calculates field pair in NVIDIA vulkan example
			    },                                                                       // StdVideoDecodeH264PictureInfoFlags
			    .seq_parameter_set_id = uint8_t( pps.seq_parameter_set_id ),
			    .pic_parameter_set_id = uint8_t( slice_header->pic_parameter_set_id ),
			    .reserved1            = 0,
			    .reserved2            = 0,
			    .frame_num            = uint16_t( slice_header->frame_num ),
			    .idr_pic_id           = uint8_t( slice_header->idr_pic_id ),
			    .PicOrderCnt          = {
			                 frame_info.poc,
			                 frame_info.poc, // make sure this is correct
			    },                  // int32_t[2]
			};

			VkVideoDecodeH264PictureInfoKHR h264_picture_info = {
			    .sType           = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR, // VkStructureType
			    .pNext           = nullptr,                                              // void *, optional
			    .pStdPictureInfo = &std_picture_info,                                    // StdVideoDecodeH264PictureInfo const *
			    .sliceCount      = ARRAY_SIZE( slice_offsets ),                          // uint32_t
			    .pSliceOffsets   = slice_offsets,                                        // uint32_t const *
			};

			std::vector<VkVideoPictureResourceInfoKHR>   src_picture_resource_infos( dpb_state.size(), VK_VIDEO_PICTURE_RESOURCE_INFO_KHR_TEMPLATE );
			std::vector<VkVideoReferenceSlotInfoKHR>     src_reference_slots_info( dpb_state.size(), VK_VIDEO_REFERENCE_SLOT_INFO_KHR_TEMPLATE );
			std::vector<VkVideoDecodeH264DpbSlotInfoKHR> src_video_decode_h264_dpb_slot_info( dpb_state.size(), VK_VIDEO_DECODE_H264_DPB_SLOT_INFO_TEMPLATE );

			{
				size_t i = 0;
				for ( auto const& dpb_el : dpb_state ) {
					uint32_t slot_idx = dpb_el.slot_idx;

					src_picture_resource_infos[ i ].imageViewBinding           = decoder->dpb_image_array[ slot_idx ].image_view;
					src_picture_resource_infos[ i ].baseArrayLayer             = 0; // TODO: change if dpb images held in image array
					src_reference_slots_info[ i ].pPictureResource             = &src_picture_resource_infos[ i ];
					src_reference_slots_info[ i ].slotIndex                    = slot_idx;
					src_video_decode_h264_dpb_slot_info[ i ].pStdReferenceInfo = &dpb_el.reference_info;
					src_reference_slots_info[ i ].pNext                        = &src_video_decode_h264_dpb_slot_info[ i ];
					i++;
				}
			}

			VkVideoPictureResourceInfoKHR dst_picture_resource;

			if ( decoder->properties.do_dpb_and_out_images_coincide ) {
				dst_picture_resource = {
				    .sType            = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,           // VkStructureType
				    .pNext            = nullptr,                                                     // void *, optional
				    .codedOffset      = { 0, 0 },                                                    // VkOffset2D
				    .codedExtent      = { decoder->video_data->width, decoder->video_data->height }, // VkExtent2D
				    .baseArrayLayer   = 0,                                                           // uint32_t
				    .imageViewBinding = decoder->dpb_image_array[ dpb_target_slot_idx ].image_view,  // VkImageView
				};
			} else {
				// Use separate dst image if dst and dpb images do not coincide.
				dst_picture_resource = {
				    .sType            = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,           // VkStructureType
				    .pNext            = nullptr,                                                     // void *, optional
				    .codedOffset      = { 0, 0 },                                                    // VkOffset2D
				    .codedExtent      = { decoder->video_data->width, decoder->video_data->height }, // VkExtent2D
				    .baseArrayLayer   = 0,                                                           // uint32_t
				    .imageViewBinding = decoder_memory_frame->maybe_dst_image_info->dst_image_view,  // VkImageView
				};
			}

			VkVideoDecodeInfoKHR video_decode_info = {
			    .sType           = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR,    // VkStructureType
			    .pNext           = &h264_picture_info,                         // must contain a VkVideoDecodeH264PictureInfoKHR *
			    .flags           = 0,                                          // VkVideoDecodeFlagsKHR, optional
			    .srcBuffer       = decoder->gpu_bitstream_buffer.buffer,       // VkBuffer
			    .srcBufferOffset = decoder_memory_frame->gpu_bitstream_offset, // VkDeviceSize
			    .srcBufferRange =
			        align_to(
			            decoder_memory_frame->gpu_bitstream_used_bytes_count,
			            decoder->properties.capabilities.minBitstreamBufferSizeAlignment ), // VkDeviceSize

			    .dstPictureResource  = dst_picture_resource,                          // VkVideoPictureResourceInfoKHR
			    .pSetupReferenceSlot = &dst_reference_slot_info,                      // VkVideoReferenceSlotInfoKHR const * -> This is where the decoded picture will be stored - if this slot has index -1, then only the picture will be stored. if it has a valid index, the picture will be stored, and this slot index will be activated for further reference
			    .referenceSlotCount  = uint32_t( src_picture_resource_infos.size() ), // uint32_t
			    .pReferenceSlots     = src_reference_slots_info.data(),               // VkVideoReferenceSlotInfoKHR const * -> These are the active slots that will be used to reconstruct the picture. in case the picture is an idr frame or a i frame, no slots may be used.
			};

			if constexpr ( SHOULD_PRINT_LOG_MESSAGES ) {
				logger.info( "Decode video frame [% 8d]: (dpb target slot idx: %d)", decoder_memory_frame->decoded_frame_index, dpb_target_slot_idx );
				for ( size_t i = 0; i != video_decode_info.referenceSlotCount; i++ ) {
					auto& slot = video_decode_info.pReferenceSlots[ i ];
					logger.info(
					    " src [ % 2d ] - slotIndex: [ % 2d ] - image: %p",
					    i,
					    slot.slotIndex,
					    slot.pPictureResource->imageViewBinding );
				}
				logger.info(
				    " dst [    ] - slotIndex: [ % 2d ] - image: %p",
				    dst_reference_slot_info.slotIndex,
				    dst_reference_slot_info.pPictureResource->imageViewBinding );
				logger.info( "------" );
			}

			vkCmdDecodeVideoKHR( cmd, &video_decode_info );

			if ( std_picture_info.flags.is_reference ) {
				dpb_state.push_front( {
				    .slot_idx       = int32_t( dpb_target_slot_idx ),
				    .frame_num      = uint16_t( slice_header->frame_num ),
				    .reference_info = dst_std_video_decode_h264_reference_info,
				} );
				if ( dpb_state.size() > decoder->video_data->num_dpb_slots - 1 ) {
					dpb_state.resize( decoder->video_data->num_dpb_slots - 1 );
				}
				frame_is_reference = true;
			} else {
				// if a frame is not a reference, this means that we may re-use this slot,
				// and that we should not increase the index in the dpb buffer
				// but to re-use this index for the next frame.
				// logger.warn( "This image is not a reference" );
			}
		}
		if ( should_use_queries() ) {
			vkCmdEndQuery( cmd, decoder->vk_query_pool, decoder_memory_frame->id );
			decoder_memory_frame->flags |=
			    le_video_decoder_o::video_decoder_memory_frame::eQueryIssued;
		}
	}

	VkVideoEndCodingInfoKHR end_video_coding_info = {
	    .sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR, // VkStructureType
	    .pNext = nullptr,                                     // must be nullptr
	    .flags = 0,                                           // must be 0
	};

	vkCmdEndVideoCodingKHR( cmd, &end_video_coding_info );

	if ( decoder->properties.do_dpb_and_out_images_coincide ) {
		// if output and dpb images coincide, we need to copy last decoded image into the image that we want to render...

		// maybe a better way to do this is to pull the image from the rendergraph, and then copy into it.
		// on the other hand, we know so much about the image here, and we have the right resolve sampler etc.
		// we should probably provide the image from the decoder and make introduce it to the rendergraph as a resource
		// similar to the swapchain surface - which you can query via the swapchain, receiving a `le_image_resource` handle.

		// we will still have to manage that the current image needs to transfer queue ownership (i think that should work)
		// and that another queue can have it on the next frame. this means that we will need to pre-buffer some frames, and
		// images that have been decoded will at the earliest show on the following frame.

		// ----------------------------------------------------------------------

		{ // first a barrier from dpb to transfer src for the dpb image
			// and a barrier from dont' care to transfer dst for the dst image
			std::vector<VkImageMemoryBarrier2> img_barriers = {
			    {
			        // dpb image: decode_dpb -> transfer_src
			        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			        .pNext               = nullptr,                                  // optional
			        .srcStageMask        = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR, // wait for decode write
			        .srcAccessMask       = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR,   // flush decode write
			        .dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,         // block on any transfer state
			        .dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,            // make memory visible to transfer read stage (after layout transition)
			        .oldLayout           = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR,     // transition from decode pdb
			        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,     // to transfer src
			        .srcQueueFamilyIndex = decoder->backend_video_decoder_queue_family_index,
			        .dstQueueFamilyIndex = decoder->backend_video_decoder_queue_family_index,
			        .image               = decoder->dpb_image_array[ dpb_target_slot_idx ].image,
			        .subresourceRange    = {
			               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			               .baseMipLevel   = 0,
			               .levelCount     = 1,
			               .baseArrayLayer = 0,
			               .layerCount     = 1,
                    },
			    },
			    // NOTE: it is the rendergraph that does the layout transfer for this image to TRANSFER_DST
			};

			VkDependencyInfo info{
			    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			    .pNext                    = nullptr,
			    .dependencyFlags          = 0,
			    .memoryBarrierCount       = 0,
			    .pMemoryBarriers          = 0,
			    .bufferMemoryBarrierCount = 0,
			    .pBufferMemoryBarriers    = 0,
			    .imageMemoryBarrierCount  = uint32_t( img_barriers.size() ),
			    .pImageMemoryBarriers     = img_barriers.data(),
			};

			vkCmdPipelineBarrier2( cmd, &info );
		}

		{
			// Copy the Image over
			//
			// Since it is a two-plane image, and we can't just copy over the color aspect,
			// we must copy both planes as separate regions (sigh)
			VkImageCopy2 regions[] = {
			    {
			        .sType          = VK_STRUCTURE_TYPE_IMAGE_COPY_2, // VkStructureType
			        .pNext          = nullptr,                        // void *, optional
			        .srcSubresource = {
			            .aspectMask     = VK_IMAGE_ASPECT_PLANE_0_BIT, // VkImageAspectFlags
			            .mipLevel       = 0,                           // uint32_t
			            .baseArrayLayer = 0,                           // uint32_t
			            .layerCount     = 1,                           // uint32_t
			        },                                                 // VkImageSubresourceLayers
			        .srcOffset      = {},                              // VkOffset3D
			        .dstSubresource = {
			            .aspectMask     = VK_IMAGE_ASPECT_PLANE_0_BIT, // VkImageAspectFlags
			            .mipLevel       = 0,                           // uint32_t
			            .baseArrayLayer = 0,                           // uint32_t
			            .layerCount     = 1,                           // uint32_t
			        },                                                 // VkImageSubresourceLayers
			        .dstOffset = {},                                   // VkOffset3D
			        .extent    = {
			               .width  = decoder->video_data->width,
			               .height = decoder->video_data->height,
			               .depth  = 1,
                    }, // VkExtent3D

			    },
			    {
			        // CbCr plane - this plane has only half the resolution (width, height)
			        // of the Y plane.
			        .sType          = VK_STRUCTURE_TYPE_IMAGE_COPY_2, // VkStructureType
			        .pNext          = nullptr,                        // void *, optional
			        .srcSubresource = {
			            .aspectMask     = VK_IMAGE_ASPECT_PLANE_1_BIT, // VkImageAspectFlags
			            .mipLevel       = 0,                           // uint32_t
			            .baseArrayLayer = 0,                           // uint32_t
			            .layerCount     = 1,                           // uint32_t
			        },                                                 // VkImageSubresourceLayers
			        .srcOffset      = {},                              // VkOffset3D
			        .dstSubresource = {
			            .aspectMask     = VK_IMAGE_ASPECT_PLANE_1_BIT, // VkImageAspectFlags
			            .mipLevel       = 0,                           // uint32_t
			            .baseArrayLayer = 0,                           // uint32_t
			            .layerCount     = 1,                           // uint32_t
			        },                                                 // VkImageSubresourceLayers
			        .dstOffset = {},                                   // VkOffset3D
			        .extent    = {
			               .width  = decoder->video_data->width / 2,
			               .height = decoder->video_data->height / 2,
			               .depth  = 1,
                    }, // VkExtent3D
			    },
			};

			VkCopyImageInfo2 info = {
			    .sType          = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2,                   // VkStructureType
			    .pNext          = nullptr,                                               // void *, optional
			    .srcImage       = decoder->dpb_image_array[ dpb_target_slot_idx ].image, // VkImage
			    .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,                  // VkImageLayout
			    .dstImage       = rendergraph_dst_image,                                 // VkImage
			    .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,                  // VkImageLayout
			    .regionCount    = 2,                                                     // uint32_t
			    .pRegions       = regions,                                               // VkImageCopy2 const *
			};

			vkCmdCopyImage2( cmd, &info );
		}

		// then we need a layout transfer from transfer src to dpb image
		// for the dpb image, and a transfer from transfer dst to sampling
		// for the sampled image

		{
			std::vector<VkImageMemoryBarrier2> img_barriers = {
			    {
			        // dpb image: transfer_src -> decode_pdb
			        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			        .pNext               = nullptr,                                  // optional
			        .srcStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,      // wait for nothing
			        .srcAccessMask       = VK_ACCESS_2_NONE,                         // flush nothing
			        .dstStageMask        = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR, // block on any decode
			        .dstAccessMask       = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR,   // make memory visible to decode write (after layout transition)
			        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,     // transition from transfer src
			        .newLayout           = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR,     // to decode dpb
			        .srcQueueFamilyIndex = decoder->backend_video_decoder_queue_family_index,
			        .dstQueueFamilyIndex = decoder->backend_video_decoder_queue_family_index,
			        .image               = decoder->dpb_image_array[ dpb_target_slot_idx ].image,
			        .subresourceRange    = {
			               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			               .baseMipLevel   = 0,
			               .levelCount     = 1,
			               .baseArrayLayer = 0,
			               .layerCount     = 1,
                    },
			    },

			};

			VkDependencyInfo dependency_info{
			    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			    .pNext                    = nullptr,
			    .dependencyFlags          = 0,
			    .memoryBarrierCount       = 0,
			    .pMemoryBarriers          = 0,
			    .bufferMemoryBarrierCount = 0,
			    .pBufferMemoryBarriers    = 0,
			    .imageMemoryBarrierCount  = uint32_t( img_barriers.size() ),
			    .pImageMemoryBarriers     = img_barriers.data(),
			};

			vkCmdPipelineBarrier2( cmd, &dependency_info );
		}
	} else {

		// in case the images do not coincide, we must first draw into the
		// image that is held by the memory frame.
		// then, instead of copying from the dbb image, we must copy from
		// the memory frame image.
		logger.error( "Not implemented" );
	}

	{
		if ( frame_is_reference ) {
			// update decoder state to new dpb state
			std::swap( decoder->dpb_state, dpb_state );
			decoder->dpb_target_slot_idx = ( ++dpb_target_slot_idx ) % decoder->video_data->num_dpb_slots;
		}
	}
}

// ----------------------------------------------------------------------

// This callback gets executed by the backend when processing the video decode command buffer.
// it might be running on a different thread than the recording thread which means you must
// only write to the decoder_frame
static void decode_backend_cb( VkCommandBuffer cmd, void* user_data, void const* p_backend_frame_data ) {
	/*
	 *  Called by the backend while executing encoder command buffers.
	 */

	// we should only be allowed to access data that is within the current
	// frame of the decoder - we should only pass the current
	// decoder's frame as user_data - you cannot pass anything that
	// is a stack pointer to user_data as the stack will be long gone.
	//
	auto decoder_frame = static_cast<le_video_decoder_o::video_decoder_memory_frame*>( user_data );
	auto decoder       = decoder_frame->decoder;

	video_decode( decoder, cmd, decoder_frame, p_backend_frame_data );
}

// ----------------------------------------------------------------------
static void print_frame_state( std::vector<le_video_decoder_o::video_decoder_memory_frame> const& frames ) {
	auto state_to_string = [](
	                           le_video_decoder_o::video_decoder_memory_frame::State const& state ) -> const char* {
		static const char* str[] = {
		    "eIdle",
		    "eRecording",
		    "eDecodeSuccess",
		    "eDecodeFailed",
		};
		return str[ uint32_t( state ) ];
	};

	size_t i = 0;
	logger.info( "* * * * * * * * * * " );
	for ( auto const& f : frames ) {
		logger.info( "Memory frame: % 2d -> [% 20s], poc: % 10d, pts: % 10llu", i, state_to_string( f.state ), f.frame_info.poc, f.ticks_pts );
		i++;
	}
	logger.info( "* * * * * * * * * * " );
};

// ----------------------------------------------------------------------
/*
 * Decode Picture Order Count
 * (tig) see ITU-T H.264 (08/2021) pp.113
 *
 */
static void calculate_frame_info( h264::NALHeader const*   nal,
                                  h264::PPS const*         pps_array,
                                  h264::SPS const*         sps_array,
                                  h264::Bitstream*         bs,
                                  pic_order_count_state_t* prev,
                                  frame_info_t&            info ) {

	// tig: see Rec. ITU-T H.264 (08/2021) p.66 (7-1)
	h264::SliceHeader* slice_header = &info.slice_header; // TODO: clean this up
	h264::read_slice_header( slice_header, nal, pps_array, sps_array, bs );

	const h264::PPS& pps = pps_array[ slice_header->pic_parameter_set_id ];
	const h264::SPS& sps = sps_array[ pps.seq_parameter_set_id ];

	uint32_t max_frame_num         = uint32_t( 1 ) << uint32_t( ( sps.log2_max_frame_num_minus4 + 4 ) );
	int      max_pic_order_cnt_lsb = 1 << ( sps.log2_max_pic_order_cnt_lsb_minus4 + 4 );
	int      pic_order_cnt_lsb     = slice_header->pic_order_cnt_lsb;
	int      pic_order_cnt_msb     = 0;

	// tig: see Rec. ITU-T H.264 (08/2021) p.66 (7-1)
	bool idr_flag = ( nal->type == h264::NAL_UNIT_TYPE::NAL_UNIT_TYPE_CODED_SLICE_IDR ); // (7-1)

	switch ( sps.pic_order_cnt_type ) {

	case 0: {
		// TYPE 0
		// Rec. ITU-T H.264 (08/2021) page 114

		if ( idr_flag ) {
			prev->pic_order_cnt_msb = 0;
			prev->pic_order_cnt_lsb = 0;
			prev->poc_cycle++;
		}

		if ( ( pic_order_cnt_lsb < prev->pic_order_cnt_lsb ) &&
		     ( prev->pic_order_cnt_lsb - pic_order_cnt_lsb ) >= max_pic_order_cnt_lsb / 2 ) {
			pic_order_cnt_msb = prev->pic_order_cnt_msb + max_pic_order_cnt_lsb;
		} else if (
		    ( pic_order_cnt_lsb > prev->pic_order_cnt_lsb ) &&
		    ( pic_order_cnt_lsb - prev->pic_order_cnt_lsb ) > max_pic_order_cnt_lsb / 2 ) {
			pic_order_cnt_msb = prev->pic_order_cnt_msb - max_pic_order_cnt_lsb;
		} else {
			pic_order_cnt_msb = prev->pic_order_cnt_msb;
		}

		// Top and bottom field order count in case the picture is a field
		if ( !slice_header->bottom_field_flag ) {
			info.top_field_order_cnt = pic_order_cnt_msb + pic_order_cnt_lsb;
		}
		if ( !slice_header->field_pic_flag ) {
			info.bottom_field_order_cnt = info.top_field_order_cnt + slice_header->delta_pic_order_cnt_bottom;
		} else {
			info.bottom_field_order_cnt = pic_order_cnt_msb + slice_header->pic_order_cnt_lsb;
		}

		info.gop = prev->poc_cycle;

		//  TODO: check for memory management operation command 5

		if ( nal->idc != 0 ) {
			prev->pic_order_cnt_msb = pic_order_cnt_msb;
			prev->pic_order_cnt_lsb = pic_order_cnt_lsb;
		}
		break;
	}

	case 1: {
		// TYPE 1
		assert( false && "not implemented" );
		break;
	}
	case 2:
		// TYPE 2
		int frame_num_offset = 0;

		if ( idr_flag ) {
			frame_num_offset = 0;
		} else if ( prev->frame_num > slice_header->frame_num ) {
			frame_num_offset = prev->frame_offset + max_frame_num;
		} else {
			frame_num_offset = prev->frame_offset;
		}

		prev->frame_offset = frame_num_offset;
		prev->frame_num    = slice_header->frame_num;

		int tmp_pic_order_count = 0;

		if ( idr_flag ) {
			tmp_pic_order_count = 0;
		} else if ( nal->idc == h264::NAL_REF_IDC( 0 ) ) {
			tmp_pic_order_count = 2 * ( frame_num_offset + slice_header->frame_num ) - 1;
		} else {
			tmp_pic_order_count = 2 * ( frame_num_offset + slice_header->frame_num );
		}

		// Top and bottom field order count in case the picture is a field
		//
		//
		if ( !slice_header->field_pic_flag ) {
			info.top_field_order_cnt    = tmp_pic_order_count;
			info.bottom_field_order_cnt = tmp_pic_order_count;
		} else if ( slice_header->bottom_field_flag ) {
			info.bottom_field_order_cnt = tmp_pic_order_count;
		} else {
			info.top_field_order_cnt = tmp_pic_order_count;
		}

		// (tig) we don't care about bottom or top fields as we assume progressive
		// if it were otherwise, for interleaved either the top or the bottom
		// field shall be set - depending on whether the current picture is the
		// top or the bottom field as indicated by bottom_field_flag
		if ( tmp_pic_order_count == 0 ) {
			prev->poc_cycle++;
		}
		info.gop = prev->poc_cycle;

		break;
	}

	if ( !slice_header->field_pic_flag ) {
		// not a field pic - that means we assume the picture is a frame (it does not consist of bottom and top field)
		info.poc = std::min( info.top_field_order_cnt, info.bottom_field_order_cnt );
	} else if ( !slice_header->bottom_field_flag ) {
		info.poc = info.top_field_order_cnt;
		// top field
	} else {
		// bottom field
		info.poc = info.bottom_field_order_cnt;
	}

	// FIXME: some videos increment poc by 2 for every frame, some don't,
	// and there seems to be no really good way to tell.
	// the only thing we could do is to look at the current group of pictures
	// and then to sort them by poc.

	if constexpr ( false ) {
		logger.info( "info.poc: % 10d, msb: % 4d, lsb: % 4d, gop: % 10d, prev msb: % 4d, prev lsb: % 4d",
		             info.poc, pic_order_cnt_msb, pic_order_cnt_lsb, info.gop, prev->pic_order_cnt_msb, prev->pic_order_cnt_lsb );
	}

	// Accept frame beginning NAL unit:
	info.nal_ref_idc   = nal->idc;
	info.nal_unit_type = nal->type;
};

// ----------------------------------------------------------------------

static void copy_video_frame_bitstream_to_gpu_buffer(
    std::ifstream&                                  mp4_filestream,
    le_video_decoder_o::video_decoder_memory_frame* memory_frame,
    size_t                                          sample_index,
    MP4D_track_t*                                   track,
    pic_order_count_state_t*                        pic_order_count_state,
    h264::SPS const*                                sps_array,
    h264::PPS const*                                pps_array,
    uint32_t                                        poc_interval,
    frame_info_t*                                   last_i_frame_info ) {

	uint8_t* dst_buffer = memory_frame->gpu_bitstream_slice_mapped_memory_address + memory_frame->gpu_bitstream_used_bytes_count;

	uint64_t mp4_stream_offset = 0;
	uint64_t frame_num_bytes   = 0;
	uint64_t frame_timestamp   = 0;
	uint64_t frame_duration    = 0;

	{
		// Calculate position into file (or stream) for current frame
		//
		unsigned found_frame_index;
		int      nchunk = sample_to_chunk( track, sample_index, &found_frame_index ); // imported via minimp4

		if ( nchunk < 0 ) {
			assert( false && "something went wrong" );
		}

		mp4_stream_offset = track->chunk_offset[ nchunk ];

		for ( ; found_frame_index < sample_index; found_frame_index++ ) {
			mp4_stream_offset += track->entry_size[ found_frame_index ];
		}

		frame_num_bytes = track->entry_size[ found_frame_index ];
		frame_timestamp = track->timestamp[ found_frame_index ];
		frame_duration  = track->duration[ found_frame_index ];
	}

	mp4_filestream.seekg( mp4_stream_offset, std::ios_base::beg );

	while ( frame_num_bytes > 0 ) {

		// NOTE: The initial 4 bytes containing the frame size info will not be copied over to the dst frame.
		//
		// NOTE: It's important to keep src_buffer as uint8_t, otherwise signed
		// char will mess up the endianness transform (you might end up with
		// negative values)
		uint8_t src_buffer[ 4 ];
		mp4_filestream.read( ( char* )src_buffer, sizeof( src_buffer ) );

		uint32_t size = ( ( uint32_t )( src_buffer[ 0 ] ) << 24 ) |
		                ( ( uint32_t )( src_buffer[ 1 ] ) << 16 ) |
		                ( ( uint32_t )( src_buffer[ 2 ] ) << 8 ) |
		                src_buffer[ 3 ];
		size += 4;
		assert( frame_num_bytes >= size );

		// TODO: You might want to guard against EOF
		uint8_t nal_header_byte = mp4_filestream.peek();

		h264::Bitstream bs = {};
		bs.init( &nal_header_byte, sizeof( nal_header_byte ) );
		h264::NALHeader nal = {};
		h264::read_nal_header( &nal, &bs );

		// Skip over any frame data that is not idr slice or non-idr slice
		if ( nal.type == h264::NAL_UNIT_TYPE_CODED_SLICE_IDR ) {
			memory_frame->frame_info.frame_type = FrameType::eFrameTypeIntra;
		} else if ( nal.type == h264::NAL_UNIT_TYPE_CODED_SLICE_NON_IDR ) {
			memory_frame->frame_info.frame_type = FrameType::eFrameTypePredictive;
		} else {
			// Continue search for frame beginning NAL unit:
			frame_num_bytes -= size;
			mp4_filestream.seekg( size - 4, std::ios_base::cur );
			continue;
		}

		// we should parse frame information here - and place
		// header info into the current frame so that it will be available when we
		// hand the frame over to the gpu for encoding.
		//
		// ----------| Invariant: Frame is either of type eFrameTypeIntra or eFrameTypePredictive

		memory_frame->frame_info.slice_header = {};

		if ( memory_frame->gpu_bitstream_used_bytes_count + ( size - 4 ) + sizeof( h264::nal_start_code ) <= memory_frame->gpu_bitstream_capacity ) {
			memcpy( dst_buffer, h264::nal_start_code, sizeof( h264::nal_start_code ) );
			mp4_filestream.read( ( char* )( dst_buffer + sizeof( h264::nal_start_code ) ), size - 4 );

			// Init byte stream to the full size of whatever we copied to the gpu
			bs.init( dst_buffer + sizeof( h264::nal_start_code ), size - 4 );
			h264::read_nal_header( &nal, &bs );
			// Update slice header and poc, gop data from coded data
			calculate_frame_info( &nal, pps_array, sps_array, &bs, pic_order_count_state, memory_frame->frame_info );

			bool idr_flag = ( nal.type == h264::NAL_UNIT_TYPE::NAL_UNIT_TYPE_CODED_SLICE_IDR ); // (7-1)

			if ( idr_flag ) {
				// reset the timestamp for last i-frame
				*last_i_frame_info                        = memory_frame->frame_info;
				last_i_frame_info->pts_in_timescale_units = frame_timestamp;
			}

			assert( last_i_frame_info->gop == memory_frame->frame_info.gop );

			// Calculate presentation time stamp:
			//
			// (Timecode of last i-frame) + (presentation order count of the current frame / poc_interval) * (frame duration)
			//
			// poc_interval is calculated based on a heuristic during demux_h264_data.
			//
			le::Ticks pts =
			    video_time_to_ticks(
			        ( last_i_frame_info->pts_in_timescale_units ) +
			            ( frame_duration * ( memory_frame->frame_info.poc / poc_interval ) ),
			        track->timescale );

			memory_frame->frame_info.duration_in_timescale_units = frame_duration;
			memory_frame->ticks_pts                              = pts;                                                     // presentation time stamp
			memory_frame->ticks_duration                         = video_time_to_ticks( frame_duration, track->timescale ); // duration
			memory_frame->gpu_bitstream_used_bytes_count += size;

		} else {
			logger.error( "Cannot copy frame data into frame bitstream - out of memory. Frame capacity: %d, frame current size %d, extra size: %d",
			              memory_frame->gpu_bitstream_capacity, memory_frame->gpu_bitstream_used_bytes_count, size );
		}
		break;
	}

	// Record picture order count into memory frame - so that
	// the playhead may pick from the most recent decoded frame.
}

// ----------------------------------------------------------------------
// Only call this once per app update cycle!
static void le_video_decoder_update( le_video_decoder_o* self, le_rendergraph_o* rendergraph, uint64_t ticks ) {

	if constexpr ( false ) {
		print_frame_state( self->memory_frames );
	}

	int64_t total_ticks          = self->video_data->duration_in_ticks.count();
	size_t  count_decode_success = 0;

	// Signal for whether we want to trigger the playback complete callback at the end of the update method.
	// we don't immediately trigger the callback, as we first want to finish with the update method before
	// allowing control to go back to the user.
	bool wants_on_playback_complete_callback = false;

	for ( auto const& f : self->memory_frames ) {
		if ( f.state == MemoryFrameState::eDecodeSuccess ) {
			count_decode_success++;
		}
	}

	// Amount of time we must add to start in case our playhead is paused
	// so that it stays on the same relative position.

	auto pause_delta = le::Ticks( ticks ) - self->ticks_at_last_update;

	switch ( self->playback_state ) {
	case ( le_video_decoder_o::PlaybackState::eInitial ): {
		self->ticks_at_start       = le::Ticks( ticks );
		self->ticks_at_last_update = le::Ticks( ticks );
		break;
	}
	case ( le_video_decoder_o::PlaybackState::ePause ): {
		self->ticks_at_start += pause_delta;
		self->ticks_at_last_update = le::Ticks( ticks );
		break;
	}
	case ( le_video_decoder_o::PlaybackState::eSeeking ): {
		self->ticks_at_start += pause_delta;
		self->ticks_at_last_update = le::Ticks( ticks );
		if ( count_decode_success == self->memory_frames.size() ) {
			self->playback_state = le_video_decoder_o::PlaybackState::ePause;
		}
		break;
	}
	case ( le_video_decoder_o::PlaybackState::ePlay ): {
		self->ticks_at_last_update = le::Ticks( ticks );
		break;
	}
	default:
		break;
	}

	auto previous_ticks_at_playhead = self->ticks_at_playhead;

	self->ticks_at_playhead =
	    ( ( self->ticks_at_last_update -
	        self->ticks_at_start +
	        self->ticks_seek_offset +
	        self->video_data->duration_in_ticks ) ) %
	    self->video_data->duration_in_ticks;

	// Detect if the playhead wrapped around - if it does, this tells us that a playthrough has been completed.
	//
	if ( self->ticks_at_playhead < previous_ticks_at_playhead && self->playback_state == le_video_decoder_o::ePlay ) {

		if ( self->is_playback_not_looping ) {
			// In case we're playing without looping, we want to stop the playhead from wrapping around.
			//
			// We restore the playhead position, and we additionally set ticks_at_start to the position it
			// would have been if `le_video_decoder_update` would have started with the player in ePause
			// state.
			//
			self->ticks_at_start += pause_delta;
			self->ticks_at_playhead = previous_ticks_at_playhead;
			self->playback_state    = le_video_decoder_o::ePause;
		}

		// If you wanted to trigger a callback on reaching the end of the movie, this would be the place to do so.
		if constexpr ( false ) {
			logger.info( "Reached end of movie." );
		}

		wants_on_playback_complete_callback = true;
	}

	int64_t delta_ticks = self->ticks_at_playhead.count();

	if constexpr ( false ) {
		logger.info( "Update. current delta time : %f", std::chrono::duration<double>( self->ticks_at_playhead ).count() );
		logger.info( "Update. current delta ticks: %d", ( self->ticks_at_playhead ).count() );
	}

	// Find the first frame that is idle - change its state
	// to recording and update the decoder's recording index
	// to that frame.

	// Record video decoding commands into rendergraph.
	auto rg = le::RenderGraph( rendergraph );

	{
		// Declare the output image - it belongs to the rendergraph
		le_resource_info_t const output_image_info =
		    le::ImageInfoBuilder()
		        .addUsageFlags( { le::ImageUsageFlagBits::eTransferDst | le::ImageUsageFlagBits::eSampled } )
		        .setExtent( self->video_data->width, self->video_data->height )
		        .setFormat( le::Format( self->properties.format_properties.format ) )
		        .build();

		for ( auto const& frame : self->memory_frames ) {
			rg.declareResource( frame.rendergraph_image_resource, output_image_info );
		}
	}

	// Find the closest DECODED frame up to and including the current playback time.
	// we don't touch any frames that are not yet decoded. If we find a frame that is
	// more than its duration in the past, we set its state to Idle, so that it can
	// 	be recycled.
	{

		int32_t closest_decoded_frame_idx = self->latest_memory_frame_available_for_rendering;
		// int32_t closest_decoded_frame_idx = -1;
		int64_t closest_offset = std::numeric_limits<int64_t>::max();

		for ( int idx = 0; idx != self->memory_frames.size(); idx++ ) {
			auto const& f = self->memory_frames[ idx ];

			// we want to skip over frames that are potentially still rendering
			if ( f.state != MemoryFrameState::eDecodeSuccess ) {
				continue;
			}

			// In case we are seeking, exclude the frame that is currently on display-
			// we will only update it once seeking has completed, in which case
			// state will move to ePaused.
			//
			// We do this so that we don't see any visible frame updates until
			// the seeking process has completed.
			//
			if ( self->playback_state == le_video_decoder_o::eSeeking &&
			     idx == self->latest_memory_frame_available_for_rendering ) {
				continue;
			}

			// We want to know how far the frame pts is from our current playhead.
			//
			// Our current playhead position is the origin - which is why we do an inverse
			// origin transform here by subtracting to the origin.
			int64_t frame_ticks_relative_to_playhead = ( total_ticks - delta_ticks + int64_t( f.ticks_pts.count() ) ) % total_ticks;
			int64_t frame_duration                   = f.ticks_duration.count();

			// We want a complement-notation, so that things that are too far away get displayed as negative
			// instead of high positive. This is useful in case we are looping and the playhead is wrapping around,
			// in which case this helps us to preserve the relative distance of the playhead to frames which
			// were at the very end of the video stream.

			// A good way to think of this is to think of the modulo domain as a circle with a positive right
			// and a negative left half.
			// Imagine a clock where we want to have our playhead at 0 hours, and anything until 6 be at a positive offset,
			// and anything between 6 and 12 be expressed in negative offsets to 0:
			//
			//              0
			//          -3     3
			//              6
			//
			// In our case the size of the domain is total number of ticks for the current video.

			if ( frame_ticks_relative_to_playhead > total_ticks / 2 ) {
				frame_ticks_relative_to_playhead -= total_ticks;
			}

			// current frame time relative to playhead

			// if cft is < -duration, then the playhead has left the frame
			// if cft is >= -duration

			if constexpr ( false ) {
				logger.info( "mem frame #% 2d : % 7d", idx, frame_ticks_relative_to_playhead );
			}

			// -1          0          1          2          3     // frame duration offsets to playhead position
			// ---|--------x-|----------|----------|----------|-- // frame boundaries
			//             ^
			//             |- playhead
			//

			// Note that future frames might be decoded out-of sequence,
			// thus appearing long before they will end up being displayed.

			// We want everything up to half a duration be positive,
			// and everything from half a duration in difference to be
			// negative

			const int64_t GOP_SIZE = 32; // Maximum distance in sequence that decoded frames are allowed
			                             // to be, while still considered part of the same group of pictures.
			                             //
			                             // This should be calculated using max_pic_order_cnt_lsb

			if ( frame_ticks_relative_to_playhead >= -frame_duration ) {
				// This frame starts somewhere after one full frame duration before the playead
				if ( frame_ticks_relative_to_playhead < closest_offset ) {

					if ( closest_decoded_frame_idx != self->latest_memory_frame_available_for_rendering &&
					     closest_offset > frame_duration * ( GOP_SIZE ) ) {
						self->memory_frames[ closest_decoded_frame_idx ].state = MemoryFrameState::eIdle;
					}

					closest_decoded_frame_idx = idx;
					closest_offset            = frame_ticks_relative_to_playhead;
				} else if ( std::abs( frame_ticks_relative_to_playhead ) > frame_duration * ( GOP_SIZE ) ) {

					// This frame is more than one full GOP size in the future (it can't be in the past
					// because of the parent condition)
					self->memory_frames[ idx ].state = MemoryFrameState::eIdle;
				}
			} else if ( frame_ticks_relative_to_playhead < -frame_duration ) {
				// This frame began before our playhead, and it ended (start+duration) before our playhead
				// it is therefore in the playhead's past and can be recycled.
				if ( closest_decoded_frame_idx != self->latest_memory_frame_available_for_rendering ) {
					self->memory_frames[ idx ].state = MemoryFrameState::eIdle;
				} else {
					closest_decoded_frame_idx = idx;
				}
			} else {
				logger.info( "could not find closest frame." );
			}

			// logger.info( "offset: %d", closest_offset );
		}

		// Only update the currently visible frame if we are not seeking.
		if ( self->playback_state != le_video_decoder_o::eSeeking ) {
			self->latest_memory_frame_available_for_rendering = closest_decoded_frame_idx;
		}

		//		logger.info( "rendering frame: % 2d", self->latest_memory_frame_available_for_rendering );

		if constexpr ( false ) {
			logger.info( "closest frame: %d, %10ul",
			             closest_decoded_frame_idx,
			             self->memory_frames[ closest_decoded_frame_idx ].ticks_pts );
		}
	}

	if constexpr ( false ) {
		print_frame_state( self->memory_frames );
		logger.info( "\n" );
	}

	if constexpr ( false ) {
		if ( self->latest_memory_frame_available_for_rendering > -1 ) {
			logger.info( "current visible frame [%d] poc: % 8d",
			             self->latest_memory_frame_available_for_rendering,
			             self->memory_frames[ self->latest_memory_frame_available_for_rendering ].frame_info.poc );
		}
	}

	// Find the first idle or failed frame and set self->memory_frame_idx_recording to its index
	self->memory_frame_idx_recording = -1;

	for ( size_t i = 0; i != self->memory_frames.size(); i++ ) {
		if ( self->memory_frames[ i ].state == MemoryFrameState::eIdle ||
		     self->memory_frames[ i ].state == MemoryFrameState::eDecodeFailed ) {
			self->memory_frame_idx_recording = i;
			self->memory_frames[ i ].state   = MemoryFrameState::eRecording;
			break;
		}
	}

	// Set up a decode renderpass - if one is needed...

	if ( self->memory_frame_idx_recording >= 0 ) {

		auto& recording_memory_frame = self->memory_frames[ self->memory_frame_idx_recording ];

		recording_memory_frame.decoded_frame_index = self->current_decoded_frame;

		// Here, we want to upload the data for the currently recorded frame to the bitstream.
		// we can do this without invoking any vulkan commands as the memory is mapped.

		copy_video_frame_bitstream_to_gpu_buffer(
		    self->mp4_filestream,
		    &recording_memory_frame,
		    recording_memory_frame.decoded_frame_index,
		    &self->mp4_demux.track[ self->video_data->video_track_id ],
		    &self->pic_order_count_state,
		    ( h264::SPS* )self->video_data->sps_bytes.data(),
		    ( h264::PPS* )self->video_data->pps_bytes.data(),
		    self->video_data->poc_interval,
		    &self->last_i_frame_info );

		// align memory for good measure
		recording_memory_frame.gpu_bitstream_used_bytes_count =
		    align_to( recording_memory_frame.gpu_bitstream_used_bytes_count, self->properties.capabilities.minBitstreamBufferSizeAlignment );

		// we need to update the size value

		auto rp = le::RenderPass( "video decode", le::QueueFlagBits::eVideoDecodeBitKhr );

		le_img_resource_handle current_output_image =
		    recording_memory_frame.rendergraph_image_resource;

		rp.useImageResource(
		    current_output_image,
		    le::AccessFlagBits2::eNone,         // previous image contents will be ignored
		    le::AccessFlagBits2::eTransferWrite // last image access before handing image over will be a transfer write
		);

		rp.setExecuteCallback( self, []( le_command_buffer_encoder_o* encoder, void* user_data ) {
			/*
			 *  Called by the Rendergraph while recording renderpasses.
			 */
			auto decoder = static_cast<le_video_decoder_o*>( user_data );
			// we can now access decoder - this gets executed during record on the main thread
			le_video_decoder_o::video_decoder_memory_frame* mem_frame = &decoder->memory_frames[ decoder->memory_frame_idx_recording ];

			// &mem_frame contains the context for the callback once it gets executed by the backend.
			le_renderer::encoder_video_decoder_i.execute_callback( encoder, decode_backend_cb, mem_frame );
		} );

		// Mark this renderpass to be a root pass,  so that it will
		// not get optimised away in case none of its resources are
		// used in the same frame.
		rp.setIsRoot( true );

		// Add to the current rendergraph: we add the renderpass,
		// and we declare any resources that we might use and share
		// with the wider rendergraph, such as the target image
		rg.addRenderPass( rp );

		{
			// Register the video decoder as an object that needs to be life-time-managed by frame lifetime
			//
			// We cache this here so that we only have to do the lookup to the forwarder
			// once for every time this compilation unit reloads
			static auto cb_addr   = le_core_forward_callback( le_video_decoder::le_video_decoder_i.on_backend_frame_clear_cb );
			auto&       mem_frame = self->memory_frames[ self->memory_frame_idx_recording ];

			le_on_frame_clear_callback_data_t callback_data{
			    .cb_fun    = cb_addr,
			    .user_data = &mem_frame,
			};

			self->reference_count++;

			le_renderer_api_i->le_rendergraph_i.add_on_frame_clear_callbacks( rg, &callback_data, 1 );
		}

		self->current_decoded_frame = ( self->current_decoded_frame + 1 ) % self->video_data->num_frames;

	} else if ( self->playback_state == le_video_decoder_o::eInitial ) {
		// in case the player was only just initialized,
		// once there are no more frames to decode, we can set the player to pause state
		self->playback_state = le_video_decoder_o::ePause;
	}

	// If we just rendered the last frame and we have a complete callback set,
	// then trigger this callback - this should be the last thing you do, as it
	// potentially hands control back to the user.

	if ( wants_on_playback_complete_callback && self->on_playback_complete_callback ) {
		self->on_playback_complete_callback( self, self->on_playback_complete_callback_userdata );
	}
}

// ----------------------------------------------------------------------
static le_img_resource_handle le_video_decoder_get_latest_available_frame( le_video_decoder_o* self ) {
	if ( self->latest_memory_frame_available_for_rendering < 0 ) {
		logger.warn( "No frame available yet." );
		return nullptr;
	}
	if ( false ) {
		logger.info( "showing frame: % 2d, % 10d",
		             self->latest_memory_frame_available_for_rendering,
		             self->memory_frames[ self->latest_memory_frame_available_for_rendering ].ticks_pts );
	}
	// --------| invariant: there is a frame available.
	return self->memory_frames[ self->latest_memory_frame_available_for_rendering ].rendergraph_image_resource;
}

// ----------------------------------------------------------------------

bool le_video_decoder_get_latest_available_frame_index( le_video_decoder_o* self, uint64_t* frame_index ) {
	if ( self->latest_memory_frame_available_for_rendering < 0 ) {
		logger.warn( "No frame available yet." );
		return false;
	}
	if ( frame_index ) {
		*frame_index = self->memory_frames[ self->latest_memory_frame_available_for_rendering ].frame_info.poc;
	}
	// --------| invariant: there is a frame available.
	return true;
}

// ----------------------------------------------------------------------

static constexpr char const* vk_err_to_c_str( const VkResult& tp ) {
	switch ( static_cast<int32_t>( tp ) ) {
		// clang-format off
	case         -1: return "VkErrorOutOfHostMemory";
	case        -10: return "VkErrorTooManyObjects";
	case -1000000000: return "VkErrorSurfaceLostKhr";
	case -1000000001: return "VkErrorNativeWindowInUseKhr";
	case -1000001004: return "VkErrorOutOfDateKhr";
	case -1000003001: return "VkErrorIncompatibleDisplayKhr";
	case -1000011001: return "VkErrorValidationFailedExt";
	case -1000012000: return "VkErrorInvalidShaderNv";
	case -1000069000: return "VkErrorOutOfPoolMemory";
	case -1000072003: return "VkErrorInvalidExternalHandle";
	case -1000158000: return "VkErrorInvalidDrmFormatModifierPlaneLayoutExt";
	case -1000161000: return "VkErrorFragmentation";
	case -1000174001: return "VkErrorNotPermittedKhr";
	case -1000255000: return "VkErrorFullScreenExclusiveModeLostExt";
	case -1000257000: return "VkErrorInvalidOpaqueCaptureAddress";
	case        -11: return "VkErrorFormatNotSupported";
	case        -12: return "VkErrorFragmentedPool";
	case        -13: return "VkErrorUnknown";
	case         -2: return "VkErrorOutOfDeviceMemory";
	case         -3: return "VkErrorInitializationFailed";
	case         -4: return "VkErrorDeviceLost";
	case         -5: return "VkErrorMemoryMapFailed";
	case         -6: return "VkErrorLayerNotPresent";
	case         -7: return "VkErrorExtensionNotPresent";
	case         -8: return "VkErrorFeatureNotPresent";
	case         -9: return "VkErrorIncompatibleDriver";
	case          0: return "VkSuccess";
	case          1: return "VkNotReady";
	case          2: return "VkTimeout";
	case          3: return "VkEventSet";
	case          4: return "VkEventReset";
	case          5: return "VkIncomplete";
	case 1000001003: return "VkSuboptimalKhr";
	case 1000268000: return "VkThreadIdleKhr";
	case 1000268001: return "VkThreadDoneKhr";
	case 1000268002: return "VkOperationDeferredKhr";
	case 1000268003: return "VkOperationNotDeferredKhr";
	case 1000297000: return "VkPipelineCompileRequired";
	default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------
/// \brief   file loader utility method
/// \details loads file given by filepath and returns a vector of chars if successful
/// \note    returns an empty vector if not successful
static std::vector<uint8_t> load_file( const std::filesystem::path& file_path, bool* success ) {
	static_assert( sizeof( char ) == sizeof( uint8_t ), "char and uint8_t must have same size." );

	std::vector<uint8_t> contents;
	size_t               fileSize = 0;
	std::ifstream        file( file_path, std::ios::in | std::ios::binary | std::ios::ate );

	if ( !file.is_open() ) {
		logger.error( "Unable to open file: '%s'", std::filesystem::canonical( file_path ).c_str() );
		*success = false;
		return contents;
	}

	// ----------| invariant: file is open, file seeker is std::ios::ate (at end)

	auto endOfFilePos = file.tellg();

	if ( endOfFilePos >= 0 ) {
		fileSize = size_t( endOfFilePos );
	} else {
		*success = false;
		file.close();
		return contents;
	}

	// ----------| invariant: file has some bytes to read
	contents.resize( fileSize );

	file.seekg( 0, std::ios::beg );
	file.read( ( char* )contents.data(), endOfFilePos );
	file.close();

	*success = true;
	return contents;
}

// ----------------------------------------------------------------------
// demux data stream and store result into le_video_data_h264_t
static int demux_h264_data( std::ifstream& input_file, size_t input_size, le_video_data_h264_t* video, MP4D_demux_t* mp4 ) {
	int         spspps_bytes;
	const void* spspps;

	struct read_callback_user_data_t {
		std::ifstream* stream;
		size_t         last_offset; // last absolute offset given in bytes.
		                            // we store this so that we can calculate
		                            // and use relative offsets to move the
		                            // filepointer forward
	};

	auto read_callback = []( int64_t offset, void* buffer, size_t size, void* user_data ) -> int {
		read_callback_user_data_t* data         = static_cast<read_callback_user_data_t*>( user_data );
		size_t                     offset_delta = offset - data->last_offset;

		data->stream->seekg( offset_delta, std::ios_base::cur );
		data->stream->read( ( char* )buffer, size );

		// store last offset, so that we can calculate the next offset_delta
		data->last_offset = offset + size;

		return data->stream->eof();
	};

	read_callback_user_data_t read_callback_user_data{
	    .stream      = &input_file,
	    .last_offset = 0,
	};

	MP4D_open( mp4, read_callback, &read_callback_user_data, input_size );

	video->title   = std::string( mp4->tag.title == nullptr ? "" : ( char* )mp4->tag.title );
	video->album   = std::string( mp4->tag.album == nullptr ? "" : ( char* )mp4->tag.album );
	video->artist  = std::string( mp4->tag.artist == nullptr ? "" : ( char* )mp4->tag.artist );
	video->year    = std::string( mp4->tag.year == nullptr ? "" : ( char* )mp4->tag.year );
	video->comment = std::string( mp4->tag.comment == nullptr ? "" : ( char* )mp4->tag.comment );
	video->genre   = std::string( mp4->tag.genre == nullptr ? "" : ( char* )mp4->tag.genre );

	// for ( int ntrack = 0; ntrack < mp4.track_count; ntrack++ )
	{
		int           ntrack       = 0; // NOTE: is it possible to have videos with more than one track? YES: but only one track will be video - other tracks may be audio etc.
		MP4D_track_t& track        = mp4->track[ ntrack ];
		unsigned      sum_duration = 0;
		int           i            = 0;
		if ( track.handler_type == MP4D_HANDLER_TYPE_VIDE ) { // assume h264

			switch ( track.object_type_indication ) {
			case MP4_OBJECT_TYPE_AVC:
				// all good
				video->video_profile  = le_video_data_h264_t::VideoProfile::VideoProfileAvc;
				video->video_track_id = ntrack;
				video->num_frames     = track.sample_count;
				break;
			case MP4_OBJECT_TYPE_HEVC:
				logger.error( "h.265 (HEVC) is not yet implemented for decode." );
				exit( -1 );
				break;
			default:
				logger.error( "could not decode track object %du", track.object_type_indication );
				exit( -1 );
			}

			{
				void const* data  = nullptr;
				int         size  = 0;
				int         index = 0;

				while ( ( data = MP4D_read_sps( mp4, ntrack, index, &size ) ) ) {

					const uint8_t* sps_data = ( const uint8_t* )data;

					h264::Bitstream bs = {};
					bs.init( sps_data, size );
					h264::NALHeader nal = {};
					h264::read_nal_header( &nal, &bs );
					assert( nal.type = h264::NAL_UNIT_TYPE_SPS );

					h264::SPS sps = {};
					h264::read_sps( &sps, &bs );

					// Some validation checks that data parsing returned expected values:
					// https://stackoverflow.com/questions/6394874/fetching-the-dimensions-of-a-h264video-stream
					uint32_t width  = ( ( sps.pic_width_in_mbs_minus1 + 1 ) * 16 ) - sps.frame_crop_left_offset * 2 - sps.frame_crop_right_offset * 2;
					uint32_t height = ( ( 2 - sps.frame_mbs_only_flag ) * ( sps.pic_height_in_map_units_minus1 + 1 ) * 16 ) - ( sps.frame_crop_top_offset * 2 ) - ( sps.frame_crop_bottom_offset * 2 );
					assert( track.SampleDescription.video.width == width );
					assert( track.SampleDescription.video.height == height );
					video->padded_width  = ( sps.pic_width_in_mbs_minus1 + 1 ) * 16;
					video->padded_height = ( sps.pic_height_in_map_units_minus1 + 1 ) * 16;
					video->num_dpb_slots = std::max( video->num_dpb_slots, uint32_t( sps.num_ref_frames * 2 + 1 ) ); // notice that a frame is a complete picture (consists of two fields when interlaced, or one picture if progressive)

					video->sps_bytes.resize( video->sps_bytes.size() + sizeof( sps ) );
					memcpy( ( h264::SPS* )video->sps_bytes.data() + video->sps_count, &sps, sizeof( sps ) );
					video->sps_count++;
					index++;
				}
			}

			{
				const void* data  = nullptr;
				int         size  = 0;
				int         index = 0;

				while ( ( data = MP4D_read_pps( mp4, ntrack, index, &size ) ) ) {
					const uint8_t* pps_data = ( const uint8_t* )data;

					h264::Bitstream bs = {};
					bs.init( pps_data, size );
					h264::NALHeader nal = {};
					h264::read_nal_header( &nal, &bs );
					assert( nal.type = h264::NAL_UNIT_TYPE_PPS );

					h264::PPS pps = {};
					h264::read_pps( &pps, &bs );
					video->pps_bytes.resize( video->pps_bytes.size() + sizeof( pps ) );
					memcpy( ( h264::PPS* )video->pps_bytes.data() + video->pps_count, &pps, sizeof( pps ) );
					video->pps_count++;
					index++;
				}
			}

			h264::PPS const* pps_array = reinterpret_cast<h264::PPS const*>( video->pps_bytes.data() );
			h264::SPS const* sps_array = reinterpret_cast<h264::SPS const*>( video->sps_bytes.data() );

			video->width     = track.SampleDescription.video.width;
			video->height    = track.SampleDescription.video.height;
			video->bit_rate  = track.avg_bitrate_bps;
			video->timescale = track.timescale;

			// track timescale is given as fraction of one second
			double timescale_rcp = 1.0 / double( track.timescale );

			{
				// Calculate POC interval

				// This is the interval between two successive Picture Order Counts. The interval
				// is often 2, but could also be 1. Once we know the interval, we can calculate
				// the time between two successive frames by looking at their POCs.

				uint64_t input_file_position = 0;          // current position in input file
				input_file.seekg( 0, std::ios_base::beg ); // return file position to 0

				// Get the POC (=picture order count) for the first n frames where n is num_dpb_slots +1.
				//
				// Once we have a list of frames like this, we can place them in order, and the
				// difference between the first two elements will be the standard POC increment between successive frames.
				// we can later use this standard increment to calculate PTS (=presentation time stamp) from poc.
				//
				// This assumes that the poc increases in constant intervals.

				pic_order_count_state_t pic_order_count_state = {};
				std::vector<uint64_t>   gop_pocs;
				gop_pocs.reserve( video->num_dpb_slots + 1 );

				for ( uint32_t sample_idx = 0; ( sample_idx < video->num_dpb_slots + 1 ) && sample_idx != track.sample_count; sample_idx++ ) {

					uint32_t           frame_bytes, timestamp, duration;
					MP4D_file_offset_t ofs = MP4D_frame_offset( mp4, ntrack, sample_idx, &frame_bytes, &timestamp, &duration );

					std::vector<uint8_t> src_buffer_data( frame_bytes );
					uint8_t*             src_buffer = src_buffer_data.data();

					if ( ofs - input_file_position != 0 ) {
						input_file.seekg( ofs - input_file_position, std::ios_base::cur );
					}
					if ( input_file.eof() ) {
						logger.error( "input file failed" );
					}
					// read a whole frame's worth of data into src_buffer_data
					input_file.read( ( char* )src_buffer, frame_bytes );

					input_file_position = ofs + frame_bytes;
					frame_info_t info   = {};

					while ( frame_bytes > 0 ) {

						uint32_t size = ( ( uint32_t )src_buffer[ 0 ] << 24 ) |
						                ( ( uint32_t )src_buffer[ 1 ] << 16 ) |
						                ( ( uint32_t )src_buffer[ 2 ] << 8 ) |
						                src_buffer[ 3 ];
						size += 4;
						assert( frame_bytes >= size );

						h264::Bitstream bs = {};
						bs.init( &src_buffer[ 4 ], frame_bytes - 4 );
						h264::NALHeader nal = {};
						h264::read_nal_header( &nal, &bs );

						if ( nal.type == h264::NAL_UNIT_TYPE_CODED_SLICE_IDR ) {
							info.frame_type = FrameType::eFrameTypeIntra;
						} else if ( nal.type == h264::NAL_UNIT_TYPE_CODED_SLICE_NON_IDR ) {
							info.frame_type = FrameType::eFrameTypePredictive;
						} else {
							// Continue search for frame beginning NAL unit:
							frame_bytes -= size;
							src_buffer += size;
							continue;
						}

						// ----------| Invariant: Frame is either of type eFrameTypeIntra or eFrameTypePredictive

						calculate_frame_info( &nal, pps_array, sps_array, &bs, &pic_order_count_state, info );
						gop_pocs.emplace_back( ( uint64_t( info.gop ) << 32 ) | uint64_t( info.poc ) );

						break;
					}
				}

				if ( gop_pocs.size() < 2 ) {
					video->poc_interval = 0;
				} else {
					std::sort( gop_pocs.begin(), gop_pocs.end() );
					video->poc_interval = gop_pocs[ 1 ] - gop_pocs[ 0 ];
				}

				logger.info( "poc_interval: %d", video->poc_interval );
			}

			input_file.seekg( 0, std::ios_base::beg ); // return file position to 0
			uint64_t track_duration = track.timestamp[ track.sample_count - 1 ] + track.duration[ track.sample_count - 1 ];

			video->max_memory_frame_size_bytes = video->padded_width * video->padded_height * sizeof( uint8_t ) * 3; // we're pessimistic: uncompressed size should never be more than rgb * width * height, right?
			video->average_frames_per_second   = float( double( track.timescale ) / double( track_duration ) * track.sample_count );
			video->duration_in_seconds         = float( double( track_duration ) * timescale_rcp );
			video->duration_in_timescale_units = track_duration;
			video->duration_in_ticks           = video_time_to_ticks( track_duration, track.timescale );

		} else if ( track.handler_type == MP4D_HANDLER_TYPE_SOUN ) { // assume aac
			// NOTE: AUDIO IS NOT YET IMPLEMENTED
			for ( i = 0; i < mp4->track[ ntrack ].sample_count; i++ ) {
				unsigned           frame_bytes, timestamp, duration;
				MP4D_file_offset_t ofs = MP4D_frame_offset( mp4, ntrack, i, &frame_bytes, &timestamp, &duration );
				logger.info( "ofs=%d frame_bytes=%d timestamp=%d duration=%d\n", ( unsigned )ofs, frame_bytes, timestamp, duration );
			}
		}
	}

	return 0;
}

// ----------------------------------------------------------------------
void le_video_decoder_play( le_video_decoder_o* self );

void le_video_decoder_set_pause_state( le_video_decoder_o* self, bool should_pause ) {
	if ( true == should_pause && self->playback_state == le_video_decoder_o::ePlay ) {
		self->playback_state = le_video_decoder_o::ePause;
	} else if ( false == should_pause ) {
		le_video_decoder_play( self );
	}
}
// ----------------------------------------------------------------------
bool le_video_decoder_get_pause_state( le_video_decoder_o* self ) {
	if ( self->playback_state == le_video_decoder_o::ePause ) {
		return true;
	} else {
		return false;
	}
};

// ----------------------------------------------------------------------

static bool get_i_frame_earlier_or_equal_to_given_frame( std::ifstream& mp4_filestream, MP4D_track_t* track, uint64_t* sample_index, uint64_t* maybe_timestamp_in_ticks ) {

	for ( ; *sample_index > 0; ( *sample_index )-- ) {

		uint64_t mp4_stream_offset = 0;
		uint64_t frame_num_bytes   = 0;

		// Calculate position into file (or stream) for current frame
		//
		unsigned found_frame_index;
		int      nchunk = sample_to_chunk( track, *sample_index, &found_frame_index ); // imported via minimp4

		if ( nchunk < 0 ) {
			assert( false && "something went wrong" );
		}

		mp4_stream_offset = track->chunk_offset[ nchunk ];

		for ( ; found_frame_index < *sample_index; found_frame_index++ ) {
			mp4_stream_offset += track->entry_size[ found_frame_index ];
		}

		// invariant: *sample_index == found_frame_index

		frame_num_bytes = track->entry_size[ found_frame_index ];
		mp4_filestream.seekg( mp4_stream_offset, std::ios_base::beg );

		while ( frame_num_bytes > 0 ) {

			// NOTE: The initial 4 bytes containing the frame size info will not be copied over to the dst frame.
			//
			// NOTE: It's important to keep src_buffer as uint8_t, otherwise signed
			// char will mess up the endianness transform (you might end up with
			// negative values)
			uint8_t src_buffer[ 4 ];
			mp4_filestream.read( ( char* )src_buffer, sizeof( src_buffer ) );

			uint32_t size = ( ( uint32_t )( src_buffer[ 0 ] ) << 24 ) |
			                ( ( uint32_t )( src_buffer[ 1 ] ) << 16 ) |
			                ( ( uint32_t )( src_buffer[ 2 ] ) << 8 ) |
			                src_buffer[ 3 ];
			size += 4;
			assert( frame_num_bytes >= size );

			// TODO: You might want to guard against EOF
			uint8_t nal_header_byte = mp4_filestream.peek();

			h264::Bitstream bs = {};
			bs.init( &nal_header_byte, sizeof( nal_header_byte ) );
			h264::NALHeader nal = {};
			h264::read_nal_header( &nal, &bs );

			// Skip over any frame data that is not idr slice or non-idr slice
			if ( nal.type == h264::NAL_UNIT_TYPE_CODED_SLICE_IDR ) {
				*maybe_timestamp_in_ticks = video_time_to_ticks_count( track->timestamp[ found_frame_index ], track->timescale );
				return true;
			} else if ( nal.type == h264::NAL_UNIT_TYPE_CODED_SLICE_NON_IDR ) {
				break;
			} else {
				// Continue search for frame beginning NAL unit:
				frame_num_bytes -= size;
				mp4_filestream.seekg( size - 4, std::ios_base::cur );
				continue;
			}
		}
	}
	return false;
}

// ----------------------------------------------------------------------
// Seek to position indicated with target_ticks.
// Returns true if this position could be found.
static bool le_video_decoder_seek( le_video_decoder_o* self, uint64_t target_ticks, bool should_resume_at_latest_i_frame ) {

	auto previous_seek_offset    = self->ticks_seek_offset;
	auto playhead_without_offset = self->ticks_at_playhead - self->ticks_seek_offset;
	auto playhead_target         = le::Ticks( target_ticks );

	// We need to seek until we find an i-frame that has a lower (or equal!) timecode than the one that we
	// currently require. If the seek target timestamp refers to a P-frame, we might have to produce (and
	// throw away) a few frames, as we must decode all intermediary frames until we meet the final seek
	// timecode.

	// Q: Does it matter that frames are iterated in decode (stream) and not in playback order as frames will
	//    most likely be presented in a different order as they are decoded?
	//
	// A: No. We only look at i-frames and i-frames must be in monotonic increasing order - the occurrance
	//    of an i-frame or a frame with nal_unit_type == 5 (IDR, "immediate decoder reset") causes the dpb
	//    to clear, this implies that each i-frame marks the beginning of a new sub-sequence of frames.

	self->pic_order_count_state = {}; // reset pic order count state.

	// RECIPE:
	//
	// 1. Find the index of the frame that has the closest timestamp to the target timestamp.
	// 2. Find the i-frame that is, or precedes the frame at this index, since we cannot
	//    directly jump to p-frames, and, anyway, p-frame timecodes may be incorrect because
	//    of re-ordering. We know that i-frame timecodes are correct, and monotonically
	//    increasing, though, which is why we must find an i-frame.
	// 3. Start decoding at this i-frame to arrive at the target timestamp.

	uint64_t closest_frame_idx = 0;
	{
		// Let's do a binary search to find the frame with the closest time stamp:

		auto const& track      = self->mp4_demux.track[ self->video_data->video_track_id ];
		auto const& timestamps = track.timestamp;
		size_t      n          = track.sample_count;

		assert( n > 0 );

		size_t l = 0;
		size_t r = n - 1;

		while ( l < r ) {
			closest_frame_idx = ( l + r ) / 2;
			size_t found_ts   = video_time_to_ticks_count( timestamps[ closest_frame_idx ], track.timescale );
			if ( found_ts < target_ticks ) {
				l = closest_frame_idx + 1;
			} else if ( found_ts > target_ticks ) {
				r = closest_frame_idx - 1;
			} else {
				break;
			}
		}
	}

	// ----------| Invariant: closest_frame_idx is now the index of the
	//                        frame closest to the search timestamp.

	uint64_t previous_i_frame_timestamp_in_ticks = 0;

	// what we need is to find the next i-frame that is <= the search frame index
	get_i_frame_earlier_or_equal_to_given_frame(
	    self->mp4_filestream,
	    &self->mp4_demux.track[ self->video_data->video_track_id ],
	    &closest_frame_idx,
	    &previous_i_frame_timestamp_in_ticks );

	// logger.info( "*** closest i-frame: %d", target_frame_idx );

	if ( should_resume_at_latest_i_frame ) {
		playhead_target = le::Ticks( previous_i_frame_timestamp_in_ticks );
	}

	self->ticks_seek_offset =
	    ( self->video_data->duration_in_ticks + playhead_target - playhead_without_offset ) %
	    self->video_data->duration_in_ticks;

	// Set current_decoded frame to the previous i-frame so that this
	// i-frame gets decoded next.
	self->current_decoded_frame = closest_frame_idx;

	// Now we need to invalidate all frames which have been decoded until now.
	// we only keep the frame which is the closest to the current playhead.
	{

		int64_t total_ticks       = self->video_data->duration_in_ticks.count();
		auto    ticks_at_playhead = ( ( self->ticks_at_last_update -
		                             self->ticks_at_start +
		                             previous_seek_offset +
		                             self->video_data->duration_in_ticks ) ) %
		                         self->video_data->duration_in_ticks;

		int64_t delta_ticks = ticks_at_playhead.count();

		int32_t closest_decoded_frame_idx = -1;
		int64_t closest_offset            = std::numeric_limits<int64_t>::max();

		for ( int i = 0; i != self->memory_frames.size(); i++ ) {
			auto& f = self->memory_frames[ i ];

			if ( f.state == MemoryFrameState::eDecodeSuccess ) {
				// We want to know how far the frame pts is from our current playhead.
				//
				// Our current playhead position is the origin - which is why we do an inverse
				// origin transform here by subtracting to the origin.
				int64_t frame_ticks_relative_to_playhead = ( total_ticks - delta_ticks + int64_t( f.ticks_pts.count() ) ) % total_ticks;

				if ( frame_ticks_relative_to_playhead > total_ticks / 2 ) {
					frame_ticks_relative_to_playhead -= total_ticks;
				}

				// Preserve the frame that is currently showing - if there is a frame that is currently showing
				if ( i == self->latest_memory_frame_available_for_rendering ) {
					continue;
				}

				// Mark all other decoded frames as invalid, and ready to be recycled.
				if ( std::abs( frame_ticks_relative_to_playhead ) < closest_offset ) {
					if ( closest_decoded_frame_idx != -1 ) {
						// There was a frame that was closer before - we must let go of that frame
						self->memory_frames[ closest_decoded_frame_idx ].state = MemoryFrameState::eIdle;
					}
					closest_decoded_frame_idx = i;
					closest_offset            = std::abs( frame_ticks_relative_to_playhead );
				} else {
					self->memory_frames[ i ].state = MemoryFrameState::eIdle;
				}
			}
		}
	}
	self->playback_state = le_video_decoder_o::eSeeking;

	return playhead_target < self->video_data->duration_in_ticks;
}

// ----------------------------------------------------------------------

void le_video_decoder_get_current_playhead_position( le_video_decoder_o* self, uint64_t* ticks, float* normalised ) {

	if ( ticks ) {
		*ticks = self->ticks_at_playhead.count();
	}
	if ( normalised ) {
		double current_pos    = std::chrono::duration<double>( self->ticks_at_playhead ).count();
		double total_duration = std::chrono::duration<double>( self->video_data->duration_in_ticks ).count();
		*normalised           = current_pos / total_duration;
	}
};

// ----------------------------------------------------------------------
uint64_t le_video_decoder_get_total_duration_in_ticks( le_video_decoder_o* self ) {
	return self->video_data->duration_in_ticks.count();
};

// ----------------------------------------------------------------------

void le_video_decoder_play( le_video_decoder_o* self ) {
	if ( self->playback_state == le_video_decoder_o::ePause ||
	     self->playback_state == le_video_decoder_o::eInitial ) {
		self->playback_state = le_video_decoder_o::ePlay;
	}
};

// ----------------------------------------------------------------------

static bool le_video_decoder_get_playback_should_loop( le_video_decoder_o* self ) {
	return !self->is_playback_not_looping;
};

// ----------------------------------------------------------------------

static void le_video_decoder_set_playback_should_loop( le_video_decoder_o* self, bool should_loop ) {
	self->is_playback_not_looping = !should_loop;
};

// ----------------------------------------------------------------------

static void le_video_decoder_set_on_video_playback_complete_cb( le_video_decoder_o* self, le_video_decoder_api::on_video_playback_complete_fun_t cb, void* user_data ) {
	self->on_playback_complete_callback_userdata = user_data;
	self->on_playback_complete_callback          = cb;
}
// ----------------------------------------------------------------------

bool le_video_decoder_get_frame_dimensions( le_video_decoder_o* self, uint32_t* w, uint32_t* h ) {

	if ( nullptr == self->video_data ) {
		return false;
	}

	if ( w ) {
		*w = self->video_data->width;
	}

	if ( h ) {
		*h = self->video_data->height;
	}

	return true;
};

// ----------------------------------------------------------------------

static void post_reload_hook( le_backend_o* backend ) {
#ifdef PLUGINS_DYNAMIC
	if ( backend ) {
		VkResult result = volkInitialize();
		assert( result == VK_SUCCESS &&
		        "must successfully initialize the vulkan loader "
		        "in case we're loading this module as a library" );
		auto       le_instance = le_backend_vk::private_backend_vk_i.get_instance( backend );
		VkInstance instance    = le_backend_vk::vk_instance_i.get_vk_instance( le_instance );
		volkLoadInstance( instance );
		auto device = le_backend_vk::private_backend_vk_i.get_vk_device( backend );
		volkLoadDevice( device );

		// Store the pointer to our current backend so we may retrieve it later
		*le_core_produce_dictionary_entry( hash_64_fnv1a_const( "le_backend_o" ) ) = backend;
	}
#endif
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_video_decoder, api ) {
	le_video_decoder_api* api_i              = static_cast<le_video_decoder_api*>( api );
	auto&                 le_video_decoder_i = api_i->le_video_decoder_i;

#ifdef PLUGINS_DYNAMIC
	// in case we're running this as a dynamic module, we must patch all vulkan methods as soon as the module gets reloaded

	auto backend_o = *le_core_produce_dictionary_entry( hash_64_fnv1a_const( "le_backend_o" ) );
	post_reload_hook( static_cast<le_backend_o*>( backend_o ) );
#endif

	le_video_decoder_i.create                            = le_video_decoder_create;
	le_video_decoder_i.destroy                           = le_video_decoder_destroy;
	le_video_decoder_i.get_pause_state                   = le_video_decoder_get_pause_state;
	le_video_decoder_i.set_pause_state                   = le_video_decoder_set_pause_state;
	le_video_decoder_i.get_playback_should_loop          = le_video_decoder_get_playback_should_loop;
	le_video_decoder_i.set_playback_should_loop          = le_video_decoder_set_playback_should_loop;
	le_video_decoder_i.play                              = le_video_decoder_play;
	le_video_decoder_i.update                            = le_video_decoder_update;
	le_video_decoder_i.init                              = le_video_decoder_init;
	le_video_decoder_i.on_backend_frame_clear_cb         = le_video_decoder_on_backend_frame_clear_cb;
	le_video_decoder_i.get_latest_available_frame        = le_video_decoder_get_latest_available_frame;
	le_video_decoder_i.get_latest_available_frame_index  = le_video_decoder_get_latest_available_frame_index;
	le_video_decoder_i.get_current_playhead_position     = le_video_decoder_get_current_playhead_position;
	le_video_decoder_i.get_total_duration_in_ticks       = le_video_decoder_get_total_duration_in_ticks;
	le_video_decoder_i.set_on_video_playback_complete_cb = le_video_decoder_set_on_video_playback_complete_cb;
	le_video_decoder_i.seek                              = le_video_decoder_seek;
	le_video_decoder_i.get_frame_dimensions              = le_video_decoder_get_frame_dimensions;
}
