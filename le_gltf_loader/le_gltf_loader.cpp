#include "le_gltf_loader.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "fx/gltf.h"

#define VULKAN_HPP_NO_SMART_HANDLE
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>
#include <map>

#include "le_renderer/le_renderer.h"
#include "le_renderer/private/hash_util.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#define GLM_ENABLE_EXPERIMENTAL
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtx/quaternion.hpp"

struct GltfUboMvp {
	glm::mat4 projection;
	glm::mat4 model;
	glm::mat4 view;
};

using namespace fx;

// ----------------------------------------------------------------------

struct Primitive {

	struct IndexData {
		gltf::BufferView   indexBuffer; // maybe document index buffer
		le::ResourceHandle cachedIndexBuffer = nullptr;
		uint64_t           cachedIndexOffset;
		size_t             numIndices = 0;
	};

	std::vector<le_vertex_input_attribute_description> attribute_descriptions; // location->
	std::vector<le_vertex_input_binding_description>   binding_descriptions;   // binding->
	std::vector<int32_t>                               boundBufferViews;       // gltf document indices of bound bufferViews

	std::optional<IndexData> indexData;

	size_t                        numVertices   = 0;       //
	le_graphics_pipeline_state_o *pipelineState = nullptr; // pipelineState

	struct PbrProperties *materialProperties = nullptr; // material properties

	std::vector<le::ResourceHandle> cachedBuffers;
	std::vector<uint64_t>           cachedOffsets;
};

struct Mesh {
	std::vector<Primitive> primitives;
};

// ----------------------------------------------------------------------

struct le_gltf_document_o {
	gltf::Document document;

	std::vector<le::ResourceHandle> bufferResources;
	std::vector<le_resource_info_t> bufferResourceInfos; // populated via loadDocument

	std::vector<le::ResourceHandle> imageResources;
	std::vector<le_resource_info_t> imageResourceInfos;

	std::vector<Mesh> meshes;

	bool isDirty = true; // true means data on gpu is not up to date, and needs upload
};

// ----------------------------------------------------------------------

static le_gltf_document_o *document_create() {
	auto self = new le_gltf_document_o();
	return self;
}

// ----------------------------------------------------------------------

