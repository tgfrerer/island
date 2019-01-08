#include "le_gltf_loader.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "fx/gltf.h"

#include <iostream>
#include <iomanip>
#include <map>

#include "le_renderer/le_renderer.h"
#include "le_pipeline_builder/le_pipeline_builder.h"

//#include "le_backend_vk/le_backend_vk.h" // for get_pipeline_cache (FIXME: get rid of this)

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#define GLM_ENABLE_EXPERIMENTAL
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtx/quaternion.hpp"
#include "glm/gtx/matrix_decompose.hpp"

struct GltfUboMvp {
	glm::mat4 projection;
	glm::mat4 model;
	glm::mat4 view;
};

using namespace fx;

// ----------------------------------------------------------------------

struct Node {
	enum Flags : uint16_t {
		eHasCamera = 0x1 << 0,
		eHasMesh   = 0x1 << 1,
	};
	uint16_t  numChildren;       //
	uint16_t  flags;             //
	uint32_t  meshOrCameraIndex; // may also be zero for a pure node
	glm::vec3 localTranslation;
	glm::vec3 localScale;
	glm::quat localRotation;
	glm::mat4 globalTransform;
};

struct Primitive {

	std::vector<le_vertex_input_attribute_description> attributeDescriptions;
	std::vector<le_vertex_input_binding_description>   bindingDescriptions;

	std::vector<size_t> attributeDataOffs; // offset into main buffer per attriute, sorted by location, which ensures at the same time sorting by binding number, as both are linked
	uint32_t            indexDataOffs = 0; // (optional) offset info main buffer to get to index data

	uint32_t numElements = 0;     //either number of indices (if hasIndices) or number of vertices to draw
	int32_t  material    = -1;    // material index based on doc, -1 means default material
	uint8_t  mode        = 0;     // triangles,lines,points TODO: use a mode that makes sense
	bool     hasIndices  = false; // wether to render using indices or

	le_gpso_handle pso{};
};

struct Mesh {
	std::vector<uint32_t> primitives;
};

struct le_gltf_document_o {

	std::vector<uint8_t> data; // raw geometry data

	std::vector<le_resource_handle_t> bufferResources;
	std::vector<le_resource_info_t>   bufferResourceInfos;

	std::vector<le_resource_handle_t> imageResources;
	std::vector<le_resource_info_t>   imageResourceInfos;

	std::vector<Primitive> primitives;
	std::vector<Mesh>      meshes;

	std::vector<Node> nodeGraph;

	uint64_t pso{}; // one pso for all elements for now.

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

enum class AttributeType : uint8_t {
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
/// \brief sets attribute location, isNormalised and type
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
		attr.type = le_vertex_input_attribute_description::Type::eChar;
	    break;
	case ( gltf::Accessor::ComponentType::UnsignedByte ):
		attr.type = le_vertex_input_attribute_description::Type::eUChar;
	    break;
	case ( gltf::Accessor::ComponentType::Short ):
		attr.type = le_vertex_input_attribute_description::Type::eShort;
	    break;
	case ( gltf::Accessor::ComponentType::UnsignedShort ):
		attr.type = le_vertex_input_attribute_description::Type::eUShort;
	    break;
	case ( gltf::Accessor::ComponentType::UnsignedInt ):
		attr.type = le_vertex_input_attribute_description::Type::eUInt;
	    break;
	case ( gltf::Accessor::ComponentType::Float ):
		attr.type         = le_vertex_input_attribute_description::Type::eFloat;
		attr.isNormalised = false; // Floats are never normalised
	    break;
	}
};

static inline uint32_t get_num_bytes_per_element( const gltf::Accessor::Type &t, const gltf::Accessor::ComponentType &cT ) {

	uint32_t componentSz = 0;
	uint32_t vecSz       = vec_size_from_gltf_type( t );

	switch ( cT ) {
	case ( gltf::Accessor::ComponentType::None ):
		componentSz = 0;
	    break;
	case ( gltf::Accessor::ComponentType::Byte ):
	case ( gltf::Accessor::ComponentType::UnsignedByte ):
		componentSz = 1;
	    break;
	case ( gltf::Accessor::ComponentType::Short ):
	case ( gltf::Accessor::ComponentType::UnsignedShort ):
		componentSz = 2;
	    break;
	case ( gltf::Accessor::ComponentType::UnsignedInt ):
	case ( gltf::Accessor::ComponentType::Float ):
		componentSz = 4;
	    break;
	}

	return vecSz * componentSz;
}

