#include "le_gltf_loader.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "fx/gltf.h"

#include <iostream>
#include <iomanip>

using namespace fx;

struct le_gltf_loader_o {
	gltf::Document document;
};

static le_gltf_loader_o *loader_create() {
	auto self = new le_gltf_loader_o();
	return self;
}

static void loader_destroy( le_gltf_loader_o *self ) {
	delete self;
}

static bool loader_load_from_text( le_gltf_loader_o *self, const char *path ) {
	bool           result = true;
	gltf::Document doc;
	try {
		doc = gltf::LoadFromText( path );
	} catch ( std::runtime_error e ) {
		std::cerr << __FILE__ " [ ERROR ] Could not load file: '" << path << "'"
		          << ", received error: " << e.what() << std::endl
		          << std::flush;
		result = false;
	}
	if ( result == true ) {
		std::swap( self->document, doc );
	}
	return result;
}

ISL_API_ATTR void register_le_gltf_loader_api( void *api ) {
	auto &loader_i = static_cast<le_gltf_loader_api *>( api )->loader_i;

	loader_i.create         = loader_create;
	loader_i.destroy        = loader_destroy;
	loader_i.load_from_text = loader_load_from_text;
}