static void document_destroy( le_gltf_document_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

enum class AttributeType : uint32_t {
	ePosition   = 0,
	eNormal     = 1,
	eTangent    = 2,
	eTexCoord_0 = 3,
	eTexCoord_1 = 4,
	eColor_0    = 5,
	eJoints_0   = 6,
	eWeights_0  = 7,
};

enum class AccessorType : uint32_t {
	eFloat         = 5126,
	eUnsignedByte  = 5121,
	eUnsignedShort = 5123,
};

template <typename T>
typename std::underlying_type<T>::type enum_to_num( const T &a ) {
	return static_cast<typename std::underlying_type<T>::type>( a );
}

static uint8_t vec_size_from_gltf_type( const gltf::Accessor::Type &t ) {
	switch ( t ) {
	case gltf::Accessor::Type::Scalar:
	    return 1;
	case gltf::Accessor::Type::Vec2:
	    return 2;
	case gltf::Accessor::Type::Vec3:
	    return 3;
	case gltf::Accessor::Type::Vec4:
	    return 4;
	default:
	    return 0;
	}
};

// ----------------------------------------------------------------------

static void getAttrInfo( const std::string &attrName, const gltf::Accessor &acc, le_vertex_input_attribute_description &attr ) {
	// see: https://github.com/KhronosGroup/glTF/blob/master/specification/2.0/README.md#meshes

	attr.vecsize = vec_size_from_gltf_type( acc.type );

	if ( attrName == "POSITION" ) {
		attr.location = enum_to_num( AttributeType::ePosition );
	} else if ( attrName == "NORMAL" ) {
		attr.location = enum_to_num( AttributeType::eNormal );
	} else if ( attrName == "TANGENT" ) {
		attr.location = enum_to_num( AttributeType::eTangent );
	} else if ( attrName == "TEXCOORD_0" ) {
		attr.location     = enum_to_num( AttributeType::eTexCoord_0 );
		attr.isNormalised = true;
	} else if ( attrName == "TEXCOORD_1" ) {
		attr.location     = enum_to_num( AttributeType::eTexCoord_1 );
		attr.isNormalised = true;
	} else if ( attrName == "COLOR_0" ) {
		attr.location     = enum_to_num( AttributeType::eColor_0 );
		attr.isNormalised = true;
	} else if ( attrName == "JOINTS_0" ) {
		attr.location = enum_to_num( AttributeType::eJoints_0 );
	} else if ( attrName == "WEIGHTS_0" ) {
		attr.location     = enum_to_num( AttributeType::eWeights_0 );
		attr.isNormalised = true;
	} else {
		assert( false ); // attribute name not recognised.
	}

	switch ( acc.componentType ) {
	case ( gltf::Accessor::ComponentType::None ):
		// invalid component type
	    break;
	case ( gltf::Accessor::ComponentType::Byte ):
	    break;
	case ( gltf::Accessor::ComponentType::UnsignedByte ):
		attr.type = le_vertex_input_attribute_description::Type::eChar;
	    break;
	case ( gltf::Accessor::ComponentType::Short ):
	    break;
	case ( gltf::Accessor::ComponentType::UnsignedShort ):
		attr.type = le_vertex_input_attribute_description::Type::eShort;
	    break;
	case ( gltf::Accessor::ComponentType::UnsignedInt ):
	    break;
	case ( gltf::Accessor::ComponentType::Float ):
		attr.type         = le_vertex_input_attribute_description::Type::eFloat;
		attr.isNormalised = false; // Floats are never normalised
	    break;
	}
};

// ----------------------------------------------------------------------

static bool document_load_from_text( le_gltf_document_o *self, const char *path ) {
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

		self->meshes.clear();
		self->bufferResourceInfos.clear();
		self->imageResourceInfos.clear();

		// we must find out what kind of pipelines this document requires.
		// for this, we must iterate over all meshes
		// - for each mesh, calculate attribute bindings
		// - for each mesh, gather materials

		for ( const auto &mesh : doc.meshes ) {

			Mesh m;

			for ( const auto &primitive : mesh.primitives ) {

				Primitive p;

				std::map<uint32_t, le_vertex_input_attribute_description> tmpAttributeDescriptions; // location->
				std::map<uint32_t, le_vertex_input_binding_description>   tmpBindingDescriptions;   // binding->

				for ( const auto &attribute : primitive.attributes ) {

					auto &accessor = doc.accessors[ attribute.second ];

					le_vertex_input_attribute_description attr{};
					le_vertex_input_binding_description   binding{};

					// Try to find buffer for this buffer view in bound buffers
					// - if we can find it, store found index in binding
					// - otherwise add buffer to bound buffers, and store new index in binding
					//
					uint32_t bufferViewIndex = 0;
					{
						const auto bufferView = accessor.bufferView;
						for ( ; bufferViewIndex < p.boundBufferViews.size(); ++bufferViewIndex ) {
							if ( p.boundBufferViews[ bufferViewIndex ] == bufferView ) {
								break;
							}
						}
						if ( bufferViewIndex == p.boundBufferViews.size() ) {
							// bufferIndex was not found in list of bound buffers, we need to add it.
							p.boundBufferViews.push_back( bufferView );
						}
					}

					// Number of vertices must be the same for every accessor
					// we're repeatedly writing this value to the primitive, but
					// as it must be the same for each vertex which is part of the
					// same primitive this should not matter.
					p.numVertices = accessor.count;

					attr.binding        = bufferViewIndex;
					attr.binding_offset = accessor.byteOffset;

					getAttrInfo( attribute.first, accessor, attr );

					tmpAttributeDescriptions[ attr.location ] = attr;

					// which binding slot to use - we must then store somewhere that we need this particular buffer
					// be bound at the slot.
					binding.binding = bufferViewIndex;

					{
						// calculate byte stride
						uint32_t bufferViewStride = doc.bufferViews[ size_t( accessor.bufferView ) ].byteStride;

						if ( bufferViewStride ) {
							binding.stride = bufferViewStride;
						} else {
							// this means the accessor has the bufferview exclusively. this means data is tightly packed,
							// and we can calculate the stride by dividing buffer view length/accessor count
							auto const &bufView = doc.bufferViews[ size_t( accessor.bufferView ) ];
							binding.stride      = bufView.byteLength / accessor.count;
						}
					}
					binding.input_rate = le_vertex_input_binding_description::ePerVertex;

					// TODO: check  - if stride is identical

					auto it = tmpBindingDescriptions.emplace( uint32_t( attr.binding ), binding );
					if ( it.second == false ) {
						// element was already present .. make sure stride is identical
						assert( it.first->second.stride == binding.stride );   // stride must be identical for all attributes of the same primitive
						assert( it.first->second.binding == binding.binding ); // binding must be identical for all attributes of the same primitive
					}
				}

				// Now check if primitive has indices.

				if ( primitive.indices != -1 ) {

					auto &           indexAcc        = doc.accessors[ primitive.indices ];
					gltf::BufferView indexBufferView = doc.bufferViews[ indexAcc.bufferView ];

					// Although it is unlikely that the accessor for index data has an offset,
					// we must account for this.
					indexBufferView.byteOffset += indexAcc.byteOffset;

					p.indexData              = std::make_optional<Primitive::IndexData>();
					p.indexData->indexBuffer = indexBufferView;
					p.indexData->numIndices  = indexAcc.count; // number of indices in this case

				} else {
					// we must use the count of vertices to draw vertices
				}

				// Flatten vertex binding, and vertex attribute info

				for ( auto &a : tmpAttributeDescriptions ) {
					p.attribute_descriptions.emplace_back( a.second );
				}

				for ( auto &b : tmpBindingDescriptions ) {
					p.binding_descriptions.emplace_back( b.second );
				}

				// Store primitive in mesh

				m.primitives.emplace_back( p );
			}

			// store mesh in document mesh array

			self->meshes.emplace_back( m );
		}

		for ( const auto &b : doc.buffers ) {
			le_resource_info_t resInfo;
			resInfo.type         = LeResourceType::eBuffer;
			resInfo.buffer.size  = b.byteLength;
			resInfo.buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			self->bufferResourceInfos.emplace_back( resInfo );
		}

		std::swap( self->document, doc );
	}

	return result;
}