// Unify gltf document structure so that all attribute data is non-interleaved.
// This also enforces a strict 1:1 relationship between bufferViews and Accessors,
// and also enforces a single data buffer per document.
static gltf::Document gltf_document_unify_structure( const gltf::Document &docInput ) {

	gltf::Document docOutput = docInput; // clone document

	struct DeepAccessor {
		uint8_t const *               src = nullptr; // source address; buffer.data() + bufferView.byteOffset+accessor.byteOffset
		size_t                        byteStride;
		gltf::Accessor::ComponentType componentType;
		gltf::Accessor::Type          type;
		uint8_t                       normalized;
		uint32_t                      numElements;
		uint64_t                      numBytesPerElement;
		std::vector<float>            min;
		std::vector<float>            max;
		std::string                   name;
	};

	// we want to copy all attribute data into bufferviews so that each attribute has its own bufferview.
	// any vertex data which is stored interleaved is de-interleaved.

	// bufferviews with byteStride are a red flag that data is interleaved.
	// accessors with byteOffset are a red flag that data is interleaved.

	// steps
	// 1) we "deepen the accessor" - place bufferview, and buffer info info into accessor
	// 2) we "render the accessor" - write out data for all accessors back to buffer, and create a bufferview for each accessor

	std::vector<DeepAccessor> deepAccessors;
	deepAccessors.reserve( docInput.accessors.size() );

	for ( const auto &a : docInput.accessors ) {
		DeepAccessor da{};

		const auto &bufferView = docInput.bufferViews[ size_t( a.bufferView ) ];
		const auto &buffer     = docInput.buffers[ size_t( bufferView.buffer ) ];

		da.src                = buffer.data.data() + bufferView.byteOffset + a.byteOffset;
		da.byteStride         = bufferView.byteStride;
		da.componentType      = a.componentType;
		da.type               = a.type;
		da.normalized         = a.normalized;
		da.numElements        = a.count;
		da.numBytesPerElement = get_num_bytes_per_element( a.type, a.componentType ); // gets e.g. the number of bytes needed to represent a vec4f == 4*4 == 16
		da.min                = a.min;
		da.max                = a.max;
		da.name               = a.name;

		deepAccessors.emplace_back( da );
	}

	// now we must re-create bufferviews and buffer based on
	// the data which we have collected in our deep accessors

	std::vector<uint8_t> exportBuffer;

	{ // get sum size of data in all buffer from the input document.
		size_t totalSize = 0;
		for ( auto const &b : docInput.buffers ) {
			totalSize += b.byteLength;
		}
		exportBuffer.resize( totalSize );
	}

	uint8_t *      buffer_end   = exportBuffer.data(); // Initialise our write stream iterator
	uint8_t *const buffer_start = exportBuffer.data(); // We keep this at start pos so we can calculate offsets

	// For each accessor, we create a corresponding bufferview, and then we
	// serialize the data referenced by the accessor by copying it back
	// into exportBuffer

	std::vector<gltf::BufferView> bufferViews;
	bufferViews.reserve( deepAccessors.size() );

	for ( auto const &da : deepAccessors ) {
		gltf::BufferView bufferView{};

		bufferView.buffer     = 0;                                                  // Must be buffer 0, all views use same buffer in the export document.
		bufferView.byteOffset = uint32_t( buffer_end - buffer_start );              // store current buffer offset as bufferView byteOffset
		bufferView.byteStride = 0;                                                  // Must be zero, zero means tightly packed, not interleaved.
		bufferView.byteLength = uint32_t( da.numBytesPerElement * da.numElements ); // byte length is always that of a tightly packed buffer

		// -- Copy data into buffer

		if ( da.byteStride == 0 || da.byteStride == da.numBytesPerElement ) {
			// -- Data is not interleaved to begin with - copy all in one go.

			size_t byteLength = da.numBytesPerElement * da.numElements;

			memcpy( buffer_end, da.src, byteLength );

			// TODO: Check if we must insure that byteLength is multiple of two
			// if so, here would be the correct place to fix this.

			buffer_end += byteLength; // update source data pointer.

		} else {
			// -- Data is interleaved - we first have to de-interleave

			// De-interleave data by "pasting together" interleaved chunks
			auto src_data = da.src;
			for ( size_t i = 0; i != da.numElements; i++ ) {

				const size_t copyBytesCount = da.numBytesPerElement;
				memcpy( buffer_end, src_data, copyBytesCount );

				src_data += da.byteStride;
				buffer_end += da.numBytesPerElement;
			}
		}

		assert( buffer_end <= exportBuffer.data() + exportBuffer.size() ); // We must not write past the end of the buffer

		bufferViews.emplace_back( bufferView );
	}

	// ------------| invariant: there is now a 1:1 relationship between bufferviews and accessors

	// Let's re-assemble the document.
	// we only have to update buffer, bufferviews, and accessors. everything else should stay the same after remapping,
	// as the accessor indices have not changed, and any data acceess must happen via accessors.

	{
		docOutput.buffers.clear(); // FIXME, this is not optimal, as it means to first copy, then delete the buffer anyway.

		std::string bufferUri = docInput.buffers.front().uri;
		{
			// Remove ".bin" extension from filename uri, if found
			auto extPos = bufferUri.find_last_of( ".bin" );
			if ( extPos != std::string::npos ) {
				bufferUri = bufferUri.substr( 0, extPos - 3 );
			}
		}

		gltf::Buffer docBuffer;
		docBuffer.name = bufferUri.empty() ? "buffer-0" : bufferUri;
		docBuffer.uri  = docBuffer.name + "-unified.bin";

		docBuffer.byteLength = uint32_t( exportBuffer.size() );
		docBuffer.data       = std::move( exportBuffer );

		docOutput.buffers.emplace_back( std::move( docBuffer ) );

		std::vector<gltf::Accessor> accessors;
		accessors.reserve( deepAccessors.size() );

		int32_t bufferViewIndex = 0;
		for ( auto const &da : deepAccessors ) {
			gltf::Accessor a{};
			a.bufferView    = bufferViewIndex;
			a.byteOffset    = 0; // must be 0
			a.componentType = da.componentType;
			a.type          = da.type;
			a.count         = da.numElements;
			a.normalized    = da.normalized;
			a.min           = da.min;
			a.max           = da.max;
			a.name          = da.name;
			accessors.emplace_back( a );
			bufferViewIndex++;
		}

		// Copy over all accessors
		docOutput.accessors = std::move( accessors );

		// Copy over all bufferviews
		docOutput.bufferViews = std::move( bufferViews );
	}

	return docOutput;
}

