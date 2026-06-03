#include "le_mesh.h"

#include <vector>
#include <filesystem> // for file loading
#include <iostream>   // for file loading
#include <fstream>    // for file loading
#include <iomanip>    // for file loading
#include <cstring>
#include <cassert>

#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "le_log.h"

#include <map>

auto& logger() {
	static le::Log l( "le_mesh_ply.cpp" );
	return l;
}

#ifdef _WIN32
#	define __PRETTY_FUNCTION__ __FUNCSIG__
#	define strtok_r strtok_s
#endif //

// ----------------------------------------------------------------------
/// \brief   file loader utility method
/// \details loads file given by filepath and returns a vector of chars if successful
/// \note    returns an empty vector if not successful
static std::vector<char> load_file( const std::filesystem::path& file_path, bool* success ) {

	std::vector<char> contents;

	size_t        fileSize = 0;
	std::ifstream file( file_path, std::ios::in | std::ios::binary | std::ios::ate );

	if ( !file.is_open() ) {
		logger().error( "Unable to open file: '%s'", std::filesystem::canonical( file_path ).string().c_str() );
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

static inline int does_start_with( char const* haystack, char const* needle, size_t& needle_len ) {
	needle_len = strlen( needle );
	return 0 == strncmp( haystack, needle, needle_len );
};

// ----------------------------------------------------------------------
/// \brief loads mesh from ply file
/// \note any contents of mesh will be cleared before loading
/// \return true upon success, false otherwise.
static bool le_mesh_load_from_ply_file( le_mesh_o* self, char const* file_path_, bool should_interleave, le_renderer_o* optional_renderer ) {

	// - Make sure file exists

	std::filesystem::path file_path{ file_path_ };

	if ( !std::filesystem::exists( file_path ) ) {
		logger().error( "File not found: '%s'", file_path.string().c_str() );
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
		char const*   name              = nullptr;
		uint8_t       name_len          = 0; ///< number of chars for name (does not include \0)
	};

	struct Element {

		enum class Type : uint8_t {
			eUnknown,
			eVertex,
			eFace,
		};
		char const*           name;
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
		logger().warn( "File could not be loaded: '%s'", file_path.string().c_str() );
		return false;
	}

	static auto DELIMS{ "\r\n\0" };
	char*       c_save_ptr; //< we use the re-entrant version of strtok, for which state is stored in here

	// --------| invariant: file was loaded.

	char* c = strtok_r( file_data.data(), DELIMS, &c_save_ptr );

	if ( 0 != strcmp( c, "ply" ) ) {
		logger().warn( "Invalid file header: '%s'", file_path.string().c_str() );
		return false;
	}

	c = strtok_r( nullptr, DELIMS, &c_save_ptr );

	if ( 0 != strcmp( c, "format ascii 1.0" ) ) {
		logger().warn( "Invalid file header: '%s'", file_path.string().c_str() );
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
			auto parse_element_line = []( char* c, Element& element ) -> bool {
				element.name = c;
				char* c_next = strchr( c, ' ' );
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
			auto parse_property_line = []( char* c, Property& property ) -> bool {
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
						logger().error( "Unknown list size type: '%s'", c );
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
						logger().error( "Unknown list content type: '%s'", c );
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
						logger().error( ": Unknown property type: %s", c );
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
						logger().warn( "Attribute name not recognised: '%s'", c );
					}

					return true;
				}
				logger().error( "Expected property type must be either 'list', or one of non-list type: uchar, float, uint, but given: %s", c );
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

		logger().error( "Invalid file header data: '%s'", c );

		assert( false );

		return false;
	}

	// - Clear mesh

	le_mesh_api_i->le_mesh_i.clear( self );

	size_t num_vertices = 0;

	// --

	// - Load file data

	Element const* element_archetype     = elements.data();
	auto const     element_archetype_end = elements.data() + elements.size();

	auto skip_comments_or_empty_lines = [ & ]() {
		// skip any comments and empty lines
		size_t needle_len = 1;
		if ( does_start_with( c, "#", needle_len ) ||
		     does_start_with( c, "\n", needle_len ) ||
		     does_start_with( c, "\r", needle_len ) ) {
			c = strtok_r( nullptr, DELIMS, &c_save_ptr );
		}
	};

	// What follows now is a list of elements, one element per line.
	// elements have properties, which are separated by commas.

	for ( ; element_archetype != element_archetype_end; element_archetype++ ) {

		skip_comments_or_empty_lines();

		// Check whether the current property is still part of the current element
		// Otherwise move to the next element

		// Element archetype can be either face vertex or face

		if ( element_archetype->type == Element::Type::eVertex ) {

			// - Make space over all attributes for number of elements.
			if ( num_vertices == 0 ) {
				bool was_reallocated = false;
				le_mesh_api_i->le_mesh_i.set_vertex_count( self, element_archetype->num_elements, &was_reallocated );
				num_vertices = element_archetype->num_elements;
			}

			std::map<le_mesh_attribute_name, le_mesh_attribute_info_t> attribute_infos;

			size_t           per_vertex_stride = 0;
			glm::vec3*       pos_data          = nullptr;
			glm::vec3 const* pos_data_end      = nullptr;
			glm::vec3*       normals_data      = nullptr;
			glm::vec3 const* normals_data_end  = nullptr;
			glm::vec2*       uvs_data          = nullptr;
			glm::vec2 const* uvs_data_end      = nullptr;
			glm::vec4*       colours_data      = nullptr;
			glm::vec4 const* colours_data_end  = nullptr;

			size_t pos_data_per_vertex_offset     = 0;
			size_t normals_data_per_vertex_offset = 0;
			size_t uvs_data_per_vertex_offset     = 0;
			size_t colours_data_per_vertex_offset = 0;

			for ( auto const& p : element_archetype->properties ) {

				switch ( p.attribute_type ) {
				case ( Property::AttributeType::eVX ): // intentional fall-through
				case ( Property::AttributeType::eVY ): // intentional fall-through
				case ( Property::AttributeType::eVZ ): // intentional fall-through
				{
					constexpr le_mesh_attribute_info_t attr_info = { .name = le_mesh_attribute_name::ePosition, .bytes_per_vertex = sizeof( glm::vec3 ) };
					if ( should_interleave ) {
						attribute_infos[ le_mesh_attribute_name::ePosition ] = attr_info;
					} else if ( pos_data == nullptr ) {
						pos_data     = ( glm::vec3* )le_mesh_api_i->le_mesh_i.allocate_vertex_data( self, &attr_info, 1, optional_renderer );
						pos_data_end = pos_data + sizeof( glm::vec3 ) * num_vertices;
					}
				} break;
				case ( Property::AttributeType::eNX ): // intentional fall-through
				case ( Property::AttributeType::eNY ): // intentional fall-through
				case ( Property::AttributeType::eNZ ): // intentional fall-through
				{
					constexpr le_mesh_attribute_info_t attr_info = { .name = le_mesh_attribute_name::eNormal, .bytes_per_vertex = sizeof( glm::vec3 ) };
					if ( should_interleave ) {
						attribute_infos[ le_mesh_attribute_name::eNormal ] = attr_info;
					} else if ( normals_data == nullptr ) {
						normals_data     = ( glm::vec3* )le_mesh_api_i->le_mesh_i.allocate_vertex_data( self, &attr_info, 1, optional_renderer );
						normals_data_end = normals_data + sizeof( glm::vec3 ) * num_vertices;
					}
				} break;
				case ( Property::AttributeType::eColR ): // intentional fall-through
				case ( Property::AttributeType::eColG ): // intentional fall-through
				case ( Property::AttributeType::eColB ): // intentional fall-through
				case ( Property::AttributeType::eColA ): // intentional fall-through
				{
					constexpr le_mesh_attribute_info_t attr_info = { .name = le_mesh_attribute_name::eColour, .bytes_per_vertex = sizeof( glm::vec4 ) };
					if ( should_interleave ) {
						attribute_infos[ le_mesh_attribute_name::eColour ] = attr_info;
					} else if ( colours_data == nullptr ) {
						colours_data     = ( glm::vec4* )le_mesh_api_i->le_mesh_i.allocate_vertex_data( self, &attr_info, 1, optional_renderer );
						colours_data_end = colours_data + sizeof( glm::vec4 ) * num_vertices;
					}
				} break;
				case ( Property::AttributeType::eTexU ): // intentional fall-through
				case ( Property::AttributeType::eTexV ): // intentional fall-through
				{
					constexpr le_mesh_attribute_info_t attr_info = { .name = le_mesh_attribute_name::eUv, .bytes_per_vertex = sizeof( glm::vec2 ) };
					if ( should_interleave ) {
						attribute_infos[ le_mesh_attribute_name::eUv ] = attr_info;
					} else if ( uvs_data == nullptr ) {
						uvs_data     = ( glm::vec2* )le_mesh_api_i->le_mesh_i.allocate_vertex_data( self, &attr_info, 1, optional_renderer );
						uvs_data_end = uvs_data + sizeof( glm::vec2 ) * num_vertices;
					}
				} break;
				case ( Property::AttributeType::eUnknown ):
					break;
				}
				// TODO: check for tangents.
			}

			if ( should_interleave ) {

				std::vector<le_mesh_attribute_info_t> attribute_info_vec;

				for ( auto& [ key, item ] : attribute_infos ) {
					attribute_info_vec.push_back( item );

					switch ( key ) {
					case le_mesh_attribute_name::eUndefined:
						break;
					case le_mesh_attribute_name::ePosition:
						pos_data_per_vertex_offset = per_vertex_stride;
						break;
					case le_mesh_attribute_name::eNormal:
						normals_data_per_vertex_offset = per_vertex_stride;
						break;
					case le_mesh_attribute_name::eColour:
						colours_data_per_vertex_offset = per_vertex_stride;
						break;
					case le_mesh_attribute_name::eUv:
						uvs_data_per_vertex_offset = per_vertex_stride;
						break;
					case le_mesh_attribute_name::eTangent:
						assert( false && "tangent import not yet implemented" );
						break;
					default:
						break;
					}

					per_vertex_stride += item.bytes_per_vertex;
				}

				void* data = le_mesh_api_i->le_mesh_i.allocate_vertex_data( self, attribute_info_vec.data(), attribute_info_vec.size(), optional_renderer );

				for ( auto const& info : attribute_info_vec ) {

					switch ( info.name ) {
					case le_mesh_attribute_name::eUndefined:
						break;
					case le_mesh_attribute_name::ePosition:
						pos_data     = ( glm::vec3* )( ( uint8_t* )( data ) + pos_data_per_vertex_offset );
						pos_data_end = ( glm::vec3* )( ( uint8_t* )( data ) + pos_data_per_vertex_offset + per_vertex_stride * num_vertices );
						break;
					case le_mesh_attribute_name::eNormal:
						normals_data     = ( glm::vec3* )( ( uint8_t* )( data ) + normals_data_per_vertex_offset );
						normals_data_end = ( glm::vec3* )( ( uint8_t* )( data ) + normals_data_per_vertex_offset + per_vertex_stride * num_vertices );
						break;
					case le_mesh_attribute_name::eColour:
						colours_data     = ( glm::vec4* )( ( uint8_t* )( data ) + colours_data_per_vertex_offset );
						colours_data_end = ( glm::vec4* )( ( uint8_t* )( data ) + colours_data_per_vertex_offset + per_vertex_stride * num_vertices );
						break;
					case le_mesh_attribute_name::eUv:
						uvs_data     = ( glm::vec2* )( ( uint8_t* )( data ) + uvs_data_per_vertex_offset );
						uvs_data_end = ( glm::vec2* )( ( uint8_t* )( data ) + uvs_data_per_vertex_offset + per_vertex_stride * num_vertices );
						break;
					case le_mesh_attribute_name::eTangent:
						assert( false && "tangent import not yet implemented" );
						break;
					default:
						break;
					}
				}
			}

			size_t count_positions = 0;

			for ( uint32_t i = 0; i != element_archetype->num_elements && c != nullptr; ++i, c = strtok_r( nullptr, DELIMS, &c_save_ptr ) ) {

				if ( colours_data ) {
					colours_data->w = 1.f; // initialize colour alpha to 1 in case it does not get set
				}

				skip_comments_or_empty_lines();

				char* s = c;

				if ( pos_data ) {
					assert( pos_data < pos_data_end );
				}
				if ( normals_data ) {
					assert( normals_data < normals_data_end );
				}
				if ( uvs_data ) {
					assert( uvs_data < uvs_data_end );
				}
				if ( colours_data ) {
					assert( colours_data < colours_data_end );
				}


				for ( auto const& p : element_archetype->properties ) {

					// todo: you need to iterate the archetype element

					// clang-format off
					switch ( p.attribute_type ) {
					case ( Property::AttributeType::eVX )   : pos_data->x  = strtof( s, &s ); break;
					case ( Property::AttributeType::eVY )   : pos_data->y  = strtof( s, &s ); break;
					case ( Property::AttributeType::eVZ )   : pos_data->z  = strtof( s, &s ); break;
					case ( Property::AttributeType::eNX )   : if ( normals_data ) { normals_data->x  = strtof( s, &s ); } break;
					case ( Property::AttributeType::eNY )   : if ( normals_data ) { normals_data->y  = strtof( s, &s ); } break;
					case ( Property::AttributeType::eNZ )   : if ( normals_data ) { normals_data->z  = strtof( s, &s ); } break;
					case ( Property::AttributeType::eTexU ) : if ( uvs_data     ) { uvs_data->x      = strtof( s, &s );      } break;
					case ( Property::AttributeType::eTexV ) : if ( uvs_data     ) { uvs_data->y      = strtof( s, &s );      } break;
					case ( Property::AttributeType::eColR ) : if ( colours_data ) { colours_data->x  = p.type == Property::Type::eFloat ? strtof( s, &s ) : strtoul( s, &s, 0 )/255.f; break; }
					case ( Property::AttributeType::eColG ) : if ( colours_data ) { colours_data->y  = p.type == Property::Type::eFloat ? strtof( s, &s ) : strtoul( s, &s, 0 )/255.f; break; }
					case ( Property::AttributeType::eColB ) : if ( colours_data ) { colours_data->z  = p.type == Property::Type::eFloat ? strtof( s, &s ) : strtoul( s, &s, 0 )/255.f; break; }
					case ( Property::AttributeType::eColA ) : if ( colours_data ) { colours_data->w  = p.type == Property::Type::eFloat ? strtof( s, &s ) : strtoul( s, &s, 0 )/255.f; break; }
					case ( Property::AttributeType::eUnknown ):
						// clang-format on
						// TODO: what do we do if there is an unknown attribute?
						assert( false );
						break;
					}
				}

				// clang-format off
				if ( should_interleave ) {
					pos_data     = pos_data     ? ( glm::vec3* )( ( uint8_t* )( pos_data     ) + per_vertex_stride ) : nullptr;
					normals_data = normals_data ? ( glm::vec3* )( ( uint8_t* )( normals_data ) + per_vertex_stride ) : nullptr;
					uvs_data     = uvs_data     ? ( glm::vec2* )( ( uint8_t* )( uvs_data     ) + per_vertex_stride ) : nullptr;
					colours_data = colours_data ? ( glm::vec4* )( ( uint8_t* )( colours_data ) + per_vertex_stride ) : nullptr;
				} else {
					pos_data     = pos_data     ? pos_data     + 1 : nullptr;
					normals_data = normals_data ? normals_data + 1 : nullptr;
					uvs_data     = uvs_data     ? uvs_data     + 1 : nullptr;
					colours_data = colours_data ? colours_data + 1 : nullptr;
				}
				// clang-format on
			}

		} else if ( element_archetype->type == Element::Type::eFace ) {

			// must be 3 indices per face - because our meshes can only be built from triangles, not quads or anything else.

			uint32_t num_bytes_per_index = sizeof( uint16_t ); // hint for 16bit indices, might be updated mesh-side if it detects that it has more than 65535 vertices.
			size_t   num_indices         = element_archetype->num_elements * 3;
			void*    index_data          = le_mesh::le_mesh_i.allocate_index_data( self, num_indices, &num_bytes_per_index, nullptr );

			// In case the mesh has more than 65535 vertices, index type will automatically have
			// been assigned to be uint32_t, in which case the bytes per index will be 4.

			if ( num_bytes_per_index == 2 ) {
				auto       current_index = ( uint16_t* )index_data;
				auto const indices_end   = current_index + num_indices;

				// this goes through line-by line.
				for ( size_t line_num = 0;
				      ( line_num != element_archetype->num_elements ) && ( current_index < indices_end ) && ( c != nullptr );
				      ++line_num ) {

					char* s = c;

					skip_comments_or_empty_lines();

					auto three = strtoul( s, &s, 0 );
					assert( three == 3 ); // first element must be three

					*current_index++ = strtoul( s, &s, 0 ); // first index
					*current_index++ = strtoul( s, &s, 0 ); // second index
					*current_index++ = strtoul( s, &s, 0 ); // third index

					c = strtok_r( nullptr, DELIMS, &c_save_ptr );
				}
			} else if ( num_bytes_per_index == 4 ) {
				auto       current_index = ( uint32_t* )index_data;
				auto const indices_end   = current_index + num_indices;

				// this goes through line-by line.
				for ( size_t line_num = 0;
				      ( line_num != element_archetype->num_elements ) && ( current_index < indices_end ) && ( c != nullptr );
				      ++line_num ) {

					char* s = c;
					skip_comments_or_empty_lines();

					auto three = strtoul( s, &s, 0 );
					assert( three == 3 ); // first element must be three

					*current_index++ = strtoul( s, &s, 0 ); // first index
					*current_index++ = strtoul( s, &s, 0 ); // second index
					*current_index++ = strtoul( s, &s, 0 ); // third index

					c = strtok_r( nullptr, DELIMS, &c_save_ptr );
				}
			}

			// parse face properties
		}
		if ( element_archetype->type == Element::Type::eUnknown ) {
			// Not implemented yet.

			auto skip_lines = [ & ]( char const* c, Element const* archetype ) {
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

ISL_API_ATTR void le_module_register_le_mesh_load_from_ply( void* api ) {
	auto& le_mesh_i              = static_cast<le_mesh_api*>( api )->le_mesh_i;
	le_mesh_i.load_from_ply_file = le_mesh_load_from_ply_file;
}