// ----------------------------------------------------------------------

static void document_declare_resources( le_gltf_document_o *self, le_renderer_o *renderer ) {

	static const auto &renderer_i = Registry::getApi<le_renderer_api>()->le_renderer_i;

	self->bufferResources.resize( self->bufferResourceInfos.size(), nullptr );

	for ( size_t i = 0; i != self->bufferResourceInfos.size(); ++i ) {
		self->bufferResources[ i ] = renderer_i.declare_resource( renderer, LeResourceType::eBuffer );
	}

	for ( auto &m : self->meshes ) {

		for ( auto &p : m.primitives ) {

			// Cache buffer lookups for primitives
			p.cachedBuffers.clear();
			p.cachedOffsets.clear();

			for ( const auto &view : p.boundBufferViews ) {
				p.cachedOffsets.emplace_back( self->document.bufferViews[ view ].byteOffset );
				p.cachedBuffers.emplace_back( self->bufferResources[ self->document.bufferViews[ view ].buffer ] );
			}

			if ( p.indexData ) {
				p.indexData->cachedIndexBuffer = self->bufferResources[ p.indexData->indexBuffer.buffer ];
				p.indexData->cachedIndexOffset = p.indexData->indexBuffer.byteOffset;
			}

			{
				le_graphics_pipeline_create_info_t pipelineInfo;
				pipelineInfo.shader_module_vert = renderer_i.create_shader_module( renderer, "./resources/shaders/pbr.vert", LeShaderType::eVert );
				pipelineInfo.shader_module_frag = renderer_i.create_shader_module( renderer, "./resources/shaders/pbr.frag", LeShaderType::eFrag );

				pipelineInfo.vertex_input_attribute_descriptions       = p.attribute_descriptions.data();
				pipelineInfo.vertex_input_attribute_descriptions_count = p.attribute_descriptions.size();
				pipelineInfo.vertex_input_binding_descriptions         = p.binding_descriptions.data();
				pipelineInfo.vertex_input_binding_descriptions_count   = p.binding_descriptions.size();

				p.pipelineState = renderer_i.create_graphics_pipeline_state_object( renderer, &pipelineInfo );
			}

		} // end for:primitives
	}     // end for:meshes
}

// ----------------------------------------------------------------------
// you have to get these resource infos from a transfer renderpass,
// to declare resources for the rendergraph
static void document_get_create_resource_infos( le_gltf_document_o *self, le_resource_info_t **infos, LeResourceHandle const **handles, size_t *numResources ) {
	*infos        = self->bufferResourceInfos.data();
	*handles      = reinterpret_cast<LeResourceHandle const *>( self->bufferResources.data() );
	*numResources = self->bufferResourceInfos.size();
}

