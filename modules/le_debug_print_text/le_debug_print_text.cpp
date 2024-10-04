#include "le_debug_print_text.h"
#include "le_core.h"
#include "le_renderer.h"
#include "le_renderer.hpp"
#include "le_pipeline_builder.h"
#include "le_log.h"
#include <vector>
#include <string>
#include "assert.h"
#include "string.h" // for strlen
#include <cstdarg>  // for arg
#include <limits>   // for std::numeric_limits

using float2         = le_debug_print_text_api::float2;
using float_colour_t = le_debug_print_text_api::float_colour_t;

struct state_t {
	float_colour_t col_fg     = { 1, 1, 1, 1 }; // rgba
	float_colour_t col_bg     = { 0, 0, 0, 0 }; // rgba
	float          char_scale = 1;
};

struct print_instruction {
	float2    cursor_start;
	float2    cursor_end; // we keep the end cursor so that we can check whether two succeeding runs can be combined
	size_t    style_id;   // which style

	std::string text;
};

struct le_debug_print_text_o {
	// members
	le_shader_module_handle shader_vert = nullptr;
	le_shader_module_handle shader_frag = nullptr;
	le_gpso_handle          pipeline    = nullptr;

	float2 cursor_pos = { 0, 0 }; // current cursor position, top right of the screen

	size_t                         last_used_style = -1;
	std::vector<state_t>           styles; // unused for now
	std::vector<print_instruction> print_instructions;
};

using this_o = le_debug_print_text_o;

struct word_data {
	uint32_t  word;     // four characters
	float     pos_size[ 3 ]; // xy position + scale
};

static auto logger = []() -> auto {
	return le::Log( "le_debug_print_text" );
}();

// ----------------------------------------------------------------------

static void le_debug_print_text_draw_reset( this_o* self ) {
	self->styles.clear();
	self->cursor_pos = {};
	self->print_instructions.clear();

	// Add default style
	self->styles.push_back( {
	    .col_fg     = { 1, 1, 1, 1 },
	    .col_bg     = { 0, 0, 0, 0 },
	    .char_scale = 1.f,
	} );
}

// ----------------------------------------------------------------------

static this_o* le_debug_print_text_create() {
	auto self = new this_o();

	logger.info( "Created debug text printer object %p", self );
	le_debug_print_text_draw_reset( self );

	return self;
}

// ----------------------------------------------------------------------

static void le_debug_print_text_destroy( this_o* self ) {
	delete self;
}

// ----------------------------------------------------------------------

static bool le_debug_print_text_has_messages( this_o* self ) {
	return !self->print_instructions.empty();
}

// ----------------------------------------------------------------------

static float le_debug_print_text_get_scale( this_o* self ) {
	return self->styles[ self->last_used_style ].char_scale;
}

// ----------------------------------------------------------------------

static void le_debug_print_text_create_pipeline_objects( this_o* self, le_pipeline_manager_o* pipeline_manager ) {
	if ( !self->shader_frag ) {
		self->shader_frag =
		    LeShaderModuleBuilder( pipeline_manager )
		        .setShaderStage( le::ShaderStage::eFragment )
		        .setSourceFilePath( "./resources/shaders/le_debug_print_text/debug_text.frag" )
		        .setSourceLanguage( le::ShaderSourceLanguage::eGlsl )
		        .build();
	}

	if ( !self->shader_vert ) {
		self->shader_vert =
		    LeShaderModuleBuilder( pipeline_manager )
		        .setShaderStage( le::ShaderStage::eVertex )
		        .setSourceFilePath( "./resources/shaders/le_debug_print_text/debug_text.vert" )
		        .setSourceLanguage( le::ShaderSourceLanguage::eGlsl )
		        .build();
	}

	if ( !self->pipeline ) {
		// clang-format off
		self->pipeline =
		    LeGraphicsPipelineBuilder( pipeline_manager )
		        .addShaderStage( self->shader_vert )
		        .addShaderStage( self->shader_frag )
		        .withAttributeBindingState()
		        	.addBinding()
		        		.setStride(3* sizeof( float ) )
		        		.addAttribute()
		        			.setType( le_num_type::eF32 )
		        			.setVecSize( 3 )
		        		.end()
		        	.end()
		        	.addBinding()
		        		.setInputRate( le_vertex_input_rate::ePerInstance )
		        		.setStride( sizeof( word_data ) )
		        		.addAttribute()
		        			.setType( le_num_type::eU32 )
		        			.setVecSize( 1 )
		        		.end()
		        		.addAttribute()
		        			.setOffset( sizeof( uint32_t ) )
		        			.setType( le_num_type::eF32 )
		        			.setVecSize( 3 )
		        		.end()
		        	.end()
		        .end()
		        .build();
		// clang-format on
	}
}

