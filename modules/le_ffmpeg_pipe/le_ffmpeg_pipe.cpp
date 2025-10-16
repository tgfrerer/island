#include "le_core.h"
#include "le_log.h"
#include "le_ffmpeg_pipe.h"

#include "private/le_renderer/le_renderer_types.h"
#include "shared/interfaces/le_image_encoder_interface.h"

#include <string>
#include <string.h>
#include <stdio.h>
#include <cassert>

#ifdef _MSC_VER

#	include <sstream>
#	include <windows.h>

static const char* G_PIPE_NAME = R"(\\.\pipe\ffmpeg_pipe)";

static std::string get_error_string( DWORD error_no ) {
	char errorMessage[ 256 ];

	// Retrieve the error message string
	FormatMessageA(
	    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	    nullptr,
	    error_no,
	    MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), // Default language
	    errorMessage,
	    sizeof( errorMessage ),
	    nullptr );

	return errorMessage;
}

#endif

struct le_image_encoder_format_o {
	le::Format format;
	le::ColorSpaceKHR color_space;
};

// ----------------------------------------------------------------------
// We must give clients of this encoder a chance to check whether they can assume
// a compatible version of this encoder:
static uint64_t le_image_encoder_get_encoder_version( le_image_encoder_o* encoder ) {
	static constexpr uint64_t ENCODER_VERSION = 0ull << 48 | 0ull << 32 | 2ull << 16 | 1ull << 0;
	static_assert( ENCODER_VERSION == le_image_encoder_interface_t::API_VERSION, "Api version must match interface api version" );
	return 0;
};

// ----------------------------------------------------------------------

static le_ffmpeg_pipe_encoder_parameters_t get_default_parameters() {
	using ns = le_ffmpeg_pipe_encoder_parameters_t;
	return le_ffmpeg_pipe_encoder_parameters_t{
	    /* MP4 @ 60 fps */ .command_line = "ffmpeg -r 60 -f rawvideo -pix_fmt %s -s %dx%d -i %s -threads 0 -vcodec h264_nvenc -preset llhq -rc:v vbr_minqp -qmin:v 19 -qmax:v 21 -b:v 2500k -maxrate:v 5000k -profile:v high %s",
	    // /* GIF */ .command_line = "ffmpeg -r 60 -f rawvideo -pix_fmt %s -s %dx%d -i %s -filter_complex \"[0:v] fps=30,split [a][b];[a] palettegen [p];[b][p] paletteuse\" %s",
	};
}

// ----------------------------------------------------------------------

struct le_image_encoder_o {
	uint32_t image_width  = 0;
	uint32_t image_height = 0;

	std::string output_file_name;

#ifdef _MSC_VER
	HANDLE              pipe = nullptr; // named windows pipe
	PROCESS_INFORMATION pi   = {};
#else
	FILE* pipe = nullptr; // ffmpeg pipe
#endif
	std::string pipe_cmd;
};

// ----------------------------------------------------------------------

static void le_image_encoder_apply_parameters( le_image_encoder_o* self, le_ffmpeg_pipe_encoder_parameters_t const& parameters ) {

	if ( parameters.command_line ) {
		self->pipe_cmd = parameters.command_line;
	}
}

// ----------------------------------------------------------------------

static le_image_encoder_o* le_image_encoder_create( char const* file_path, uint32_t width, uint32_t height ) {
	static auto logger = LeLog( "le_ffmpeg_pipe" );
	auto        self   = new le_image_encoder_o();
	logger.info( "Creating ffmpeg pipe encoder %p", self );

	self->output_file_name = file_path;
	self->image_width      = width;
	self->image_height     = height;

	le_image_encoder_apply_parameters( self, get_default_parameters() );

	return self;
}

// ----------------------------------------------------------------------