// ----------------------------------------------------------------------
/// \brief flattens a node hierarchy into a topologically sorted array
/// \details each parent is placed before all its decendents, and each
/// parent's numChildren value means the total number of children and children's children
size_t node_graph_append_children_from_gltf_nodes( std::vector<Node> &nodegraph, std::vector<gltf::Node> const &gltf_nodes, size_t index ) {

	const auto &gltfNode = gltf_nodes[ index ];

	nodegraph.push_back( {} );
	auto &node = nodegraph.back();

	const size_t numDirectChildren = gltfNode.children.size();
	size_t       totalChildren     = numDirectChildren;

	for ( size_t i = 0; i != numDirectChildren; ++i ) {
		totalChildren += node_graph_append_children_from_gltf_nodes( nodegraph, gltf_nodes, size_t( gltfNode.children[ i ] ) );
	}

	node.numChildren     = uint16_t( totalChildren );
	node.globalTransform = glm::mat4( 1 );

	{
		// Calculate local translation, rotation, and scale.
		// we don't know whether matrix or TRS have been set.
		// which is why we must first build a matrix representing it all.
		// we assume that our importer will have set unused elements to identity.
		glm::mat4x4 localTransform = reinterpret_cast<glm::mat4 const &>( gltfNode.matrix ); // TODO: apply TRS if available

		localTransform =
		    glm::translate( glm::mat4{1}, reinterpret_cast<const glm::vec3 &>( gltfNode.translation ) ) *
		    glm::toMat4( reinterpret_cast<const glm::quat &>( gltfNode.rotation ) ) *
		    glm::scale( glm::mat4{1}, reinterpret_cast<const glm::vec3 &>( gltfNode.scale ) ) * localTransform;

		glm::vec3 skew{0};
		glm::vec4 perspective{0};
		glm::decompose( localTransform, node.localScale, node.localRotation, node.localTranslation, skew, perspective );

		// TEST: localTransformNew must be the same again as localTransform
		glm::mat4 localTransformNew =
		    glm::translate( glm::mat4{1}, reinterpret_cast<const glm::vec3 &>( node.localTranslation ) ) *
		    glm::toMat4( reinterpret_cast<const glm::quat &>( node.localRotation ) ) *
		    glm::scale( glm::mat4{1}, reinterpret_cast<const glm::vec3 &>( node.localScale ) );

		float const *a        = &localTransform[ 0 ][ 0 ];
		float const *b        = &localTransformNew[ 0 ][ 0 ];
		float *const endRange = &localTransform[ 3 ][ 3 ];
		for ( ; a != endRange; a++, b++ ) {
			float difference = std::fabs( *a - *b );
			if ( difference > std::numeric_limits<float>::epsilon() ) {
				std::cout << "Warning: Rounding error when importing nodes : " << difference << std::endl
				          << std::flush;
			}
		}
	}

	if ( gltfNode.mesh != -1 ) {
		node.flags |= Node::Flags::eHasMesh;
		node.meshOrCameraIndex = uint32_t( gltfNode.mesh );
	} else if ( gltfNode.camera != -1 ) {
		node.flags |= Node::Flags::eHasCamera;
		node.meshOrCameraIndex = uint32_t( gltfNode.camera );
	}

	return totalChildren;
};