// ----------------------------------------------------------------------

static void document_upload_resource_data( le_gltf_document_o *self, le_command_buffer_encoder_o *encoder ) {

	if ( self->isDirty == false ) {
		return;
	}

	// ---------| invariant: data needs to be uploaded to GPU

	static const auto &encoder_i = Registry::getApi<le_renderer_api>()->le_command_buffer_encoder_i;

	// upload data for all resources
	for ( size_t i = 0; i != self->document.buffers.size(); ++i ) {
		assert( self->bufferResources[ i ] != le::ResourceHandle{} );
		encoder_i.write_to_buffer( encoder, self->bufferResources[ i ], 0, self->document.buffers[ i ].data.data(), self->document.buffers[ i ].data.size() );
	}

	self->isDirty = false;
}

// ----------------------------------------------------------------------

static void document_draw_node( le_gltf_document_o const *   doc,
                                const size_t                 nodeIndex,
                                glm::mat4 const &            matrix,
                                le_command_buffer_encoder_o *encoder,
                                GltfUboMvp const *           mvp ) {

	static const auto &encoder_i = Registry::getApi<le_renderer_api>()->le_command_buffer_encoder_i;

	const auto &node = doc->document.nodes[ nodeIndex ];

	glm::mat4 localMatrix = matrix *
	                        glm::translate( glm::mat4{1}, reinterpret_cast<const glm::vec3 &>( node.translation ) ) *
	                        glm::toMat4( reinterpret_cast<const glm::quat &>( node.rotation ) ) *
	                        glm::scale( glm::mat4{1}, reinterpret_cast<const glm::vec3 &>( node.scale ) ) *
	                        reinterpret_cast<const glm::mat4 &>( node.matrix );

	if ( node.mesh != -1 ) {
		// draw all primitives from this mesh

		auto &m = doc->meshes[ size_t( node.mesh ) ];

		struct GltfUboNode {
			glm::mat4 matrix;
		} uboNode;

		uboNode.matrix = localMatrix;

		for ( auto const &p : m.primitives ) {

			assert( p.pipelineState != nullptr ); // pipeline state must exist
			encoder_i.bind_graphics_pipeline( encoder, p.pipelineState );

			encoder_i.set_argument_ubo_data( encoder, const_char_hash64( "UBO" ), mvp, sizeof( GltfUboMvp ) );
			encoder_i.set_argument_ubo_data( encoder, const_char_hash64( "UBONode" ), &uboNode, sizeof( GltfUboNode ) );

			encoder_i.bind_vertex_buffers( encoder, 0, uint32_t( p.cachedBuffers.size() ), reinterpret_cast<LeResourceHandle const *>( p.cachedBuffers.data() ), p.cachedOffsets.data() );

			if ( p.indexData ) {
				encoder_i.bind_index_buffer( encoder, p.indexData->cachedIndexBuffer, p.indexData->cachedIndexOffset, 0 );
				encoder_i.draw_indexed( encoder, uint32_t( p.indexData->numIndices ), 1, 0, 0, 0 );
			} else {
				encoder_i.draw( encoder, uint32_t( p.numVertices ), 1, 0, 0 );
			}
		}
	}
	for ( auto nI : node.children ) {
		document_draw_node( doc, size_t( nI ), localMatrix, encoder, mvp );
	}
}

// ----------------------------------------------------------------------

static void document_draw( le_gltf_document_o *self, le_command_buffer_encoder_o *encoder, GltfUboMvp const *mvp ) {

	gltf::Scene *scene;
	if ( self->document.scene != -1 ) {
		scene = &self->document.scenes[ self->document.scene ];
	} else if ( !self->document.scenes.empty() ) {
		scene = &self->document.scenes.front();
	} else {
		return;
	}

	for ( auto const &nI : scene->nodes ) {
		document_draw_node( self, nI, glm::mat4( 1 ), encoder, mvp );
	}
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_gltf_loader_api( void *api ) {
	auto &document_i = static_cast<le_gltf_loader_api *>( api )->document_i;

	document_i.create                    = document_create;
	document_i.destroy                   = document_destroy;
	document_i.load_from_text            = document_load_from_text;
	document_i.declare_resources         = document_declare_resources;
	document_i.get_create_resource_infos = document_get_create_resource_infos;
	document_i.upload_resource_data      = document_upload_resource_data;
	document_i.draw                      = document_draw;
}