static void le_image_encoder_destroy( le_image_encoder_o* self ) {
	static auto logger = LeLog( "le_ffmpeg_pipe" );
	logger.info( "Destroying ffmpeg pipe encoder %p", self );

	if ( self->pipe ) {

#ifdef _MSC_VER
		// TODO : Add Windows implementation
		CloseHandle( self->pipe );
		CloseHandle( self->pi.hProcess );
		CloseHandle( self->pi.hThread );
		self->pipe = nullptr;
#else
		pclose( self->pipe );
		self->pipe = nullptr;
#endif
	}

	delete self;
}

// ----------------------------------------------------------------------

static void le_image_encoder_set_encode_parameters( le_image_encoder_o* self, void* p_parameters ) {
	static auto logger = LeLog( "le_ffmpeg_pipe" );
	if ( p_parameters ) {
		le_image_encoder_apply_parameters( self, *static_cast<le_ffmpeg_pipe_encoder_parameters_t*>( p_parameters ) );
	} else {
		logger.warn( "Could not set parameters for encoder: Parameters pointer was NULL." );
	}
}
// ----------------------------------------------------------------------

static bool le_image_encoder_write_pixels( le_image_encoder_o* self, uint8_t const* p_pixel_data, size_t pixel_data_byte_count, le_image_encoder_format_o* pixel_data_format ) {
	static auto logger = LeLog( "le_ffmpeg_pipe" );

	if ( !self->pipe && !self->pipe_cmd.empty() ) {

		std::string pix_fmt = "rgba";

		switch ( pixel_data_format->format ) {
		case ( le::Format::eR8G8B8A8Unorm ): // fall-through
		case ( le::Format::eR8G8B8A8Srgb ):
			pix_fmt = "rgba";
			break;
		case ( le::Format::eB8G8R8A8Srgb ):
		case ( le::Format::eB8G8R8A8Unorm ):
			pix_fmt = "bgra";
			break;
		case ( le::Format::eR16G16B16A16Unorm ):
			pix_fmt = "rgba64le";
			break;
		case ( le::Format::eR16G16B16A16Sfloat ):
			pix_fmt = "rgbaf16le";
			break;
		default:
			pix_fmt = "rgba";
		}

#ifdef _MSC_VER

		// create windows named pipe
		// Create the named pipe

		self->pipe = CreateNamedPipeA(
		    G_PIPE_NAME,                // Pipe name
		    PIPE_ACCESS_OUTBOUND,       // Write access
		    PIPE_TYPE_BYTE | PIPE_WAIT, // Byte stream pipe, blocking mode
		    1,                          // Max number of instances
		    0,                          // Output buffer size
		    0,                          // Input buffer size
		    0,                          // Default time-out
		    nullptr );                  // Default security attributes

		if ( self->pipe == INVALID_HANDLE_VALUE ) {
			DWORD errorCode = GetLastError(); // Retrieve the last error code
			logger.error( "Could not create named pipe. Additionally, GetLastError reports: $s", get_error_string( errorCode ).c_str() );
			self->pipe = nullptr;
			return false;
		}

		// create ffmpeg process
		self->pi = {}; // zero out process_info struct

		STARTUPINFOA si = {};
		si.cb           = sizeof( self->pi );

		char cmd[ 1024 ]{};
		snprintf( cmd, sizeof( cmd ), self->pipe_cmd.c_str(), pix_fmt.c_str(), self->image_width, self->image_height, G_PIPE_NAME, self->output_file_name.c_str() );
		logger.info( "Image encoder opening pipe using command line: '%s'", cmd );

		// Create the process
		if ( !CreateProcessA(
		         nullptr,        // Application name
		         ( LPSTR )cmd,   // Command line
		         nullptr,        // Process handle not inheritable
		         nullptr,        // Thread handle not inheritable
		         FALSE,          // Set handle inheritance to FALSE
		         0,              // No creation flags
		         nullptr,        // Use parent's environment block
		         nullptr,        // Use parent's starting directory
		         &si,            // Pointer to STARTUPINFO structure
		         &self->pi ) ) { // Pointer to PROCESS_INFORMATION structure

			DWORD error_no = GetLastError();
			logger.error( "error creating process: %s", get_error_string( error_no ).c_str() );

			return false;
		}

		// this should block until ffmpeg has connected
		ConnectNamedPipe( self->pipe, nullptr );
		// Wait until the process exits
		// WaitForSingleObject( self->pi.hProcess, INFINITE );

		// Close process and thread handles

#else
		char cmd[ 1024 ]{};
		snprintf( cmd, sizeof( cmd ), self->pipe_cmd.c_str(), pix_fmt.c_str(), self->image_width, self->image_height, "-", self->output_file_name.c_str() );
		logger.info( "Image encoder opening pipe using command line: '%s'", cmd );

		// Open pipe to ffmpeg's stdin in binary write mode
		self->pipe = popen( cmd, "w" );

		if ( self->pipe == nullptr ) {
			logger.error( "Could not open pipe. Additionally, strerror reports: $s", strerror( errno ) );
		}

		return false;
#endif // _MSC_VER
	}

	if ( self->pipe ) {
		// Write out frame contents to ffmpeg via pipe.

		// TODO: we should be able to do the write on the back thread.
		// the back thread must signal that it is complete with writing
		// before the next present command is executed.
#ifdef _MSC_VER

		DWORD bytesWritten;
		if ( !WriteFile( self->pipe, p_pixel_data, pixel_data_byte_count, &bytesWritten, nullptr ) ) {
			DWORD errorCode = GetLastError(); // Retrieve the last error code
			logger.error( "Could not write to named pipe. %s", get_error_string( errorCode ).c_str() );
			self->pipe = nullptr;
			return false;
		}
#else
		// Linux implementation
		fwrite( p_pixel_data, pixel_data_byte_count, 1, self->pipe );
#endif
	}

	return true;
}