// ----------------------------------------------------------------------

static void pass_main_print_text( le_command_buffer_encoder_o* encoder_, void* user_data ) {

	// Draw main scene

	auto                self = static_cast<this_o*>( user_data );
	le::GraphicsEncoder encoder{ encoder_ };

	auto extents = encoder.getRenderpassExtent();

	le_debug_print_text_create_pipeline_objects( self, encoder.getPipelineManager() );

	float vertexPositions[][ 3 ] = {
	    // all dimensions given in font map pixels
	    { 0, 16, 0 },
	    { 0, 0, 0 },
	    { 8 * 4, 0, 0 }, // 8*4 == width for whole word (4 chars)
	    { 8 * 4, 16, 0 },
	};

	uint16_t indices[] = {
	    0, 1, 2,
	    0, 2, 3, //
	};

	// the font has a size of 8 pixels in x ... this means that we want to find out how much one character
	// takes in terms of screen percentage


	float one_pixel_w = 2.f / extents.width;

	std::vector<word_data> words;

	for ( auto& p : self->print_instructions ) {
		float  char_scale = self->styles[ p.style_id ].char_scale;
		float  char_width = ( ( 8.f * char_scale ) / ( extents.width ) );
		size_t num_words  = p.text.size() / 4;
		for ( int i = 0; i != num_words; i++ ) {
			uint32_t* p_words = ( uint32_t* )p.text.data();
			words.push_back(
			    {
			        p_words[ i ],
			        { 8 * 4 * i * char_scale + p.cursor_start.x,
			          0 * char_scale + p.cursor_start.y, char_scale } // position given in pixels, scale (scale gets applied first)
			        // we don't automatically apply the char scale becuase we want to maintain that xy is absolute pixels.
			    } );
		}
	}

	// TODO:
	// build a buffer with word, position, scale for each word
	// and then render enough instances of words so that all the words
	// get drawn

	float u_resolution[ 2 ] = {
	    float( extents.width ),
	    float( extents.height ),
	};

	encoder
	    .bindGraphicsPipeline( self->pipeline )
	    .setPushConstantData( &u_resolution, sizeof( u_resolution ) )
	    .setVertexData( vertexPositions, sizeof( vertexPositions ), 0 )
	    .setVertexData( words.data(), words.size() * sizeof( word_data ), 1 )
	    .setIndexData( indices, sizeof( indices ) )
	    .drawIndexed( 6, words.size(), 0, 0, 0 );

	// note that this should clear the state ... we only want to do this once.

	le_debug_print_text_draw_reset( self );
}

// ----------------------------------------------------------------------

static void le_debug_print_text_draw( this_o* self, le_renderpass_o* rp_ ) {
	auto rp = le::RenderPass( rp_ );
	rp.setExecuteCallback( self, pass_main_print_text );
}

// ----------------------------------------------------------------------

static void le_debug_print_text_set_scale( this_o* self, float scale ) {
	// find  last element in style.
	// this happens in a copy-on-write fashion,
	// but we do re-use last element if it has not been used.

	assert( !self->styles.empty() );

	// ---------- | invariant: there is at least one style available

	// only store the style if it changes from the current style.

	if ( std::abs( self->styles.back().char_scale - scale ) <= std::numeric_limits<float>::epsilon() ) {
		return;
	}

	// -----------| invariant: the scale changes

	size_t last_style_id = self->styles.size() - 1;

	// if the last style was used, then we must copy the last style

	if ( self->last_used_style == last_style_id ) {
		self->styles.push_back( self->styles.back() );
	};

	self->styles.back().char_scale = scale;
}

// ----------------------------------------------------------------------