// ----------------------------------------------------------------------
void updateNodeGraph( std::vector<Node> &nodes ) {

	// We don't want to do this recursively.
	// We want to do this linearly. Therefore, we iterate over nodes held in a vector.

	/* This Node structure:
	 *
	 * A
	 *  B   C
	 *   DE
	 *     F
	 *
	 * Results in:
	 *
	 *  Global Transform , (num Children)
	 *  A                , (5) = A
	 *	A                , (3)
	 *	A                , (0)
	 *	A                , (1)
	 *	A                , (0)
	 *	A                , (0)
	 *	A*B              , (3) = B
	 *	A*B              , (0)
	 *	A*B              , (1)
	 *	A*B              , (0)
	 *	A*B*D            , (0) = D
	 *	A*B*E            , (1) = E
	 *	A*B*E            , (0)
	 *	A*B*E*F          , (0) = F
	 *	A*C              , (0) = C
	 */

	Node *          parent            = nodes.data();
	size_t          numChildren       = parent->numChildren;
	Node *const     endNodeRange      = parent + nodes.size();
	const glm::mat4 identityTransform = glm::mat4( 1 );
	size_t          numOps            = 0;

	glm::mat4 parentMatrix =
	    glm::translate( glm::mat4{1}, reinterpret_cast<const glm::vec3 &>( parent->localTranslation ) ) *
	    glm::toMat4( reinterpret_cast<const glm::quat &>( parent->localRotation ) ) *
	    glm::scale( glm::mat4{1}, reinterpret_cast<const glm::vec3 &>( parent->localScale ) );

	// initialise all global transforms to identity
	for ( Node *child = parent; child != endNodeRange; child++ ) {
		child->globalTransform = identityTransform;
	}

	for ( Node *child = parent; child != endNodeRange; numOps++ ) {

		// Apply current matrix to all children.
		//
		// If this is the first time we're iterating over children,
		// global Transforms are first reset to parent Transform, as
		// child->globalMatrix would then be the identity.
		//
		child->globalTransform = child->globalTransform * parentMatrix; // pre-multiply

		//		std::cout << glm::to_string( child->globalTransform ) << ", (" << child->numChildren << ")" << std::endl
		//		          << std::flush;

		if ( child == parent + numChildren ) {

			parent++;
			child = parent;

			if ( parent != endNodeRange ) {
				numChildren = parent->numChildren;
				// calculate local transform matrix for parent.
				parentMatrix =
				    glm::translate( glm::mat4{1}, reinterpret_cast<const glm::vec3 &>( parent->localTranslation ) ) *
				    glm::toMat4( reinterpret_cast<const glm::quat &>( parent->localRotation ) ) *
				    glm::scale( glm::mat4{1}, reinterpret_cast<const glm::vec3 &>( parent->localScale ) );
			}

		} else {
			child++;
		}
	}
	//	std::cout << "TRANSFORMED NODE GRAPH IN " << std::dec << numOps << " OPS" << std::endl
	//	          << std::flush;
}