// ----------------------------------------------------------------------

static void le_image_encoder_update_filename( le_image_encoder_o* self, char const* filename ) {
	// Note: only the first file name will be used
	self->output_file_name = filename;
}
// ----------------------------------------------------------------------

static void* parameters_object_clone( void const* obj ) {
	auto result = new le_ffmpeg_pipe_encoder_parameters_t{
	    *static_cast<le_ffmpeg_pipe_encoder_parameters_t const*>( obj ) };
	return result;
};

// ----------------------------------------------------------------------
static void parameters_object_destroy( void* obj ) {
	le_ffmpeg_pipe_encoder_parameters_t* typed_obj =
	    static_cast<le_ffmpeg_pipe_encoder_parameters_t*>( obj );
	delete ( typed_obj );
};

LE_MODULE_REGISTER_IMPL( le_ffmpeg_pipe, api ) {
	auto& encoder_i = static_cast<le_ffmpeg_pipe_api*>( api )->le_ffmpeg_pipe_encoder_i;

	if ( encoder_i == nullptr ) {
		encoder_i = new le_image_encoder_interface_t{};
	} else {
		// The interface already existed - we have been reloaded and only just need to update
		// function pointer addresses.
		//
		// This is important as by not re-allocating a new interface object
		// but by updating the existing interface object by-value, we keep the *public
		// address for the interface*, while updating its function pointers.
		*encoder_i = le_image_encoder_interface_t();
	}

	encoder_i->clone_image_encoder_parameters_object   = parameters_object_clone;
	encoder_i->destroy_image_encoder_parameters_object = parameters_object_destroy;

	encoder_i->create_image_encoder  = le_image_encoder_create;
	encoder_i->destroy_image_encoder = le_image_encoder_destroy;
	encoder_i->write_pixels          = le_image_encoder_write_pixels;
	encoder_i->update_filename       = le_image_encoder_update_filename;
	encoder_i->set_encode_parameters = le_image_encoder_set_encode_parameters;
	encoder_i->get_encoder_version   = le_image_encoder_get_encoder_version;
}