static void le_debug_print_text_generate_instructions( this_o* self, std::string& text_str, float2& cursor ) {

	// Todo: Maybe add a high watermark -- so that we don't just accumulate without printing to screen

	assert( !self->styles.empty() );

	// ----------| Invariant: there must at least be one style available

	float2 cursor_end = cursor;

	size_t style_id                   = self->styles.size() - 1;
	self->last_used_style             = style_id;

	// Find out if this is a continuation - if it is, then we can just paste the text
	// to the end of the last text.

	if ( !self->print_instructions.empty() ) {

		auto& last_instruction = self->print_instructions.back();

		float2 difference_vec = {
		    last_instruction.cursor_end.x - cursor.x,
		    last_instruction.cursor_end.y - cursor.y,

		};

		float difference_dotted = difference_vec.x * difference_vec.x + difference_vec.y * difference_vec.y;

		if ( std::numeric_limits<float>::epsilon() >= difference_dotted && last_instruction.style_id == style_id ) {

			// this is a continuation - we can just concatenate the
			// current string with the next string

			// find the last valid position in the current string
			// this is equivalent to the index of the first '\0' char.

			size_t last_char = strlen( last_instruction.text.c_str() );

			// remove trailing \0 chars
			last_instruction.text = std::string( last_instruction.text.begin(), last_instruction.text.begin() + last_char );
			// append new text
			last_instruction.text.append( text_str );

			// See if we need to add any trailing \0 chars
			last_char = strlen( last_instruction.text.c_str() );

			// Append \0 chars to complete the last word if necessary
			last_instruction.text.append( ( 4 - ( last_char % 4 ) ) % 4, '\0' );

			cursor_end.x = last_instruction.cursor_end.x +
			               strlen( text_str.c_str() ) * 8 * self->styles[ style_id ].char_scale;

			last_instruction.cursor_end = cursor_end;

			cursor = cursor_end;
			return;
		}
	}

	// we pad any leftover chars of the string with '\0' chars

	text_str.append( ( 4 - text_str.size() % 4 ) % 4, '\0' );

	cursor_end.x = cursor.x +
	               strlen( text_str.c_str() ) * 8 * self->styles[ style_id ].char_scale;

	self->print_instructions.push_back( {
	    .cursor_start = cursor,
	    .cursor_end   = cursor_end, // we keep the end cursor so that we can check whether two succeeding runs can be combined
	    .style_id     = style_id,   // which style
	    .text         = text_str,
	} );

	cursor = cursor_end;
}

// ----------------------------------------------------------------------

static void le_debug_print_text_print( this_o* self, char const* text ) {

	std::string text_line = text;

	// we need to do some processing on the text here
	// so that we can filter our unprintable characters for example
	// and so that we can break lines that need breaking.

	le_debug_print_text_generate_instructions( self, text_line, self->cursor_pos );
}

// ----------------------------------------------------------------------

static void le_debug_print_text_printf( this_o* self, const char* msg, ... ) {

	static size_t      num_bytes_buffer_2 = 0;
	static std::string buffer{};

	va_list arglist;

	va_start( arglist, msg );
	{
		va_list old_args;
		va_copy( old_args, arglist );

		do {
			va_list args;
			va_copy( args, old_args );
			buffer.resize( num_bytes_buffer_2 );
			num_bytes_buffer_2 = vsnprintf( buffer.data(), buffer.size(), msg, args );
			num_bytes_buffer_2++; // make space for final \0 byte
			va_end( args );
		} while ( num_bytes_buffer_2 > buffer.size() );

		va_end( old_args );
	}
	va_end( arglist );

	le_debug_print_text_print( self, buffer.c_str() );
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_debug_print_text, api ) {
	auto const& p_le_debug_print_text_api = static_cast<le_debug_print_text_api*>( api );
	auto&       le_debug_print_text_i     = static_cast<le_debug_print_text_api*>( api )->le_debug_print_text_i;
	le_debug_print_text_i.create          = le_debug_print_text_create;
	le_debug_print_text_i.destroy         = le_debug_print_text_destroy;
	le_debug_print_text_i.draw            = le_debug_print_text_draw;
	le_debug_print_text_i.print           = le_debug_print_text_print;
	le_debug_print_text_i.set_scale       = le_debug_print_text_set_scale;
	le_debug_print_text_i.printf          = le_debug_print_text_printf;
	le_debug_print_text_i.has_messages    = le_debug_print_text_has_messages;
	le_debug_print_text_i.get_scale       = le_debug_print_text_get_scale;

	if ( p_le_debug_print_text_api->singleton_obj == nullptr ) {
		// If we're registering this for the first time, we must create the singleton object.
		// This object will never get destroyed.
		p_le_debug_print_text_api->singleton_obj = le_debug_print_text_create();
	}
}