// ----------------------------------------------------------------------

static bool document_load_from_text( le_gltf_document_o *self, const char *path ) {
	bool           result = true;
	gltf::Document importDoc;

	try {
		importDoc = gltf::LoadFromText( path );
	} catch ( std::runtime_error e ) {
		std::cerr << __FILE__ " [ ERROR ] Could not load file: '" << path << "'"
		          << ", received error: " << e.what() << std::endl
		          << std::flush;
		return false;
	}

	// ---------| invariant: file was loaded successfully

	/*
	 * Ingest geometry:
	 *
	 * We want geometry data to be of uniform structure.
	 * We dont want vertex data to be interleaved, because this makes it less performant when rendering sub-passes (e.g. z-prepass, where we only need positions)
	 *
	 * Mesh
	 * \-- indices[]
	 * \-- positions[]
	 * \-- normals[]
	 * \-- tangents[]
	 *
	 * This means we must rewrite the data so that each attribute of the mesh has its own
	 * bufferview, and that each bufferview has a stride of 0 (tightly packed)
	 *
	 *   */

	importDoc = gltf_document_unify_structure( importDoc );

	size_t geometryDataSize = 0;
	{
		for ( const auto &b : importDoc.buffers ) {
			geometryDataSize += b.byteLength;
		}
	}

	{
		le_resource_handle_t bufferResource = LE_RESOURCE( "gltf-buffer-1", LeResourceType::eBuffer );
		self->bufferResources.emplace_back( bufferResource );
		le_resource_info_t resourceInfo;
		resourceInfo.type         = LeResourceType::eBuffer;
		resourceInfo.buffer.size  = uint32_t( geometryDataSize );
		resourceInfo.buffer.usage = LE_BUFFER_USAGE_INDEX_BUFFER_BIT |
		                            LE_BUFFER_USAGE_VERTEX_BUFFER_BIT |
		                            LE_BUFFER_USAGE_TRANSFER_DST_BIT;
		self->bufferResourceInfos.emplace_back( resourceInfo );
	}

	{ // traverse document and store vertex data in a format best suited for rendering.

		auto const &doc            = importDoc;
		auto &      accessors      = doc.accessors;
		auto &      docBufferViews = doc.bufferViews;

		// Steal data buffer from gltf document
		std::swap( self->data, importDoc.buffers.front().data );

		self->meshes.reserve( doc.meshes.size() );

		for ( const auto &m : doc.meshes ) {

			Mesh msh;

			for ( const auto &p : m.primitives ) {

				Primitive prim{};

				if ( p.indices != -1 ) {
					//mesh has indices
					auto const &acc        = accessors[ size_t( p.indices ) ];
					auto const &bufferView = docBufferViews[ size_t( acc.bufferView ) ];
					prim.indexDataOffs     = bufferView.byteOffset;
					prim.numElements       = acc.count;
					prim.hasIndices        = true;
				}

				{
					struct AttributeInfo {
						le_vertex_input_attribute_description attr;
						uint32_t                              bufferViewOffs;
					};

					std::vector<AttributeInfo> tmpAttrInfos;
					tmpAttrInfos.reserve( p.attributes.size() );

					for ( auto &a : p.attributes ) {
						AttributeInfo attrInfo;
						auto const &  acc = accessors[ a.second ];

						getAttrInfo( a.first, acc, attrInfo.attr );            // fills in location, isNormalized and type
						attrInfo.attr.binding        = attrInfo.attr.location; // binding shall be identical with location - 1:1 relationship since we're de-interleaved.
						attrInfo.attr.binding_offset = 0;                      // offset must be 0, as we're not interleaved.
						attrInfo.attr.vecsize        = vec_size_from_gltf_type( acc.type );
						auto const &bufferView       = docBufferViews[ size_t( acc.bufferView ) ];
						attrInfo.bufferViewOffs      = bufferView.byteOffset; // buffer is always 0, offset is offset of the bufferView

						if ( prim.numElements == 0 ) {
							prim.numElements = acc.count; // numElements was not set via index count, this means non-indexed draw, numElements must be vertexCount
						} else if ( !prim.hasIndices ) {
							assert( prim.numElements == acc.count ); // count must be identical over all attributes, if we have not got indices!
						}

						tmpAttrInfos.push_back( attrInfo );
						// first find out what kind of attribute that is
					}

					// now sort AttributeInfos by location

					std::sort( tmpAttrInfos.begin(), tmpAttrInfos.end(),
					           []( const AttributeInfo &lhs, const AttributeInfo &rhs ) { return lhs.attr.location < rhs.attr.location; } );

					// now make sure that bindings are not sparse (locations may well be...)
					// TODO: look up whether bindings *must* be contiguous or may be sparse.
					const size_t numTmpAttributes = tmpAttrInfos.size();
					for ( size_t i = 0; i != numTmpAttributes; ++i ) {
						assert( i < 32 ); // you can't have more than 32 bindings!
						tmpAttrInfos[ i ].attr.binding = uint8_t( i );
					}

					// Store data in Primitive, where it is saved as structure-of-arrays
					prim.attributeDescriptions.reserve( numTmpAttributes );
					prim.attributeDataOffs.reserve( numTmpAttributes );
					prim.bindingDescriptions.reserve( numTmpAttributes ); // One binding description per attribute descrition because de-interleaved.

					for ( const auto &attrInfo : tmpAttrInfos ) {
						le_vertex_input_binding_description bindingDescr;
						bindingDescr.binding = attrInfo.attr.binding;
						bindingDescr.stride  = attrInfo.attr.vecsize * ( 1 << ( attrInfo.attr.type & 0x03 ) ); // FIXME: stride cannot be 0, it must be the size in bytes of this attribute!
						prim.bindingDescriptions.emplace_back( bindingDescr );
						prim.attributeDescriptions.push_back( attrInfo.attr );
						prim.attributeDataOffs.push_back( ( attrInfo.bufferViewOffs ) );
					}
				}

				prim.mode     = enum_to_num( p.mode );
				prim.material = p.material;
				msh.primitives.push_back( uint32_t( self->primitives.size() ) );
				self->primitives.push_back( prim );
			} // end for all primitives
			self->meshes.push_back( msh );
		}

		{ // translate the node hierarchy

			std::vector<gltf::Node> const &gltfNodes = importDoc.nodes;

			self->nodeGraph.reserve( gltfNodes.size() );

			auto rootScene = importDoc.scene != -1 ? importDoc.scenes[ size_t( importDoc.scene ) ] : importDoc.scenes.front();

			for ( auto &rootNode : rootScene.nodes ) {
				// we append all nodes connected to each root node to our scene graph.
				// this will first (recursively) add all nodes attached to the first root node,
				// then add (recursively) all nodes attached to the next root node, etc.
				node_graph_append_children_from_gltf_nodes( self->nodeGraph, gltfNodes, rootNode );
			}

			std::cout << "imported " << self->nodeGraph.size() << "nodes." << std::endl
			          << std::flush;

			updateNodeGraph( self->nodeGraph );
		}
	}

	return result;
}

