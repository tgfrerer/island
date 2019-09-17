#include "le_mesh.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include <math.h>
#include <vector>
#include <experimental/filesystem> // for file loading
#include <iostream>                // for file loading
#include <fstream>                 // for file loading
#include <iomanip>                 // for file loading

namespace std {
using namespace experimental;
}

#include <cstring>

#include "le_mesh_types.h" //

// ----------------------------------------------------------------------

static le_mesh_o *le_mesh_create() {
	auto self = new le_mesh_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_mesh_destroy( le_mesh_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_mesh_clear( le_mesh_o *self ) {
	self->vertices.clear();
	self->normals.clear();
	self->uvs.clear();
	self->tangents.clear();
	self->colours.clear();
	self->indices.clear();
}

// ----------------------------------------------------------------------

static void le_mesh_get_vertices( le_mesh_o *self, size_t &count, float **vertices ) {
	count = self->vertices.size();
	if ( vertices ) {
		*vertices = static_cast<float *>( &self->vertices[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_tangents( le_mesh_o *self, size_t &count, float **tangents ) {
	count = self->tangents.size();
	if ( tangents ) {
		*tangents = static_cast<float *>( &self->tangents[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_indices( le_mesh_o *self, size_t &count, uint16_t **indices ) {
	count = self->indices.size();
	if ( indices ) {
		*indices = self->indices.data();
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_normals( le_mesh_o *self, size_t &count, float **normals ) {
	count = self->normals.size();
	if ( normals ) {
		*normals = static_cast<float *>( &self->normals[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_colours( le_mesh_o *self, size_t &count, float **colours ) {
	count = self->colours.size();
	if ( colours ) {
		*colours = static_cast<float *>( &self->colours[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_uvs( le_mesh_o *self, size_t &count, float **uvs ) {
	count = self->normals.size();
	if ( uvs ) {
		*uvs = static_cast<float *>( &self->uvs[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_data( le_mesh_o *self, size_t &numVertices, size_t &numIndices, float **vertices, float **normals, float **uvs, float **colours, uint16_t **indices ) {
	numVertices = self->vertices.size();
	numIndices  = self->indices.size();

	if ( vertices ) {
		*vertices = self->vertices.empty() ? nullptr : static_cast<float *>( &self->vertices[ 0 ].x );
	}

	if ( colours ) {
		*colours = self->colours.empty() ? nullptr : static_cast<float *>( &self->colours[ 0 ].x );
	}

	if ( normals ) {
		*normals = self->normals.empty() ? nullptr : static_cast<float *>( &self->normals[ 0 ].x );
	}

	if ( uvs ) {
		*uvs = self->uvs.empty() ? nullptr : static_cast<float *>( &self->uvs[ 0 ].x );
	}

	if ( indices ) {
		*indices = self->indices.data();
	}
}

// ----------------------------------------------------------------------
/// \brief   file loader utility method
/// \details loads file given by filepath and returns a vector of chars if successful
/// \note    returns an empty vector if not successful
static std::vector<char> load_file( const std::filesystem::path &file_path, bool *success ) {

	std::vector<char> contents;

	size_t        fileSize = 0;
	std::ifstream file( file_path, std::ios::in | std::ios::binary | std::ios::ate );

	if ( !file.is_open() ) {
		std::cerr << "Unable to open file: " << std::filesystem::canonical( file_path ) << std::endl
		          << std::flush;
		*success = false;
		return contents;
	}

	//	std::cout << "OK Opened file:" << std::filesystem::canonical( file_path ) << std::endl
	//	          << std::flush;

	// ----------| invariant: file is open

	auto endOfFilePos = file.tellg();

	if ( endOfFilePos > 0 ) {
		fileSize = size_t( endOfFilePos );
	} else {
		*success = false;
		return contents;
	}

	// ----------| invariant: file has some bytes to read
	contents.resize( fileSize );

	file.seekg( 0, std::ios::beg );
	file.read( contents.data(), endOfFilePos );
	file.close();

	*success = true;
	return contents;
}

// ----------------------------------------------------------------------

inline int does_start_with( char const *haystack, char const *needle, size_t &needle_len ) {
	needle_len = strlen( needle );
	return 0 == strncmp( haystack, needle, needle_len );
};

// ----------------------------------------------------------------------
/// \brief loads mesh from ply file
/// \note any contents of mesh will be cleared before loading
/// \return true upon success, false otherwise.
static bool le_mesh_load_from_ply_file( le_mesh_o *self, char const *file_path_ ) {

	// - Make sure file exists

	std::filesystem::path file_path{file_path_};

	if ( !std::filesystem::exists( file_path ) ) {
		std::cerr << "File not found: '" << file_path << "'";
		return false;
	}

	// --------| invariant: File path exists

	// - Build mesh attributes structure based on header.

	/*
	 * element vertex structure: attribute index tells us where to store data which we parse
	*/

	struct Property {

		// data type for the property
		enum class Type : uint8_t {
			eUnknown,
			eList,
			eFloat,
			eUchar,
			eUint,
		};

		// name for attribute in context of a mesh
		enum class AttributeType : uint8_t {
			eUnknown,
			eVX,
			eVY,
			eVZ,
			eNX,
			eNY,
			eNZ,
			eTexU,
			eTexV,
			eColR,
			eColG,
			eColB,
			eColA,
		};

		Type          type              = Type::eUnknown;
		AttributeType attribute_type    = AttributeType::eUnknown; // only used for attributes - not lists.
		Type          list_size_type    = Type::eUnknown;          // only used for lists
		Type          list_content_type = Type::eUnknown;          // only used for lists
		char const *  name              = nullptr;
		uint8_t       name_len          = 0; ///< number of chars for name (does not include \0)
	};

	struct Element {

		enum class Type : uint8_t {
			eUnknown,
			eVertex,
			eFace,
		};
		char const *          name;
		Type                  type;
		uint8_t               name_len; ///< number of chars for name (does not include \0)
		uint32_t              num_elements;
		std::vector<Property> properties;
	};

	// - Check that the header is correct (consistent, has minimum necessary attributes)

	// - Read file into memory
	bool              file_load_success = true;
	std::vector<char> file_data         = load_file( file_path, &file_load_success );

	if ( !file_load_success ) {
		std::cerr << "File could not be loaded: '" << file_path << "'";
		return false;
	}

	static auto DELIMS{"\n\0"};
	char *      c_save_ptr; //< we use the re-entrant version of strtok, for which state is stored in here

	// --------| invariant: file was loaded.

	char *c = strtok_r( file_data.data(), DELIMS, &c_save_ptr );

	if ( 0 != strcmp( c, "ply" ) ) {
		std::cerr << "Invalid file header: '" << file_path << "'";
		return false;
	}

	c = strtok_r( nullptr, DELIMS, &c_save_ptr );

	if ( 0 != strcmp( c, "format ascii 1.0" ) ) {
		std::cerr << "Invalid file header: '" << file_path << "'";
		return false;
	}

	c = strtok_r( nullptr, DELIMS, &c_save_ptr );

	// Parse header data into a vector of Element
	std::vector<Element> elements;

	for ( ; c != nullptr; c = strtok_r( nullptr, DELIMS, &c_save_ptr ) ) {

		size_t last_search_string_len = 0;

		if ( does_start_with( c, "comment", last_search_string_len ) ) {
			// Anything after a comment will be ignored
			continue;
		}

		else if ( does_start_with( c, "element", last_search_string_len ) ) {
			Element element;

			// Note: This method replaces spaces between in-element tokens with \0 characters.
			auto parse_element_line = []( char *c, Element &element ) -> bool {
				element.name = c;
				char *c_next = strchr( c, ' ' );
				if ( c_next == nullptr ) {
					// There must be a space character
					assert( false );
					return false;
				}
				*c_next          = 0; // insert an end-of-string token
				element.name_len = uint8_t( c_next - c );

				if ( 0 == strncmp( element.name, "vertex", element.name_len ) ) {
					element.type = Element::Type::eVertex;
				} else if ( 0 == strncmp( element.name, "face", element.name_len ) ) {
					element.type = Element::Type::eFace;
				}

				c = c_next + 1; // adding one because we don't want the zero terminator.

				element.num_elements = uint32_t( strtoul( c, nullptr, 0 ) );
				return true;
			};

			c += last_search_string_len + 1;

			// fetch name of element, and count of elements
			parse_element_line( c, element );

			elements.emplace_back( std::move( element ) );

			continue;
		}

		else if ( does_start_with( c, "property", last_search_string_len ) ) {
			Property property;

			// Note: this replaces spaces between in-element tokens with \0 characters.
			auto parse_property_line = []( char *c, Property &property ) -> bool {
				size_t last_search_string_len = 0;

				// now, we expect either list or [float|uchar] as property type
				if ( does_start_with( c, "list", last_search_string_len ) ) {
					c += last_search_string_len + 1;
					property.type = Property::Type::eList;

					// next item will be list size type

					if ( does_start_with( c, "uchar", last_search_string_len ) ) {
						property.list_size_type = Property::Type::eUchar;
						c += last_search_string_len + 1;
					} else if ( does_start_with( c, "uint", last_search_string_len ) ) {
						property.list_size_type = Property::Type::eUint;
						c += last_search_string_len + 1;
					} else {
						std::cerr << "Unknown list size type: '" << c << "'" << std::endl
						          << std::flush;
						assert( false );
					}

					// next item will be list content type

					if ( does_start_with( c, "uchar", last_search_string_len ) ) {
						property.list_content_type = Property::Type::eUchar;
						c += last_search_string_len + 1;
					} else if ( does_start_with( c, "uint", last_search_string_len ) ) {
						property.list_content_type = Property::Type::eUint;
						c += last_search_string_len + 1;
					} else {
						std::cerr << "Unknown list content type: '" << c << "'" << std::endl
						          << std::flush;
						assert( false );
					}

					// last item will be list name

					property.name     = c;
					property.name_len = uint8_t( strlen( c ) );
					return true;
				} else {

					// Non-list type

					if ( does_start_with( c, "float", last_search_string_len ) ) {
						property.type = Property::Type::eFloat;
					} else if ( does_start_with( c, "uint", last_search_string_len ) ) {
						property.type = Property::Type::eUint;
					} else if ( does_start_with( c, "uchar", last_search_string_len ) ) {
						property.type = Property::Type::eUchar;
					} else {
						// Unknown property type.
						std::cerr << __PRETTY_FUNCTION__ << ": Unknown property type: " << c << std::endl
						          << std::flush;
						assert( false );
						return false;
					}

					c += last_search_string_len + 1;
					property.name     = c;
					property.name_len = uint8_t( strlen( c ) );

					if ( 0 == strncmp( c, "x", property.name_len ) ) {
						property.attribute_type = Property::AttributeType::eVX;
					} else if ( 0 == strncmp( c, "y", property.name_len ) ) {
						property.attribute_type = Property::AttributeType::eVY;
					} else if ( 0 == strncmp( c, "z", property.name_len ) ) {
						property.attribute_type = Property::AttributeType::eVZ;
					} else if ( 0 == strncmp( c, "nx", property.name_len ) ) {
						property.attribute_type = Property::AttributeType::eNX;
					} else if ( 0 == strncmp( c, "ny", property.name_len ) ) {
						property.attribute_type = Property::AttributeType::eNY;
					} else if ( 0 == strncmp( c, "nz", property.name_len ) ) {
						property.attribute_type = Property::AttributeType::eNZ;
					} else if ( 0 == strncmp( c, "s", property.name_len ) ||
					            0 == strncmp( c, "u", property.name_len ) ) {
						property.attribute_type = Property::AttributeType::eTexU;
					} else if ( 0 == strncmp( c, "t", property.name_len ) ||
					            0 == strncmp( c, "v", property.name_len ) ) {
						property.attribute_type = Property::AttributeType::eTexV;
					} else if ( 0 == strncmp( c, "red", property.name_len ) ||
					            0 == strncmp( c, "r", property.name_len ) ) {
						property.attribute_type = Property::AttributeType::eColR;
					} else if ( 0 == strncmp( c, "green", property.name_len ) ||
					            0 == strncmp( c, "g", property.name_len ) ) {
						property.attribute_type = Property::AttributeType::eColG;
					} else if ( 0 == strncmp( c, "blue", property.name_len ) ||
					            0 == strncmp( c, "b", property.name_len ) ) {
						property.attribute_type = Property::AttributeType::eColB;
					} else if ( 0 == strncmp( c, "alpha", property.name_len ) ||
					            0 == strncmp( c, "a", property.name_len ) ) {
						property.attribute_type = Property::AttributeType::eColA;
					} else {
						std::cerr << "WARNING: Attribute name not recognised: '" << c << "'" << std::endl
						          << std::flush;
					}

					return true;
				}
				std::cerr << "Expected property type must be either 'list', or one of non-list type: uchar, float, uint, but given: " << c << std::endl
				          << std::flush;
				assert( false );
				return false;
			};

			c += last_search_string_len + 1;

			if ( !elements.empty() && parse_property_line( c, property ) ) {
				elements.back().properties.emplace_back( std::move( property ) );
			}

			continue;
		}

		else if ( does_start_with( c, "end_header", last_search_string_len ) ) {
			// we have reached the marker which signals the end of the header.
			// - Move file data pointer past "end_header" line
			c = strtok_r( nullptr, DELIMS, &c_save_ptr );
			break;
		}

		// The following code only gets executed if none of the above if clauses has
		// been triggered - this means that there is something wrong with
		// the file. we must exit.

		std::cerr << "ERROR: " << __PRETTY_FUNCTION__ << "Invalid file header data: '" << c << "'" << std::endl
		          << std::flush;

		assert( false );

		return false;
	}

	// - Clear mesh

	le_mesh_clear( self );

	// - Load file data

	Element const *element_archetype     = elements.data();
	auto const     element_archetype_end = elements.data() + elements.size();

	size_t element_idx = 0;

	// What follows now is a list of elements, one element per line.
	// elements have properties, which are separated by commas.

	for ( ; element_archetype != element_archetype_end; element_archetype++ ) {

		// Check whether the current property is still part of the current element
		// Otherwise move to the next element

		// Element archetype can be either face vertex or face

		if ( element_archetype->type == Element::Type::eVertex ) {

			// - Make space over all attributes for number of elements.

			for ( auto const &p : element_archetype->properties ) {
				switch ( p.attribute_type ) {
				case ( Property::AttributeType::eVX ): // intentional fall-through
				case ( Property::AttributeType::eVY ): // intentional fall-through
				case ( Property::AttributeType::eVZ ): // intentional fall-through
					self->vertices.resize( element_archetype->num_elements, {} );
					break;
				case ( Property::AttributeType::eNX ): // intentional fall-through
				case ( Property::AttributeType::eNY ): // intentional fall-through
				case ( Property::AttributeType::eNZ ): // intentional fall-through
					self->normals.resize( element_archetype->num_elements, {} );
					break;
				case ( Property::AttributeType::eColR ): // intentional fall-through
				case ( Property::AttributeType::eColG ): // intentional fall-through
				case ( Property::AttributeType::eColB ): // intentional fall-through
				case ( Property::AttributeType::eColA ): // intentional fall-through
					self->colours.resize( element_archetype->num_elements, {} );
					break;
				case ( Property::AttributeType::eTexU ): // intentional fall-through
				case ( Property::AttributeType::eTexV ): // intentional fall-through
					self->uvs.resize( element_archetype->num_elements, {} );
					break;
				case ( Property::AttributeType::eUnknown ):
					break;
				}
				// TODO: check for tangents.
			}

			for ( uint32_t i = 0; i != element_archetype->num_elements && c != nullptr; ++i, c = strtok_r( nullptr, DELIMS, &c_save_ptr ) ) {
				char *s = c;

				auto *v_data  = self->vertices.empty() ? nullptr : &self->vertices[ i ];
				auto *n_data  = self->normals.empty() ? nullptr : &self->normals[ i ];
				auto *uv_data = self->uvs.empty() ? nullptr : &self->uvs[ i ];
				auto *c_data  = self->colours.empty() ? nullptr : &self->colours[ i ];

				for ( auto const &p : element_archetype->properties ) {

					// clang-format off
					switch ( p.attribute_type ) {
					case ( Property::AttributeType::eVX )   : v_data->x  = strtof( s, &s ); break;
					case ( Property::AttributeType::eVY )   : v_data->y  = strtof( s, &s ); break;
					case ( Property::AttributeType::eVZ )   : v_data->z  = strtof( s, &s ); break;
					case ( Property::AttributeType::eNX )   : n_data->x  = strtof( s, &s ); break;
					case ( Property::AttributeType::eNY )   : n_data->y  = strtof( s, &s ); break;
					case ( Property::AttributeType::eNZ )   : n_data->z  = strtof( s, &s ); break;
					case ( Property::AttributeType::eTexU ) : uv_data->x = strtof( s, &s ); break;
					case ( Property::AttributeType::eTexV ) : uv_data->y = strtof( s, &s ); break;
					case ( Property::AttributeType::eColR ):
						c_data->x = p.type == Property::Type::eFloat ? strtof( s, &s ) : strtoul( s, &s, 0 )/255.f; break;
					case ( Property::AttributeType::eColG ):
						c_data->y = p.type == Property::Type::eFloat ? strtof( s, &s ) : strtoul( s, &s, 0 )/255.f; break;
					case ( Property::AttributeType::eColB ):
						c_data->z = p.type == Property::Type::eFloat ? strtof( s, &s ) : strtoul( s, &s, 0 )/255.f; break;
					case ( Property::AttributeType::eColA ):
						c_data->w = p.type == Property::Type::eFloat ? strtof( s, &s ) : strtoul( s, &s, 0 )/255.f; break;
					case ( Property::AttributeType::eUnknown ):
						// TODO: what do we do if there is an unknown attribute?
						assert( false );
						break;
					}
					// clang-format on
				}
			}

		} else if ( element_archetype->type == Element::Type::eFace ) {

			// must be 3 indices per face - because our meshes can only be built from triangles, not quads or anything else.
			self->indices.resize( element_archetype->num_elements * 3, {} );

			auto *      current_index = self->indices.data();
			auto *const indices_end   = self->indices.data() + self->indices.size();

			// this goes through line-by line.
			for ( size_t line_num = 0;
			      ( line_num != element_archetype->num_elements ) && ( current_index < indices_end ) && ( c != nullptr );
			      ++line_num ) {

				char *s = c;

				auto three = strtoul( s, &s, 0 );
				assert( three == 3 ); // first element must be three

				*current_index++ = strtoul( s, &s, 0 ); // first index
				*current_index++ = strtoul( s, &s, 0 ); // second index
				*current_index++ = strtoul( s, &s, 0 ); // third index

				c = strtok_r( nullptr, DELIMS, &c_save_ptr );
			}

			// parse face properties
		}
		if ( element_archetype->type == Element::Type::eUnknown ) {
			// Not implemented yet.

			auto skip_lines = [&]( char const *c, Element const *archetype ) {
				for ( uint32_t i = 0; i != archetype->num_elements && c != nullptr; ++i ) {
					c = strtok_r( nullptr, DELIMS, &c_save_ptr );
				}
			};

			skip_lines( c, element_archetype );

			break;
		}
	}

	return true;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_mesh_api( void *api ) {
	auto &le_mesh_i = static_cast<le_mesh_api *>( api )->le_mesh_i;

	le_mesh_i.get_vertices = le_mesh_get_vertices;
	le_mesh_i.get_indices  = le_mesh_get_indices;
	le_mesh_i.get_uvs      = le_mesh_get_uvs;
	le_mesh_i.get_tangents = le_mesh_get_tangents;
	le_mesh_i.get_normals  = le_mesh_get_normals;
	le_mesh_i.get_colours  = le_mesh_get_colours;
	le_mesh_i.get_data     = le_mesh_get_data;

	le_mesh_i.load_from_ply_file = le_mesh_load_from_ply_file;

	le_mesh_i.clear   = le_mesh_clear;
	le_mesh_i.create  = le_mesh_create;
	le_mesh_i.destroy = le_mesh_destroy;
}
