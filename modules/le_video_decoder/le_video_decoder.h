#ifndef GUARD_le_video_H
#define GUARD_le_video_H

#include "le_core.h"

/*
 * Api note: why do we require a filepath when creating a video player?
 *
 * We can only tell what kind of resources to allocate, and what kind of
 * decoder to generate once we had a chance to load and look at the file.
 *
 * This is why each file needs its own decoder. We cannot switch files once
 * a decoder has been created - the file is part of the decoder's identity.
 *
 */

struct le_video_decoder_o;
struct le_renderer_o;
struct le_rendergraph_o;
struct le_img_resource_handle_t;

// clang-format off
struct le_video_decoder_api {

	typedef void (*on_video_playback_complete_fun_t)(le_video_decoder_o* decoder, void* user_data);

	struct le_video_decoder_interface_t {

        void                 ( * init        ) ( );  
		le_video_decoder_o * ( * create      ) ( le_renderer_o* renderer, char const * file_path);
		void                 ( * destroy     ) ( le_video_decoder_o* self );
		void                 ( * update      ) ( le_video_decoder_o* self, le_rendergraph_o* rendergaph, uint64_t ticks );

		void (*set_pause_state)(le_video_decoder_o* self, bool should_pause);
		bool (*get_pause_state)(le_video_decoder_o* self );

		bool (*get_playback_should_loop)(le_video_decoder_o* self);
		void (*set_playback_should_loop)(le_video_decoder_o* self, bool should_loop);

		void (*play)(le_video_decoder_o* self);
		bool (*seek)( le_video_decoder_o* self, uint64_t target_ticks, bool should_resume_at_latest_i_frame );

		bool (*get_frame_dimensions)(le_video_decoder_o* self, uint32_t *w, uint32_t *h);

		// If any of the pointers in ticks, normalised are valid, sets their value to
		// ticks     : number of ticks at current playhead position
		// normalised: normalised (0..1) current playhead position
		void (*get_current_playhead_position)(le_video_decoder_o* self, uint64_t *ticks, float* normalised);

		uint64_t (*get_total_duration_in_ticks)(le_video_decoder_o* self);

		/// \return a handle to the current image - nullptr if no image available
		///
		/// NOTE: The image handle is only valid for the current update cycle -
		/// you must not keep a reference to it that lasts for longer than the
		/// current frame.
		///
		le_img_resource_handle_t* (* get_latest_available_frame )(le_video_decoder_o* self);

		// returns frame's picture order count (poc) - this may not be what you're expecting
		bool (* get_latest_available_frame_index )(le_video_decoder_o* self, uint64_t *frame_index);

		// Callback to trigger once the current video has completed playback
		// This will trigger everytime the video is playing and has completed
		// displaying the last frame.
		void(*set_on_video_playback_complete_cb)(le_video_decoder_o* self, on_video_playback_complete_fun_t cb, void* user_data);

		// private:
		void (* on_backend_frame_clear_cb ) ( void * user_data);
	};

	le_video_decoder_interface_t       le_video_decoder_i;
};
// clang-format on

LE_MODULE( le_video_decoder );
LE_MODULE_LOAD_DEFAULT( le_video_decoder );

#ifdef __cplusplus

namespace le_video_decoder {
static const auto& api                = le_video_decoder_api_i;
static const auto& le_video_decoder_i = api->le_video_decoder_i;
} // namespace le_video_decoder

class LeVideoDecoder : NoCopy, NoMove {
	le_video_decoder_o* self;
	bool                owns_self = false; // if true, c++ RAII will apply to this object. Otherwise, the object is only used for more ergonomic access to the underlying c object.

  public:
	LeVideoDecoder( le_renderer_o* renderer, const char* file_path )
	    : self( le_video_decoder::le_video_decoder_i.create( renderer, file_path ) )
	    , owns_self( true ) {
	}

	LeVideoDecoder( le_video_decoder_o* obj )
	    : self( obj )
	    , owns_self( false ){};

	~LeVideoDecoder() {
		if ( owns_self ) {
			le_video_decoder::le_video_decoder_i.destroy( self );
		}
	}

	/// Note: only call update once per app update cyclce - and call update before using any of the
	/// resources provided by the video decoder, meaning call update() before get_lastest_available_frame()
	void update( le_rendergraph_o* rendergraph, uint64_t ticks ) {
		le_video_decoder::le_video_decoder_i.update( self, rendergraph, ticks );
	}

	void play() {
		le_video_decoder::le_video_decoder_i.play( self );
	}

	void seek( uint64_t target_ticks, bool should_resume_at_latest_i_frame = false ) {
		le_video_decoder::le_video_decoder_i.seek( self, target_ticks, should_resume_at_latest_i_frame );
	}

	void set_pause_state( bool pause_state ) {
		le_video_decoder::le_video_decoder_i.set_pause_state( self, pause_state );
	}

	void get_current_playhead_position( uint64_t* ticks, float* normalised ) {
		le_video_decoder::le_video_decoder_i.get_current_playhead_position( self, ticks, normalised );
	}

	uint64_t get_total_duration_in_ticks() {
		return le_video_decoder::le_video_decoder_i.get_total_duration_in_ticks( self );
	}

	bool get_pause_state() {
		return le_video_decoder::le_video_decoder_i.get_pause_state( self );
	}

	bool get_playback_should_loop() {
		return le_video_decoder::le_video_decoder_i.get_playback_should_loop( self );
	}
	void set_playback_should_loop( bool should_loop ) {
		le_video_decoder::le_video_decoder_i.set_playback_should_loop( self, should_loop );
	}

	bool get_frame_dimensions( uint32_t* w, uint32_t* h ) {
		return le_video_decoder::le_video_decoder_i.get_frame_dimensions( self, w, h );
	}

	le_img_resource_handle_t* get_latest_available_frame() {
		return le_video_decoder::le_video_decoder_i.get_latest_available_frame( self );
	}

	// returns frame's poc (picture order count) this may not be what you're expecting.
	bool get_latest_available_frame_index( uint64_t* frame_index ) {
		return le_video_decoder::le_video_decoder_i.get_latest_available_frame_index( self, frame_index );
	}

	void set_on_playback_complete_callback( le_video_decoder_api::on_video_playback_complete_fun_t cb, void* user_data ) {
		le_video_decoder::le_video_decoder_i.set_on_video_playback_complete_cb( self, cb, user_data );
	}

	operator auto() {
		return self;
	}
};

namespace le {
using VideoPlayer = LeVideoDecoder;
}

#endif // __cplusplus

#endif