// ----------------------------------------------------------------------

static void document_setup_resources( le_gltf_document_o *self, le_renderer_o *renderer, le_pipeline_manager_o *pipeline_manager ) {
	static const auto &renderer_i = Registry::getApi<le_renderer_api>()->le_renderer_i;

	using namespace le_renderer;

	for ( auto &p : self->primitives ) {

		// Cache buffer lookups for primitives

		{
			auto shader_module_vert = renderer_i.create_shader_module( renderer, "./resources/shaders/pbr.vert", {le::ShaderStage::eVertex} );
			auto shader_module_frag = renderer_i.create_shader_module( renderer, "./resources/shaders/pbr.frag", {le::ShaderStage::eFragment} );

			p.pso = LeGraphicsPipelineBuilder( pipeline_manager )
			            .addShaderStage( shader_module_frag )
			            .addShaderStage( shader_module_vert )
			            .withRasterizationState()
			            .setCullMode( le::CullModeFlagBits::eBack )
			            .setFrontFace( le::FrontFace::eCounterClockwise )
			            .end()
			            .withAttachmentBlendState()
			            .usePreset( le::AttachmentBlendPreset::ePremultipliedAlpha )
			            .end()
			            .setVertexInputAttributeDescriptions( p.attributeDescriptions.data(), p.attributeDescriptions.size() )
			            .setVertexInputBindingDescriptions( p.bindingDescriptions.data(), p.bindingDescriptions.size() )
			            .build();
		}

	} // end for:primitives
}

// ----------------------------------------------------------------------
// You have to get these resource infos from a transfer renderpass,
// to make resources accessible to the rendergraph
static void document_get_resource_infos( le_gltf_document_o *self, le_resource_info_t **infos, le_resource_handle_t const **handles, size_t *numResources ) {
	*infos        = self->bufferResourceInfos.data();
	*handles      = self->bufferResources.data();
	*numResources = self->bufferResources.size(); //FIXME: return 1 once this is fixed.
}

// ----------------------------------------------------------------------

static void document_upload_resource_data( le_gltf_document_o *self, le_command_buffer_encoder_o *encoder ) {

	if ( self->isDirty == false ) {
		return;
	}

	// ---------| invariant: data needs to be uploaded to GPU

	static const auto &encoder_i = Registry::getApi<le_renderer_api>()->le_command_buffer_encoder_i;
	if ( self->bufferResources.size() == 1 ) {
		encoder_i.write_to_buffer( encoder, self->bufferResources[ 0 ], 0, self->data.data(), self->data.size() );
	}

	self->isDirty = false;
}

// ----------------------------------------------------------------------

static void document_draw( le_gltf_document_o *self, le_command_buffer_encoder_o *encoder, GltfUboMvp const *mvp ) {

	static const auto &encoder_i = Registry::getApi<le_renderer_api>()->le_command_buffer_encoder_i;

	updateNodeGraph( self->nodeGraph );

	const auto &nodeGraph = self->nodeGraph;

	// What a bloody mess! we should be able to sort nodes by material,
	// that way we can make sure to minimize binding changes.
	// And all that crap is running on the front thread. Aaaargh.

	auto &                               documentBufferHandle = self->bufferResources[ 0 ];
	std::array<le_resource_handle_t, 32> bufferHandles;
	bufferHandles.fill( documentBufferHandle );

	encoder_i.bind_graphics_pipeline( encoder, self->primitives[ 0 ].pso );

	struct GltfUboNode {
		glm::mat4 matrix;
	} uboNode;

	for ( auto &n : nodeGraph ) {

		if ( n.flags & Node::Flags::eHasMesh ) {

			uboNode.matrix = n.globalTransform;
			encoder_i.set_argument_data( encoder, hash_64_fnv1a_const( "UBO" ), mvp, sizeof( GltfUboMvp ) );
			encoder_i.set_argument_data( encoder, hash_64_fnv1a_const( "UBONode" ), &uboNode, sizeof( GltfUboNode ) );

			// this node has a mesh, let's draw it.

			auto &primitives = self->meshes[ n.meshOrCameraIndex ].primitives;

			for ( auto &pIdx : primitives ) {
				auto &p = self->primitives[ pIdx ];

				encoder_i.bind_vertex_buffers( encoder, 0, p.attributeDataOffs.size(), bufferHandles.data(), p.attributeDataOffs.data() );

				if ( p.hasIndices ) {
					// bind indices
					encoder_i.bind_index_buffer( encoder, documentBufferHandle, p.indexDataOffs, le::IndexType::eUint16 ); // TODO: check if indextype really is 0==uint16_t
					encoder_i.draw_indexed( encoder, p.numElements, 1, 0, 0, 0 );
				} else {
					encoder_i.draw( encoder, p.numElements, 1, 0, 0 );
				}
			}
		}
	}
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_gltf_loader_api( void *api ) {
	auto &document_i = static_cast<le_gltf_loader_api *>( api )->document_i;

	document_i.create               = document_create;
	document_i.destroy              = document_destroy;
	document_i.load_from_text       = document_load_from_text;
	document_i.setup_resources      = document_setup_resources;
	document_i.get_resource_infos   = document_get_resource_infos;
	document_i.upload_resource_data = document_upload_resource_data;
	document_i.draw                 = document_draw;
}
