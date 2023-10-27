#include "le_backend_vk.h"
#include "le_backend_types_internal.h"

#include <cassert>
#include <string>
#include <set>
#include <unordered_map>

#include <filesystem> // for parsing shader source file paths
#include <fstream>    // for reading shader source files
#include <cstring>    // for memcpy
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <algorithm>

#include "le_core.h"
#include "le_shader_compiler.h"

#include "util/spirv_reflect/spirv_reflect.h"

#include "le_file_watcher.h" // for watching shader source files
#include "le_log.h"
#include "3rdparty/src/spooky/SpookyV2.h" // for hashing renderpass gestalt, so that we can test for *compatible* renderpasses

static constexpr auto LOGGER_LABEL = "le_pipeline";

#include <vulkan/vulkan.h>
#include "private/le_backend_vk/le_backend_types_pipeline.inl"

#include "le_tracy.h"

typedef void ( *file_watcher_callback_fun_t )( char const*, void* );

struct specialization_map_info_t {
	std::vector<VkSpecializationMapEntry> entries;
	std::vector<char>                     data;
};

static constexpr auto TEXTURE_NAME_YCBCR_REQUEST_STRING = "__ycbcr__"; // add this string to a shader texture name to signal that we require an immutable YcBcR conversion sampler for this binding

struct le_shader_module_o {
	uint64_t                                       hash                = 0;     ///< hash taken from spirv code + hash_shader_defines
	uint64_t                                       hash_shader_defines = 0;     ///< hash taken from shader defines string
	uint64_t                                       hash_pipelinelayout = 0;     ///< hash taken from descriptors over all sets
	std::string                                    macro_defines       = "";    ///< #defines to pass to shader compiler
	std::vector<le_shader_binding_info>            bindings;                    ///< info for each binding, sorted asc.
	std::vector<uint32_t>                          spirv    = {};               ///< spirv source code for this module
	std::filesystem::path                          filepath = {};               ///< path to source file
	std::vector<std::string>                       vertexAttributeNames;        ///< (used for debug only) name for vertex attribute
	std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions; ///< descriptions gathered from reflection if shader type is vertex
	std::vector<VkVertexInputBindingDescription>   vertexBindingDescriptions;   ///< descriptions gathered from reflection if shader type is vertex
	VkShaderModule                                 module                    = nullptr;
	le::ShaderStageFlagBits                        stage                     = {};
	uint64_t                                       push_constant_buffer_size = 0; ///< number of bytes for push constant buffer, zero indicates no push constant buffer in use.
	le::ShaderSourceLanguage                       source_language           = le::ShaderSourceLanguage::eDefault;
	specialization_map_info_t                      specialization_map_info;       ///< information concerning specialization constants for this shader stage

	enum class ImmutableSamplerRequestedValue : uint64_t                          ///<  sentinel values used to signal that an immutable binding needs to be filled wit a special sampler
	{
		eNone  = 0,
		eYcBcR = hash_64_fnv1a_const( TEXTURE_NAME_YCBCR_REQUEST_STRING ),
	};
};

// A table from `handle` -> `object*`, protected by mutex.
//
// Access is internally synchronised.
//
// lookup time will deteriorate linearly with number of elements,
// but cache locality is very good for lookups, so this should work
// fairly well for small number of resources such as pipelines.
template <typename T, typename U>
class HashTable : NoCopy, NoMove {

	std::shared_mutex mtx;
	std::vector<T>    handles;
	std::vector<U*>   objects; // owning, object is copied on add_entry

  public:
	// Insert a new obj into table, object is copied.
	// return true if successful, false if entry aready existed.
	// in case return value is false, object was not copied.
	bool try_insert( T const& handle, U* obj ) {
		mtx.lock();
		size_t i = 0;
		for ( auto const& h : handles ) {
			if ( h == handle ) {
				break;
			}
			i++;
		}
		if ( i != handles.size() ) {
			// entry already existed - this is strange.
			mtx.unlock();
			return false;
		}
		// -------| invariant: i == handles.size()
		handles.push_back( handle );
		objects.emplace_back( new U( *obj ) ); // make a copy
		mtx.unlock();
		return true;
	}

	// Looks up table entry under `needle`,
	// returns nullptr if not found.
	U* const try_find( T const& needle ) {
		mtx.lock_shared();
		U* const* obj = objects.data();
		for ( auto const& h : handles ) {
			if ( h == needle ) {
				mtx.unlock();
				return *obj;
			}
			obj++;
		}
		// --------| Invariant: no handle matching needle found
		mtx.unlock();
		return nullptr;
	}

	typedef void ( *iterator_fun )( U* e, void* user_data );

	// do something on all objects
	void iterator( iterator_fun fun, void* user_data ) {
		mtx.lock();
		for ( auto& e : objects ) {
			fun( e, user_data );
		}
		mtx.unlock();
	}

	void clear() {
		mtx.lock();
		for ( auto& obj : objects ) {
			delete obj;
		}
		handles.clear();
		objects.clear();
		mtx.unlock();
	}

	~HashTable() {
		clear();
	}
};

//
template <typename S, typename T>
class HashMap : NoCopy, NoMove {

	std::shared_mutex         mtx;
	std::unordered_map<S, T*> store; // owning, object is copied on successful try_insert

  public:
	T* try_find( S needle ) {
		mtx.lock_shared();
		auto e = store.find( needle );
		if ( e == store.end() ) {
			mtx.unlock();
			return nullptr;
		} else {
			auto ret = e->second;
			mtx.unlock();
			return ret;
		}
	}
	// returns true and stores copy of obj in internal hash - or
	// returns false if element with key already existed.
	bool try_insert( S handle, T* obj ) {

		mtx.lock();

		// we attempt an insertion - a nullptr will go in as a placeholder
		// because we cannot tell at this point whether the insertion will
		// be successful.
		auto result = store.emplace( handle, nullptr );

		if ( result.second ) {
			// Insertion was successful - replace the placeholder with a copy of the actual object.
			result.first->second = new T( *obj );
		}

		mtx.unlock();

		return result.second;
	}

	typedef void ( *iterator_fun )( T* e, void* user_data );

	// do something on all objects
	void iterator( iterator_fun fun, void* user_data ) {
		mtx.lock();
		for ( auto& e : store ) {
			fun( e.second, user_data );
		}
		mtx.unlock();
	}

	void clear() {
		mtx.lock();
		for ( auto& e : store ) {
			delete ( e.second );
		}
		store.clear();
		mtx.unlock();
	}
	~HashMap() {
		clear();
	}
};

struct ProtectedModuleDependencies {
	std::mutex                                                         mtx;
	std::unordered_map<std::string, std::set<le_shader_module_handle>> moduleDependencies; // map 'canonical shader source file path, watch_id' -> [shader modules]
	std::unordered_map<std::string, int>                               moduleWatchIds;
	std::unordered_map<std::string, file_watcher_callback_fun_t>       moduleWatchCallbackAddrs; // we store this so that we can release the callback forwarder when resetting the watcher.
};

struct le_shader_manager_o {
	VkDevice device = nullptr;

	HashMap<le_shader_module_handle, le_shader_module_o> shaderModules; // OWNING. Stores all shader modules used in backend, indexed via shader_module_handle

	ProtectedModuleDependencies protected_module_dependencies; // must lock mutex before using.

	std::set<le_shader_module_handle> modifiedShaderModules; // non-owning pointers to shader modules which need recompiling (used by file watcher)

	le_shader_compiler_o* shader_compiler   = nullptr; // owning
	le_file_watcher_o*    shaderFileWatcher = nullptr; // owning
};

// NOTE: It might make sense to have one pipeline manager per worker thread, and
//       to consolidate after the frame has been processed.
struct le_pipeline_manager_o {
	le_backend_o* backend   = nullptr; // weak, non-owning
	le_device_o*  le_device = nullptr; // arc-owning, increases reference count, decreases on destruction
	VkDevice      device    = nullptr;

	std::mutex mtx;

	VkPipelineCache vulkanCache = nullptr;

	le_shader_manager_o* shaderManager = nullptr; // owning: does it make sense to have a shader manager additionally to the pipeline manager?

	HashTable<le_gpso_handle, graphics_pipeline_state_o> graphicsPso;
	HashTable<le_cpso_handle, compute_pipeline_state_o>  computePso;
	HashTable<le_rtxpso_handle, rtx_pipeline_state_o>    rtxPso;

	HashMap<uint64_t, VkPipeline>              pipelines;             // indexed by pipeline_hash
	HashTable<uint64_t, char*>                 rtx_shader_group_data; // indexed by pipeline_hash
	HashMap<uint64_t, le_pipeline_layout_info> pipelineLayoutInfos;

	HashMap<uint64_t, le_descriptor_set_layout_t> descriptorSetLayouts;
	HashMap<uint64_t, VkPipelineLayout>           pipelineLayouts; // indexed by hash of array of descriptorSetLayoutCache keys per pipeline layout
};

static VkFormat vk_format_from_spv_reflect_format( SpvReflectFormat const& format ) {
	// clang-format off
	switch (format)
	{
		case SPV_REFLECT_FORMAT_UNDEFINED           : return  VK_FORMAT_UNDEFINED;
		case SPV_REFLECT_FORMAT_R32_UINT            : return  VK_FORMAT_R32_UINT;
		case SPV_REFLECT_FORMAT_R32_SINT            : return  VK_FORMAT_R32_SINT;
		case SPV_REFLECT_FORMAT_R32_SFLOAT          : return  VK_FORMAT_R32_SFLOAT;
		case SPV_REFLECT_FORMAT_R32G32_UINT         : return  VK_FORMAT_R32G32_UINT;
		case SPV_REFLECT_FORMAT_R32G32_SINT         : return  VK_FORMAT_R32G32_SINT;
		case SPV_REFLECT_FORMAT_R32G32_SFLOAT       : return  VK_FORMAT_R32G32_SFLOAT;
		case SPV_REFLECT_FORMAT_R32G32B32_UINT      : return  VK_FORMAT_R32G32B32_UINT;
		case SPV_REFLECT_FORMAT_R32G32B32_SINT      : return  VK_FORMAT_R32G32B32_SINT;
		case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT    : return  VK_FORMAT_R32G32B32_SFLOAT;
		case SPV_REFLECT_FORMAT_R32G32B32A32_UINT   : return  VK_FORMAT_R32G32B32A32_UINT;
		case SPV_REFLECT_FORMAT_R32G32B32A32_SINT   : return  VK_FORMAT_R32G32B32A32_SINT;
		case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT : return  VK_FORMAT_R32G32B32A32_SFLOAT;
		case SPV_REFLECT_FORMAT_R64_UINT            : return  VK_FORMAT_R64_UINT;
		case SPV_REFLECT_FORMAT_R64_SINT            : return  VK_FORMAT_R64_SINT;
		case SPV_REFLECT_FORMAT_R64_SFLOAT          : return  VK_FORMAT_R64_SFLOAT;
		case SPV_REFLECT_FORMAT_R64G64_UINT         : return  VK_FORMAT_R64G64_UINT;
		case SPV_REFLECT_FORMAT_R64G64_SINT         : return  VK_FORMAT_R64G64_SINT;
		case SPV_REFLECT_FORMAT_R64G64_SFLOAT       : return  VK_FORMAT_R64G64_SFLOAT;
		case SPV_REFLECT_FORMAT_R64G64B64_UINT      : return  VK_FORMAT_R64G64B64_UINT;
		case SPV_REFLECT_FORMAT_R64G64B64_SINT      : return  VK_FORMAT_R64G64B64_SINT;
		case SPV_REFLECT_FORMAT_R64G64B64_SFLOAT    : return  VK_FORMAT_R64G64B64_SFLOAT;
		case SPV_REFLECT_FORMAT_R64G64B64A64_UINT   : return  VK_FORMAT_R64G64B64A64_UINT;
		case SPV_REFLECT_FORMAT_R64G64B64A64_SINT   : return  VK_FORMAT_R64G64B64A64_SINT;
		case SPV_REFLECT_FORMAT_R64G64B64A64_SFLOAT : return  VK_FORMAT_R64G64B64A64_SFLOAT;
		default	                                    : assert(false); return VkFormat();
	} // clang-format on
}

static uint32_t byte_stride_from_spv_type_description( SpvReflectNumericTraits const& traits ) {

	uint32_t unit_size = traits.scalar.width / 8;

	assert( unit_size != 0 );

	uint32_t result = unit_size;
	result          = std::max<uint32_t>( result, unit_size * traits.vector.component_count );
	result          = std::max<uint32_t>( result, unit_size * traits.matrix.column_count * traits.matrix.row_count );
	result          = std::max<uint32_t>( result, traits.matrix.stride );

	return result;
}

static le::DescriptorType descriptor_type_from_spv_descriptor_type( SpvReflectDescriptorType const& spv_descriptor_type ) {
	// clang-format off
	switch(spv_descriptor_type)
	{
		case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER                    : return le::DescriptorType( VK_DESCRIPTOR_TYPE_SAMPLER);
		case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER     : return le::DescriptorType( VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE              : return le::DescriptorType( VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE              : return le::DescriptorType( VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER       : return le::DescriptorType( VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER       : return le::DescriptorType( VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
		case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER             : // Deliberate fall-through: we make all uniform buffers dynamic.
		case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC     : return le::DescriptorType( VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER             : // Deliberate fall-through: we make storage buffers dynamic.
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC     : return le::DescriptorType( VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
		case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT           : return le::DescriptorType( VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
		case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR : return le::DescriptorType( VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
        default: assert(false); return le::DescriptorType();
	}
	// clang-format on
}

// ----------------------------------------------------------------------

// clang-format off
/// \returns corresponding VkFormat for a given le_input_attribute_description struct
static inline VkFormat vk_format_from_le_vertex_input_attribute_description( le_vertex_input_attribute_description const & d){

	if ( d.vecsize == 0 || d.vecsize > 4 ){
		assert(false); // vecsize must be between 1 and 4
		return VK_FORMAT_UNDEFINED;
	}

	switch ( d.type ) {
	case le_num_type::eFloat:
		switch ( d.vecsize ) {
		case 4: return VK_FORMAT_R32G32B32A32_SFLOAT;
		case 3: return VK_FORMAT_R32G32B32_SFLOAT;
		case 2: return VK_FORMAT_R32G32_SFLOAT;
		case 1: return VK_FORMAT_R32_SFLOAT;
		}
	    break;
	case le_num_type::eHalf:
		switch ( d.vecsize ) {
		case 4: return VK_FORMAT_R16G16B16A16_SFLOAT;
		case 3: return VK_FORMAT_R16G16B16_SFLOAT;
		case 2: return VK_FORMAT_R16G16_SFLOAT;
		case 1: return VK_FORMAT_R16_SFLOAT;
		}
	    break;
	case le_num_type::eUShort: // fall through to eShort
	case le_num_type::eShort:
		if (d.isNormalised){
			switch ( d.vecsize ) {
			case 4: return VK_FORMAT_R16G16B16A16_UNORM;
			case 3: return VK_FORMAT_R16G16B16_UNORM;
			case 2: return VK_FORMAT_R16G16_UNORM;
			case 1: return VK_FORMAT_R16_UNORM;
			}
		}else{
			switch ( d.vecsize ) {
			case 4: return VK_FORMAT_R16G16B16A16_UINT;
			case 3: return VK_FORMAT_R16G16B16_UINT;
			case 2: return VK_FORMAT_R16G16_UINT;
			case 1: return VK_FORMAT_R16_UINT;
			}
		}
	    break;
	case le_num_type::eInt:
		switch ( d.vecsize ) {
		case 4: return VK_FORMAT_R32G32B32A32_SINT;
		case 3: return VK_FORMAT_R32G32B32_SINT;
		case 2: return VK_FORMAT_R32G32_SINT;
		case 1: return VK_FORMAT_R32_SINT;
		}
	    break;
	case le_num_type::eUInt:
		switch ( d.vecsize ) {
		case 4: return VK_FORMAT_R32G32B32A32_UINT;
		case 3: return VK_FORMAT_R32G32B32_UINT;
		case 2: return VK_FORMAT_R32G32_UINT;
		case 1: return VK_FORMAT_R32_UINT;
		}
	    break;
	case le_num_type::eULong:
		switch ( d.vecsize ) {
		case 4: return VK_FORMAT_R64G64B64A64_UINT;
		case 3: return VK_FORMAT_R64G64B64_UINT;
		case 2: return VK_FORMAT_R64G64_UINT;
		case 1: return VK_FORMAT_R64_UINT;
		}
	    break;
	case le_num_type::eChar:  // fall through to uChar
	case le_num_type::eUChar:
		if (d.isNormalised){
			switch ( d.vecsize ) {
			case 4: return VK_FORMAT_R8G8B8A8_UNORM;
			case 3: return VK_FORMAT_R8G8B8_UNORM;
			case 2: return VK_FORMAT_R8G8_UNORM;
			case 1: return VK_FORMAT_R8_UNORM;
			}
		} else {
			switch ( d.vecsize ) {
			case 4: return VK_FORMAT_R8G8B8A8_UINT;
			case 3: return VK_FORMAT_R8G8B8_UINT;
			case 2: return VK_FORMAT_R8G8_UINT;
			case 1: return VK_FORMAT_R8_UINT;
			}
		}
	    break;
    default:
        assert(false);
	}

	assert(false); // abandon all hope
	return VK_FORMAT_UNDEFINED;
}
// clang-format on

// Converts a le shader stage enum to a vulkan shader stage flag bit
// Currently these are kept in sync which means conversion is a simple
// matter of initialising one from the other.
static inline VkShaderStageFlagBits le_to_vk( const le::ShaderStage& stage ) {
	return VkShaderStageFlagBits( stage );
}

// ----------------------------------------------------------------------
/// \brief   file loader utility method
/// \details loads file given by filepath and returns a vector of chars if successful
/// \note    returns an empty vector if not successful
static bool load_file( const std::filesystem::path& file_path, std::vector<char>& result ) {

	static auto logger = LeLog( LOGGER_LABEL );

	size_t        fileSize = 0;
	std::ifstream file( file_path, std::ios::in | std::ios::binary | std::ios::ate );

	if ( !file.is_open() ) {
		logger.error( "Unable to open file: '%s'", file_path.c_str() );
		return false;
	}

	logger.debug( "Opened file : '%s'", std::filesystem::canonical( ( file_path ) ).c_str() );

	// ----------| invariant: file is open

	auto endOfFilePos = file.tellg();

	if ( endOfFilePos > 0 ) {
		fileSize = size_t( endOfFilePos );
	} else {
		return false;
	}

	// ----------| invariant: file has some bytes to read
	result.resize( fileSize );

	file.seekg( 0, std::ios::beg );
	file.read( result.data(), endOfFilePos );
	file.close();

	return true;
}

// ----------------------------------------------------------------------
// Returns the hash for a given shaderModule
static inline uint64_t le_shader_module_get_hash( le_shader_manager_o* manager, le_shader_module_handle handle ) {
	auto module = manager->shaderModules.try_find( handle );
	assert( module != nullptr );
	return module->hash;
}

// Returns the stage for a given shader module
static le::ShaderStage le_shader_module_get_stage( le_pipeline_manager_o* manager, le_shader_module_handle handle ) {
	auto module = manager->shaderManager->shaderModules.try_find( handle );
	assert( module != nullptr );
	return module->stage;
}

// ----------------------------------------------------------------------

static bool check_is_data_spirv( const void* raw_data, size_t data_size ) {

	struct SpirVHeader {
		uint32_t magic; // Spirv magic number
		union {         // Spir-V version number, bytes (high to low): [0x00, major, minor, 0x00]
			struct {
				uint8_t padding_low;
				uint8_t version_minor;
				uint8_t version_major;
				uint8_t padding_hi;
			};
			uint32_t version_number;
		};
		uint32_t gen_magic; // Generator magic number
		uint32_t bound;     //
		uint32_t reserved;  // Reserved
	} file_header;

	if ( data_size < sizeof( SpirVHeader ) ) {
		// Ahem, file does not even contain a header, what were you thinking?
		return false;
	}

	// ----------| invariant: file contains enough bytes for a valid file header

	static const uint32_t SPIRV_MAGIC = 0x07230203; // magic number for spir-v files, see: <https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#_a_id_physicallayout_a_physical_layout_of_a_spir_v_module_and_instruction>

	memcpy( &file_header, raw_data, sizeof( file_header ) );

	if ( file_header.magic == SPIRV_MAGIC ) {
		return true;
	} else {
		// Invalid file header for spir-v file.
		//		std::cerr << "ERROR: Invalid header for SPIR-V file detected." << std::endl
		//		          << std::flush;
		return false;
	}
}

// ----------------------------------------------------------------------

/// \brief translate a binary blob into spirv code if possible
/// \details Blob may be raw spirv data, or glsl data
static bool translate_to_spirv_code(
    le_shader_compiler_o*      shader_compiler,
    void*                      raw_data,
    size_t                     numBytes,
    LeShaderSourceLanguageEnum shader_source_language,
    le::ShaderStage            moduleType,
    const char*                original_file_name,
    std::vector<uint32_t>&     spirvCode,
    std::set<std::string>&     includesSet,
    std::string const&         shaderDefines ) {

	ZoneScoped;

	bool result = false;

	if ( check_is_data_spirv( raw_data, numBytes ) ) {
		spirvCode.resize( numBytes / 4 );
		memcpy( spirvCode.data(), raw_data, numBytes );
		result = true;
	} else {

		// ----------| Invariant: Data is not SPIRV, it still needs to be compiled

		using namespace le_shader_compiler;

		auto compilation_result = compiler_i.result_create();

		compiler_i.compile_source(
		    shader_compiler,
		    static_cast<const char*>( raw_data ), numBytes,
		    shader_source_language,
		    moduleType,
		    original_file_name,
		    shaderDefines.c_str(),
		    shaderDefines.size(),
		    compilation_result );

		if ( compiler_i.result_get_success( compilation_result ) == true ) {
			const char* addr;
			size_t      res_sz;
			compiler_i.result_get_bytes( compilation_result, &addr, &res_sz );
			spirvCode.resize( res_sz / 4 );
			memcpy( spirvCode.data(), addr, res_sz );

			// -- grab a list of includes which this compilation unit depends on:
			const char* pStr  = nullptr;
			size_t      strSz = 0;

			while ( compiler_i.result_get_includes( compilation_result, &pStr, &strSz ) ) {
				// -- update set of includes for this module
				includesSet.emplace( pStr, strSz );
			}
			result = true;
		} else {
			result = false;
		}

		// Release compile result object
		compiler_i.result_destroy( compilation_result );
	}
	return result;
}

// ----------------------------------------------------------------------

// Flags all modules which are affected by a change in shader_source_file_path,
// and adds them to a set of shader modules wich need to be recompiled.
// Note: This method is called via a file changed callback.
static void le_pipeline_cache_flag_affected_modules_for_source_path( le_shader_manager_o* self, const char* shader_source_file_path ) {
	// find all modules from dependencies set
	// insert into list of modified modules.

	static auto logger = LeLog( LOGGER_LABEL );

	auto lck = std::unique_lock( self->protected_module_dependencies.mtx );

	if ( 0 == self->protected_module_dependencies.moduleDependencies.count( shader_source_file_path ) ) {
		// -- no matching dependencies.
		logger.info( "Shader code update detected, but no modules using shader source file: '%s'", shader_source_file_path );
		return;
	}

	// ---------| invariant: at least one module depends on this shader source file.

	auto const& moduleDependencies = self->protected_module_dependencies.moduleDependencies[ shader_source_file_path ];

	// -- add all affected modules to the set of modules which depend on this shader source file.

	for ( auto const& m : moduleDependencies ) {
		self->modifiedShaderModules.insert( m );
	}
};

// ----------------------------------------------------------------------
static void le_shader_file_watcher_on_callback( const char* path, void* user_data ) {
	auto shader_manager = static_cast<le_shader_manager_o*>( user_data );
	// call a method on backend to tell it that the file path has changed.
	// backend to figure out which modules are affected.
	static auto logger = LeLog( LOGGER_LABEL );
	logger.debug( "Source file update detected: '%s'", path );
	le_pipeline_cache_flag_affected_modules_for_source_path( shader_manager, path );
}
// ----------------------------------------------------------------------

// Thread-safety: needs exclusive access to shader_manager->moduleDependencies for full duration
// We use a lock for this reason.
static void le_pipeline_cache_set_module_dependencies_for_watched_file( le_shader_manager_o* self, le_shader_module_handle module, std::set<std::string>& sourcePaths ) {

	// To be able to tell quickly which modules need to be recompiled if a source file changes,
	// we build map from source file to modules which depend on the source file.
	//
	// we do this by, for each new module, we record all its source files, and we store
	// a reference back to the module.
	//
	static auto logger = LeLog( LOGGER_LABEL );
	auto        lck    = std::unique_lock( self->protected_module_dependencies.mtx );

	if ( !sourcePaths.empty() ) {
		logger.debug( "Shader module (%p):", module );
	}

	for ( const auto& s : sourcePaths ) {

		// If no previous entry for this source path existed, we must insert a watch for this path
		// the watch will call a backend method which figures out how many modules were affected.
		if ( 0 == self->protected_module_dependencies.moduleDependencies.count( s ) ) {

			// this is the first time this file appears on our radar. Let's create a file watcher for it.

			le_file_watcher_watch_settings settings;
			settings.filePath                                                 = s.c_str();
			settings.callback_user_data                                       = self;
			settings.callback_fun                                             = ( file_watcher_callback_fun_t )( le_core_forward_callback( le_backend_vk_api_i->private_shader_file_watcher_i.on_callback_addr ) );
			auto watch_id                                                     = le_file_watcher::le_file_watcher_i.add_watch( self->shaderFileWatcher, &settings );
			self->protected_module_dependencies.moduleWatchIds[ s ]           = watch_id;
			self->protected_module_dependencies.moduleWatchCallbackAddrs[ s ] = settings.callback_fun;

			logger.debug( "\t (+) watch for file '%s'", std::filesystem::relative( s ).c_str() );
		}

		logger.debug( "\t + '%s'", std::filesystem::relative( s ).c_str() );

		self->protected_module_dependencies.moduleDependencies[ s ].insert( module );
	}
}

// Thread-safety: needs exclusive access to shader_manager->moduleDependencies for full duration.
// We use a lock for this reason.
static void le_pipeline_cache_remove_module_from_dependencies( le_shader_manager_o* self, le_shader_module_handle module ) {
	static auto logger = LeLog( LOGGER_LABEL );
	// iterate over all module dependencies in shader manager, remove the module.
	// then remove any file watchers which have no modules left.
	auto lck = std::unique_lock( self->protected_module_dependencies.mtx );

	for ( auto d = self->protected_module_dependencies.moduleDependencies.begin();
	      d != self->protected_module_dependencies.moduleDependencies.end(); ) {
		// if we find module, we remove it.
		auto it = d->second.find( module );
		if ( it != d->second.end() ) {
			d->second.erase( it );
		}
		// if there are no more modules in the entry this means that this file doesn't need to be
		// watched anymore.
		if ( d->second.empty() ) {
			int watch_id = self->protected_module_dependencies.moduleWatchIds[ d->first ];
			le_file_watcher::le_file_watcher_i.remove_watch( self->shaderFileWatcher, watch_id );
			// remove watcher handle
			self->protected_module_dependencies.moduleWatchIds.erase( d->first );

			// Remove the callback forwarder from our list of callback forwarders
			le_core_forward_callback_release( self->protected_module_dependencies.moduleWatchCallbackAddrs.at( d->first ) );
			// remove the entry which refers to the callback forwarder
			self->protected_module_dependencies.moduleWatchCallbackAddrs.erase( d->first );

			logger.debug( "\t (-) watch for file '%s'", std::filesystem::relative( d->first ).c_str() );

			// remove file entry
			d = self->protected_module_dependencies.moduleDependencies.erase( d ); // must do this because we're erasing from within

		} else {
			d++;
		}
	}
}

// ----------------------------------------------------------------------
// HOT PATH: this gets executed every frame - for every pipeline switch
//
// Calculates a hash of hashes over all pipeline layout hashes over all
// shader stages held in array shader_modules
// Note that shader_modules must have not more than 16 elements.
static uint64_t shader_modules_get_pipeline_layout_hash( le_shader_manager_o* shader_manager, le_shader_module_handle const* shader_modules, size_t numModules ) {

	assert( numModules <= 16 ); // note max 16 shader modules.

	// We use a stack-allocated c-array instead of vector so that
	// temporary allocations happens on the stack and not on the
	// free store. The number of shader modules will always be very
	// small.

	uint64_t pipeline_layout_hash_data[ 16 ];

	le_shader_module_handle const* end_shader_modules = shader_modules + numModules;

	uint64_t* elem = pipeline_layout_hash_data; // Get first element

	for ( auto s = shader_modules; s != end_shader_modules; s++ ) {
		auto p_module = ( shader_manager->shaderModules.try_find( *s ) );
		assert( p_module && "shader module was not found" );
		if ( p_module ) {
			*elem = p_module->hash_pipelinelayout;
		}
		elem++;
	}

	return SpookyHash::Hash64( pipeline_layout_hash_data, numModules * sizeof( uint64_t ), 0 );
}

// Collect push_constant block sizes, and shader stages for all given shaders.
//
//
// Re: push_constant block sizes:
// Realistically, you should make sure that all shader modules declare the same push constant block, as
// we don't support per-shader stage push constants, because on Desktop GPUs we don't expect push constant
// broadcasting to be much more costly than to set push constants individually per shader stage. It is also
// considerably simpler to implement, as we don't have to keep track of whether push constant ranges declared
// in different shader stages are aliasing.
static void shader_modules_collect_info( le_shader_manager_o* shader_manager, le_shader_module_handle const* shader_modules, size_t numModules, size_t* push_constant_buffer_size_max, VkShaderStageFlags* shader_stage_flags ) {
	for ( le_shader_module_handle const *s = shader_modules, *s_end = shader_modules + numModules; s != s_end; s++ ) {
		auto p_module = ( shader_manager->shaderModules.try_find( *s ) );
		assert( p_module && "shader module was not found" );
		if ( p_module ) {
			*push_constant_buffer_size_max = std::max<uint64_t>( p_module->push_constant_buffer_size, *push_constant_buffer_size_max );
			*shader_stage_flags |= le_to_vk( p_module->stage );
		}
	}
}

inline static uint64_t le_shader_bindings_calculate_hash( le_shader_binding_info const* info_vec, size_t info_count ) {
	uint64_t hash = 0;

	le_shader_binding_info const* info_begin = info_vec;
	auto const                    info_end   = info_vec + info_count;

	for ( le_shader_binding_info const* info = info_begin; info != info_end; info++ ) {
		hash = SpookyHash::Hash64( info, offsetof( le_shader_binding_info, name_hash ), hash );
	}

	return hash;
}

static void shader_module_update_reflection( le_shader_module_o* module ) {

	static auto                         logger = LeLog( LOGGER_LABEL );
	std::vector<le_shader_binding_info> bindings; // <- gets stored in module at end

	SpvReflectShaderModule spv_module;
	SpvReflectResult       spv_result{};

	spv_result = spvReflectCreateShaderModule( module->spirv.size() * sizeof( uint32_t ), module->spirv.data(), &spv_module );

	assert( spv_result == SPV_REFLECT_RESULT_SUCCESS );

	// ---------| Invariant: spv_module created successfully.

	// If this shader module represents a vertex shader, we parse default vertex attribute bindigs.
	//
	// We assign one location to each binding by default. If you want to do more fancy layouts
	// for your vertex attributes, you must specify these explicitly when creating your pipeline.
	// What we generate here represents the fallback vertex attribute bindings for this shader.
	//
	if ( module->stage == le::ShaderStage::eVertex ) {
		std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions; // <- gets stored in module at end
		std::vector<VkVertexInputBindingDescription>   vertexBindingDescriptions;   // <- gets stored in module at end
		std::vector<std::string>                       vertexAttributeNames;        // <- gets stored in module at end

		size_t input_count = spv_module.input_variable_count;

		struct AttributeBindingDescription {
			VkVertexInputAttributeDescription attribute;
			VkVertexInputBindingDescription   binding;
			std::string                       name;
		};

		std::vector<AttributeBindingDescription> input_descriptions;
		input_descriptions.reserve( input_count );

		for ( size_t i = 0; i != input_count; i++ ) {
			auto& input = spv_module.input_variables[ i ];
			if ( input->location != uint32_t( ~0 ) ) {

				input_descriptions.emplace_back();
				input_descriptions.back() = {
				    .attribute = {
				        .location = input->location,                                    // by default, one binding per location
				        .binding  = input->location,                                    // by default, one binding per location
				        .format   = vk_format_from_spv_reflect_format( input->format ), // derive format from spv type
				        .offset   = 0,                                                  // non-interleaved means offset must be 0
				    },
				    .binding = {
				        .binding   = input->location,
				        .stride    = byte_stride_from_spv_type_description( input->type_description->traits.numeric ),
				        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
				    },
				    .name = input->name,
				};
			}
		}

		std::sort(
		    input_descriptions.begin(), input_descriptions.end(),
		    []( AttributeBindingDescription const& lhs, AttributeBindingDescription const& rhs ) -> bool {
			    return lhs.attribute.location < rhs.attribute.location;
		    } );

		for ( auto& d : input_descriptions ) {
			vertexAttributeDescriptions.emplace_back( std::move( d.attribute ) );
			vertexBindingDescriptions.emplace_back( std::move( d.binding ) );
			vertexAttributeNames.emplace_back( std::move( d.name ) );
		}

		input_descriptions.clear();

#ifndef NDEBUG
		constexpr bool CHECK_LOCATIONS_ARE_CONSECUTIVE = false;
		if ( CHECK_LOCATIONS_ARE_CONSECUTIVE ) {
			// Ensure that locations are sorted asc, and there are no holes.
			if ( vertexAttributeDescriptions.size() > 1 ) {
				auto prev_l = vertexAttributeDescriptions.begin();
				assert( prev_l->location == 0 );
				for ( auto l = prev_l + 1; l != vertexAttributeDescriptions.end(); prev_l++, l++ ) {
					assert( l->location == prev_l->location + 1 );
				}
			}
		}
#endif

		// Store vertex input info with module
		module->vertexAttributeDescriptions = std::move( vertexAttributeDescriptions );
		module->vertexBindingDescriptions   = std::move( vertexBindingDescriptions );
		module->vertexAttributeNames        = std::move( vertexAttributeNames );
	}

	for ( size_t set_idx = 0; set_idx != spv_module.descriptor_set_count; set_idx++ ) {
		auto const& set = spv_module.descriptor_sets[ set_idx ];

		for ( size_t binding_idx = 0; binding_idx != set.binding_count; binding_idx++ ) {
			auto const& binding = set.bindings[ binding_idx ];

			le_shader_binding_info info{};

			info.setIndex   = binding->set;
			info.binding    = binding->binding;
			info.type       = descriptor_type_from_spv_descriptor_type( binding->descriptor_type );
			info.stage_bits = uint32_t( module->stage );
			info.count      = binding->count;

			// Dynamic uniform buffers need to specify a range given in bytes.
			if ( info.type == le::DescriptorType::eUniformBufferDynamic ) {
				info.range = binding->block.size;
			}

			if ( std::string::npos != std::string( binding->name ).find( TEXTURE_NAME_YCBCR_REQUEST_STRING ) ) {

				// If the binding name contains the special string value "__ycbcr__", then
				// we set the .immutable_sampler value with a special sentinel - this
				// will be replaced by an actual immutable VkSampler when creating the
				// DescriptorSet, see `le_pipeline_cache_produce_descriptor_set_layout`

				logger.info( "Detected immutable sampler: [%s]", binding->name );
				info.immutable_sampler = VkSampler( le_shader_module_o::ImmutableSamplerRequestedValue::eYcBcR );
			}

			// For buffer Types the name of the binding we're interested in is the type name.
			if ( info.type == le::DescriptorType::eUniformBufferDynamic ||
			     info.type == le::DescriptorType::eStorageBufferDynamic ) {
				info.name_hash = hash_64_fnv1a( binding->type_description->type_name );
			} else {
				info.name_hash = hash_64_fnv1a( binding->name );
			}

			bindings.emplace_back( std::move( info ) );
		}
	}

	// Sort bindings - this makes it easier for us to link shader stages together
	std::sort( bindings.begin(), bindings.end() ); // we're sorting shader bindings by set, binding ASC

	// -- calculate hash over bindings
	module->hash_pipelinelayout = le_shader_bindings_calculate_hash( bindings.data(), bindings.size() );

	// -- calculate hash over push constant range - if any

	if ( spv_module.push_constant_block_count > 0 ) {

		if ( spv_module.push_constant_block_count != 1 ) {
			logger.error( "Push constant block count must be either 0 or 1, but is %d.", spv_module.push_constant_block_count );
			assert( false && "push constannt block count must be either 0 or 1" );
		}

		module->push_constant_buffer_size = spv_module.push_constant_blocks[ 0 ].size;
		module->hash_pipelinelayout       = SpookyHash::Hash64( &module->push_constant_buffer_size, sizeof( uint64_t ), module->hash_pipelinelayout );
	}

	// -- store bindings with module
	module->bindings = std::move( bindings );

	// we must clean up after ourselves.
	spvReflectDestroyShaderModule( &spv_module );
}

// ----------------------------------------------------------------------

/// \brief compare sorted bindings and raise the alarm if two successive bindings alias locations
static bool shader_module_check_bindings_valid( le_shader_binding_info const* bindings, size_t numBindings ) {
	static auto logger = LeLog( LOGGER_LABEL );

	// -- perform sanity check on bindings - bindings must be unique:
	// (location+binding cannot be shared between shader uniforms)

	auto b_start = bindings;
	auto b_end   = b_start + numBindings;

	for ( auto b = b_start, b_prev = b_start; b != b_end; b++ ) {

		if ( b == b_prev ) {
			// first iteration
			continue;
		}

		if ( b->setIndex == b_prev->setIndex &&
		     b->binding == b_prev->binding ) {
			logger.error( "Illegal shader bindings detected, rejecting shader." );
			logger.error( "Duplicate bindings for set: %d, binding %d", b->setIndex, b->binding );
			return false;
		}

		b_prev = b;
	}

	return true;
}

// Create union of bindings over shader stages based on the
// invariant that each shader stage provides their bindings
// in ascending order.
//
// Returns a vector with binding info, combined over all shader stages given.
// Note: Bindings *must not* be sparse, otherwise this method will assert(false)
//
//
static std::vector<le_shader_binding_info> shader_modules_merge_bindings( le_shader_manager_o* shader_manager, le_shader_module_handle const* shader_handles, size_t shader_handles_count ) {

	static auto logger = LeLog( LOGGER_LABEL );
	// maxNumBindings holds the upper bound for the total number of bindings
	// assuming no overlaps in bindings between shader stages.

	std::vector<le_shader_module_o*> shader_stages;
	shader_stages.reserve( shader_handles_count );

	for ( auto s = shader_handles; s != shader_handles + shader_handles_count; s++ ) {
		auto m = shader_manager->shaderModules.try_find( *s );
		if ( m ) {
			shader_stages.emplace_back( m );
		} else {
			assert( false && "shader module not found" );
		}
	}

	std::vector<le_shader_binding_info> all_bindings;

	// accumulate all bindings

	for ( auto& s : shader_stages ) {
		all_bindings.insert( all_bindings.end(), ( s )->bindings.begin(), ( s )->bindings.end() );
	}

	auto get_filepaths_affected_by_message = []( std::vector<le_shader_module_o*> const& shader_stages,
	                                             uint32_t                                stage_bitfield ) {
		std::ostringstream os;

		// print out filenames for shader stage which matches stage bitflag

		for ( auto& s : shader_stages ) {
			if ( uint32_t( ( s )->stage ) & stage_bitfield ) {
				os << "\t '" << ( s )->filepath << "'" << std::endl;
			}
		}
		return os.str();
	};

	// -- Sort all_bindings so that they are ordered by set, location

	std::sort( all_bindings.begin(), all_bindings.end() );

	// -- Merge bindings, so that elements with common set, binding number are kept together.

	std::vector<le_shader_binding_info> combined_bindings;
	le_shader_binding_info*             last_binding = nullptr;

	for ( auto& b : all_bindings ) {

		if ( nullptr == last_binding ) {
			combined_bindings.emplace_back( b );
			last_binding = &combined_bindings.back();
			// First iteration does not need to do any comparison
			// because there is by definition only one element in
			// combined_bindings at this stage.
			continue;
		}

		// ----------| Invariant: There is a last_binding

		// -- Check current binding against last_binding

		if ( b == *last_binding ) {
			// -- Skip if fully identical
			continue;
		}

		// Attempt to merge binding info, if set id and location match

		if ( b.setIndex == last_binding->setIndex &&
		     b.binding == last_binding->binding ) {

			// -- Attempt to merge

			// We must compare bindings' count, range and type to make sure these
			// are identical for bindings which are placed at the same set and
			// location.

			if ( b.count == last_binding->count &&
			     b.range == last_binding->range &&
			     b.type == last_binding->type ) {

				// -- Name must be identical

				if ( b.name_hash != last_binding->name_hash ) {

					// If name hash is not equal, then try to recover
					// by choosing the namehash which has the lowest stage
					// flag bits set. This ensures that names in vert shaders
					// have precedence over names in frag shaders for example.

					if ( b.stage_bits < last_binding->stage_bits ) {
						last_binding->name_hash = b.name_hash;
						logger.warn( "Name for binding at set: %d, location: %d did not match.", b.setIndex, b.binding );
						logger.warn( "Affected files:\n%s",
						             get_filepaths_affected_by_message( shader_stages, uint32_t( b.stage_bits | last_binding->stage_bits ) ).c_str() );
					}
				}

				// Merge stage bits.

				last_binding->stage_bits |= b.stage_bits;

				continue;
			} else {
				assert( false && "descriptor at position set/binding must refer to same count, range and type." );
			}

		} else {
			// New binding -- we should probably check that set number is continuous
			// and, if not, insert placeholder sets with empty bindings.
			combined_bindings.emplace_back( b );
			last_binding = &combined_bindings.back();
		}
	}

	return combined_bindings;
}

// ----------------------------------------------------------------------

static void le_shader_manager_shader_module_update( le_shader_manager_o* self, le_shader_module_handle handle ) {

	// Shader module needs updating if shader code has changed.
	// if this happens, a new vulkan object for the module must be created.

	// The module must be locked for this, as we need exclusive access just in case the module is
	// in use by the frame recording thread, which may want to create pipelines.
	//
	// Vulkan lifetimes require us only to keep module alive for as long as a pipeline is being
	// generated from it. This means we "only" need to protect against any threads which might be
	// creating pipelines.

	auto module = self->shaderModules.try_find( handle );
	assert( module && "module not found" );

	// -- get module spirv code
	std::vector<char> source_text;

	if ( !load_file( module->filepath, source_text ) ) {
		// file could not be loaded. bail out.
		return;
	}

	std::vector<uint32_t> spirv_code;
	std::set<std::string> includesSet{ { module->filepath.string() } }; // let first element be the original source file path

	translate_to_spirv_code( self->shader_compiler, source_text.data(), source_text.size(), { module->source_language }, module->stage, module->filepath.string().c_str(), spirv_code, includesSet, module->macro_defines );

	if ( spirv_code.empty() ) {
		// no spirv code available, bail out.
		return;
	}

	module->hash_shader_defines = SpookyHash::Hash64( module->macro_defines.data(), module->macro_defines.size(), 0 );

	// -- check spirv code hash against module spirv hash
	uint64_t hash_of_module = SpookyHash::Hash64( spirv_code.data(), spirv_code.size() * sizeof( uint32_t ), module->hash_shader_defines );

	if ( hash_of_module == module->hash ) {
		// spirv code identical, no update needed, bail out.
		return;
	}

	le_shader_module_o previous_module = *module; // create backup copy

	// -- update module hash
	module->hash = hash_of_module;

	le_pipeline_cache_remove_module_from_dependencies( self, handle );
	// -- update additional include paths, if necessary.
	le_pipeline_cache_set_module_dependencies_for_watched_file( self, handle, includesSet );

	// ---------| Invariant: new spir-v code detected.

	// -- if hash doesn't match, delete old vk module, create new vk module

	// -- store new spir-v code
	module->spirv = std::move( spirv_code );

	// -- update bindings via spirv-cross, and update bindings hash
	shader_module_update_reflection( module );

	if ( false == shader_module_check_bindings_valid( module->bindings.data(), module->bindings.size() ) ) {
		// we must clean up, and report an error
		*module = previous_module;
		return;
	}

	// -- delete old vulkan shader module object
	// Q: Should we rather defer deletion? In case that this module is in use?
	// A: Not really - according to spec module must only be alife while pipeline is being compiled.
	//    If we can guarantee that no other process is using this module at the moment to compile a
	//    Pipeline, we can safely delete it.
	vkDestroyShaderModule( self->device, module->module, nullptr );
	module->module = nullptr;

	// -- create new vulkan shader module object

	VkShaderModuleCreateInfo createInfo = {
	    .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
	    .pNext    = nullptr,
	    .flags    = 0,
	    .codeSize = module->spirv.size() * sizeof( uint32_t ),
	    .pCode    = module->spirv.data(),
	};

	vkCreateShaderModule( self->device, &createInfo, nullptr, &module->module );
}

// ----------------------------------------------------------------------
// this method is called via renderer::update - before frame processing.
static void le_shader_manager_update_shader_modules( le_shader_manager_o* self ) {

	// -- find out which shader modules have been tainted

	// this will call callbacks on any watched file objects as a side effect
	// callbacks will modify le_backend->modifiedShaderModules
	le_file_watcher::le_file_watcher_i.poll_notifications( self->shaderFileWatcher );

	// -- update only modules which have been tainted

	for ( auto& s : self->modifiedShaderModules ) {
		le_shader_manager_shader_module_update( self, s );
	}

	self->modifiedShaderModules.clear();
}

// ----------------------------------------------------------------------

le_shader_manager_o* le_shader_manager_create( VkDevice_T* device ) {
	auto self = new le_shader_manager_o();

	self->device = device;

	// -- create shader compiler
	using namespace le_shader_compiler;
	self->shader_compiler = compiler_i.create();

	// -- create file watcher for shader files so that changes can be detected
	self->shaderFileWatcher = le_file_watcher::le_file_watcher_i.create();

	return self;
}

// ----------------------------------------------------------------------

static void le_shader_manager_destroy( le_shader_manager_o* self ) {

	using namespace le_shader_compiler;
	using namespace le_file_watcher;

	if ( self->shaderFileWatcher ) {
		// -- destroy file watcher
		le_file_watcher_i.destroy( self->shaderFileWatcher );
		self->shaderFileWatcher = nullptr;
	}

	if ( self->shader_compiler ) {
		// -- destroy shader compiler
		compiler_i.destroy( self->shader_compiler );
		self->shader_compiler = nullptr;
	}

	// -- destroy retained shader modules
	self->shaderModules.iterator( []( le_shader_module_o* module, void* user_data ) {
		VkDevice device = *static_cast<VkDevice*>( user_data );
		vkDestroyShaderModule( device, module->module, nullptr );
	},
	                              &self->device );

	self->shaderModules.clear();
	delete self;
}

// ----------------------------------------------------------------------
/// \brief create vulkan shader module based on file path
/// \details FIXME: this method can get called nearly anywhere - it should not be publicly accessible.
/// ideally, this method is only allowed to be called in the setup phase.
///
static le_shader_module_handle le_shader_manager_create_shader_module(
    le_shader_manager_o*              self,
    char const*                       path,
    const LeShaderSourceLanguageEnum& shader_source_language,
    const le::ShaderStage&            moduleType,
    char const*                       macro_defines_,
    le_shader_module_handle           handle,
    VkSpecializationMapEntry const*   specialization_map_entries,
    uint32_t                          specialization_map_entries_count,
    void*                             specialization_map_data,
    uint32_t                          specialization_map_data_num_bytes ) {

	static auto logger = LeLog( LOGGER_LABEL );

	// We use the canonical path to store a fingerprint of the file
	auto canonical_path_as_string = std::filesystem::canonical( path ).string();

	std::string macro_defines = macro_defines_ ? std::string( macro_defines_ ) : "";

	// We include specialization data into hash calculation for this module, because specialization data
	// is stored with the module, and therefore it contributes to the module's phenotype.
	//
	uint64_t hash_specialization_constants = 0;

	if ( specialization_map_entries_count != 0 ) {
		hash_specialization_constants = SpookyHash::Hash64( specialization_map_data, specialization_map_data_num_bytes, hash_specialization_constants );
		hash_specialization_constants = SpookyHash::Hash64( specialization_map_entries, sizeof( VkSpecializationMapEntry ) * specialization_map_entries_count, hash_specialization_constants );
	}

	uint64_t hash_shader_defines = SpookyHash::Hash64( macro_defines.data(), macro_defines.size(), hash_specialization_constants );

	uint64_t hash_input_parameters = SpookyHash::Hash64( canonical_path_as_string.data(), canonical_path_as_string.size(), hash_shader_defines );

	// If no explicit handle is given, we create one by hashing
	// input parameters.
	//
	// We do this so that the same input parameters give us the same handle,
	// this means that if the shader source changes, we can update the corresponding
	// module.
	//
	// If an explicit handle is given, then we will attempt to update the module
	// regardless of whether input parameters have changed. This can make sense for
	// engine-internal shaders, such as imgui shaders, for which we know that there
	// will only ever be one unique module per shader source and usage.
	if ( handle == nullptr ) {
		handle = reinterpret_cast<le_shader_module_handle>( hash_input_parameters );
	}

	std::vector<char> raw_file_data;

	if ( !load_file( canonical_path_as_string, raw_file_data ) ) {
		logger.error( "Could not load shader file: '%s'", path );
		assert( false && "file loading was unsuccessful" );
		return nullptr;
	}

	// ---------| invariant: load was successful

	// -- Make sure the file contains spir-v code.

	std::vector<uint32_t> spirv_code;
	std::set<std::string> includesSet = { { canonical_path_as_string } }; // let first element be the source file path

	translate_to_spirv_code( self->shader_compiler, raw_file_data.data(), raw_file_data.size(), shader_source_language, moduleType, path, spirv_code, includesSet, macro_defines );

	le_shader_module_o module{};
	module.stage               = moduleType;
	module.filepath            = canonical_path_as_string;
	module.macro_defines       = macro_defines;
	module.hash_shader_defines = hash_shader_defines;

	module.hash            = SpookyHash::Hash64( spirv_code.data(), spirv_code.size() * sizeof( uint32_t ), module.hash_shader_defines );
	module.spirv           = std::move( spirv_code );
	module.source_language = shader_source_language;
	module.specialization_map_info.data.assign(
	    static_cast<char*>( specialization_map_data ),
	    static_cast<char*>( specialization_map_data ) + specialization_map_data_num_bytes );
	module.specialization_map_info.entries.assign(
	    reinterpret_cast<VkSpecializationMapEntry const*>( specialization_map_entries ),
	    reinterpret_cast<VkSpecializationMapEntry const*>( specialization_map_entries ) + specialization_map_entries_count );

	static_assert( sizeof( VkSpecializationMapEntry ) == sizeof( VkSpecializationMapEntry ), "SpecializationMapEntry must be of same size, whether using vkhpp or not." );

	le_shader_module_o* cached_module = self->shaderModules.try_find( handle );

	if ( cached_module && cached_module->hash == module.hash ) {
		// A module with the same handle already exists, and the cached
		// version has the same hash as our new version: no more work to do.
		logger.info( "Found cached shader module for '%s'.", path );
		return handle;
	}

	//----------| Invariant: there is either no old module, or the old module does not match our new module.

	shader_module_update_reflection( &module );

	if ( false == shader_module_check_bindings_valid( module.bindings.data(), module.bindings.size() ) ) {
		// we must clean up, and report an error
		logger.error( "Shader module reports invalid bindings" );
		assert( false );
		return nullptr;
	}
	// ----------| invariant: bindings sanity check passed

	VkShaderModuleCreateInfo createInfo = {
	    .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
	    .pNext    = nullptr, // optional
	    .flags    = 0,       // optional
	    .codeSize = module.spirv.size() * sizeof( uint32_t ),
	    .pCode    = module.spirv.data(),
	};

	vkCreateShaderModule( self->device, &createInfo, nullptr, &module.module );
	logger.info( "Vk shader module created %p", module.module );

	if ( cached_module == nullptr ) {
		// there is no prior module - let's create a module and try to retain it in shader manager
		bool insert_successful = self->shaderModules.try_insert( handle, &module );
		if ( !insert_successful ) {
			logger.error( "Could not retain shader module" );
			vkDestroyShaderModule( self->device, module.module, nullptr );
			logger.debug( "Vk shader module destroyed %p", module.module );
			return nullptr;
		}
	} else {

		le_pipeline_cache_remove_module_from_dependencies( self, handle );

		// -- invariant: the old module has a different hash than our new module.
		// we must swap the two ...
		auto old_module = *cached_module;
		*cached_module  = module;
		// ... and delete the old module
		vkDestroyShaderModule( self->device, old_module.module, nullptr );
		logger.debug( "Vk shader module destroyed %p", old_module.module );
	}

	// -- add all source files for this file to the list of watched
	//    files that point back to this module
	le_pipeline_cache_set_module_dependencies_for_watched_file( self, handle, includesSet );

	return handle;
}

// ----------------------------------------------------------------------
// Cold path.
// called via decoder / produce_frame - only if we create a vkPipeline
static VkPipelineLayout le_pipeline_manager_get_pipeline_layout( le_pipeline_manager_o* self, le_shader_module_handle const* shader_modules, size_t numModules ) {

	static auto logger             = LeLog( LOGGER_LABEL );
	uint64_t    pipelineLayoutHash = shader_modules_get_pipeline_layout_hash( self->shaderManager, shader_modules, numModules );

	auto foundLayout = self->pipelineLayouts.try_find( pipelineLayoutHash );

	if ( foundLayout ) {
		return *foundLayout;
	} else {
		logger.error( "Could not find pipeline layout with hash: %x", pipelineLayoutHash );
		assert( false );
		return nullptr;
	}
}

// ----------------------------------------------------------------------
// Creates a vulkan graphics pipeline based on a shader state object and a given renderpass and subpass index.
//
static VkPipeline le_pipeline_cache_create_graphics_pipeline( le_pipeline_manager_o* self, graphics_pipeline_state_o const* pso, const BackendRenderPass& pass, uint32_t subpass ) {

	std::vector<VkPipelineShaderStageCreateInfo> pipelineStages;
	pipelineStages.reserve( pso->shaderModules.size() );

	std::vector<VkSpecializationInfo*> p_specialization_infos;
	p_specialization_infos.reserve( pso->shaderModules.size() );

	le_shader_module_o* vertexShaderModule = nullptr; // We may need the vertex shader module later

	for ( auto const& shader_stage : pso->shaderModules ) {

		auto s = self->shaderManager->shaderModules.try_find( shader_stage );
		assert( s && "could not find shader module" );

		// Try to set the vertex shader module pointer while we are at it. We will need it
		// when figuring out default bindings later, as the Vertex module is used to derive
		// default attribute bindings.
		if ( s->stage == le::ShaderStage::eVertex ) {
			vertexShaderModule = s;
		}

		// Create a new (potentially unused) entry for specialization info
		p_specialization_infos.emplace_back( nullptr );
		VkSpecializationInfo*& p_specialization_info = p_specialization_infos.back();

		// Fetch specialization constant data from shader
		// associate it with p_specialization_info

		if ( !s->specialization_map_info.entries.empty() ) {
			p_specialization_info  = new ( VkSpecializationInfo );
			*p_specialization_info = {
			    .mapEntryCount = uint32_t( s->specialization_map_info.entries.size() ),
			    .pMapEntries   = s->specialization_map_info.entries.data(),
			    .dataSize      = s->specialization_map_info.data.size(),
			    .pData         = s->specialization_map_info.data.data(),
			};
		}

		VkPipelineShaderStageCreateInfo info = {
		    .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		    .pNext               = nullptr,
		    .flags               = 0,
		    .stage               = VkShaderStageFlagBits( s->stage ),
		    .module              = s->module,
		    .pName               = "main",
		    .pSpecializationInfo = p_specialization_info, // optional
		};

		pipelineStages.emplace_back( info );
	}

	std::vector<VkVertexInputBindingDescription>   vertexBindingDescriptions;        // Where to get data from
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributeDescriptions; // How it feeds into the shader's vertex inputs

	if ( vertexShaderModule != nullptr ) {

		// We only add vertex attribute bindings if the pipeline contains a vertex stage.
		// If it doesn't, then it is most likely a task/mesh shader pipeline which skips
		// vertex assembly.

		if ( pso->explicitVertexInputBindingDescriptions.empty() ) {
			// Default: use vertex input schema based on shader reflection
			vertexBindingDescriptions        = vertexShaderModule->vertexBindingDescriptions;
			vertexInputAttributeDescriptions = vertexShaderModule->vertexAttributeDescriptions;
		} else {
			// Use vertex input schema based on explicit user input
			// which was stored in `backend_create_graphics_pipeline_state_object`
			vertexBindingDescriptions.reserve( pso->explicitVertexInputBindingDescriptions.size() );
			vertexInputAttributeDescriptions.reserve( pso->explicitVertexAttributeDescriptions.size() );

			// Create vertex input binding descriptions
			for ( auto const& b : pso->explicitVertexInputBindingDescriptions ) {

				VkVertexInputBindingDescription bindingDescription = {
				    .binding   = b.binding,
				    .stride    = b.stride,
				    .inputRate = VkVertexInputRate( b.input_rate ),
				};

				vertexBindingDescriptions.emplace_back( std::move( bindingDescription ) );
			}

			for ( auto const& a : pso->explicitVertexAttributeDescriptions ) {
				VkVertexInputAttributeDescription attributeDescription = {
				    .location = a.location,
				    .binding  = a.binding,
				    .format   = vk_format_from_le_vertex_input_attribute_description( a ),
				    .offset   = a.binding_offset,
				};

				vertexInputAttributeDescriptions.emplace_back( std::move( attributeDescription ) );
			}
		}
	}

	// Combine vertex input `binding` state and vertex input `attribute` state into
	// something that vk will accept
	VkPipelineVertexInputStateCreateInfo vertexInputStageInfo = {
	    .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	    .pNext                           = nullptr,
	    .flags                           = 0,
	    .vertexBindingDescriptionCount   = uint32_t( vertexBindingDescriptions.size() ),
	    .pVertexBindingDescriptions      = vertexBindingDescriptions.data(),
	    .vertexAttributeDescriptionCount = uint32_t( vertexInputAttributeDescriptions.size() ),
	    .pVertexAttributeDescriptions    = vertexInputAttributeDescriptions.data(),
	};
	;

	// Fetch VkPipelineLayout for this pso
	auto pipelineLayout = le_pipeline_manager_get_pipeline_layout( self, pso->shaderModules.data(), pso->shaderModules.size() );

	//
	// We must match blend attachment states with number of attachments for
	// the current renderpass - each attachment may have their own blend state.
	// Our pipeline objects will have 16 stages which are readable.
	//
	assert( pass.numColorAttachments <= LE_MAX_COLOR_ATTACHMENTS );
	//
	VkPipelineColorBlendStateCreateInfo colorBlendState =
	    {
	        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
	        .pNext           = nullptr,
	        .flags           = 0,
	        .logicOpEnable   = VK_FALSE,
	        .logicOp         = VK_LOGIC_OP_CLEAR,
	        .attachmentCount = pass.numColorAttachments,
	        .pAttachments    = pso->data.blendAttachmentStates,
	        .blendConstants  = {
	             pso->data.blend_factor_constants[ 0 ],
	             pso->data.blend_factor_constants[ 1 ],
	             pso->data.blend_factor_constants[ 2 ],
	             pso->data.blend_factor_constants[ 3 ],
            },
	    };
	;

	// Viewport and Scissor are tracked as dynamic states, and although this object will not
	// get used, we must still fulfill the contract of providing a valid object to vk.
	//
	static VkPipelineViewportStateCreateInfo defaultViewportState = {
	    .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
	    .pNext         = nullptr,
	    .flags         = 0,
	    .viewportCount = 1,
	    .pViewports    = nullptr,
	    .scissorCount  = 1,
	    .pScissors     = nullptr,
	};
	;

	// We will allways keep Scissor, Viewport and LineWidth as dynamic states,
	// otherwise we might have way too many pipelines flying around.
	VkDynamicState dynamicStates[] = {
	    VK_DYNAMIC_STATE_SCISSOR,
	    VK_DYNAMIC_STATE_VIEWPORT,
	    VK_DYNAMIC_STATE_LINE_WIDTH,
	};

	VkPipelineDynamicStateCreateInfo dynamicState = {
	    .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
	    .pNext             = nullptr,
	    .flags             = 0,
	    .dynamicStateCount = sizeof( dynamicStates ) / sizeof( VkDynamicState ),
	    .pDynamicStates    = dynamicStates,
	};

	// We must patch pipeline multisample state here - this is because we may not know the renderpass a pipeline
	// is used with, and the number of samples such renderpass supports.
	auto multisampleCreateInfo = pso->data.multisampleState;

	multisampleCreateInfo.rasterizationSamples = VkSampleCountFlagBits( pass.sampleCount );

	// setup pipeline

	VkGraphicsPipelineCreateInfo gpi =
	    {
	        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
	        .pNext               = nullptr,                                  //
	        .flags               = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT, //
	        .stageCount          = uint32_t( pipelineStages.size() ),        // set shaders
	        .pStages             = pipelineStages.data(),                    // set shaders
	        .pVertexInputState   = &vertexInputStageInfo,                    //
	        .pInputAssemblyState = &pso->data.inputAssemblyState,            //
	        .pTessellationState  = &pso->data.tessellationState,             //
	        .pViewportState      = &defaultViewportState,                    // not used as these states are dynamic, defaultState is a dummy value to pacify driver
	        .pRasterizationState = &pso->data.rasterizationInfo,             //
	        .pMultisampleState   = &multisampleCreateInfo,                   // <- we patch this with correct sample count for renderpass, because otherwise not possible
	        .pDepthStencilState  = &pso->data.depthStencilState,             //
	        .pColorBlendState    = &colorBlendState,                         //
	        .pDynamicState       = &dynamicState,                            //
	        .layout              = pipelineLayout,                           //
	        .renderPass          = pass.renderPass,                          // must be a valid renderpass.
	        .subpass             = subpass,                                  //
	        .basePipelineHandle  = nullptr,                                  // optional
	        .basePipelineIndex   = 0,                                        // -1 signals not to use a base pipeline index
	    };

	VkPipeline pipeline = nullptr;
	auto       result   = vkCreateGraphicsPipelines( self->device, self->vulkanCache, 1, &gpi, nullptr, &pipeline );

	// cleanup temporary specialisation info objects
	for ( auto& p_spec : p_specialization_infos ) {
		delete ( p_spec );
	}
	p_specialization_infos.clear();

	assert( result == VK_SUCCESS && "pipeline must be created successfully" );
	return pipeline;
}

// ----------------------------------------------------------------------

static VkPipeline le_pipeline_cache_create_compute_pipeline( le_pipeline_manager_o* self, compute_pipeline_state_o const* pso ) {

	// Fetch VkPipelineLayout for this pso
	auto pipelineLayout = le_pipeline_manager_get_pipeline_layout( self, &pso->shaderStage, 1 );
	auto s              = self->shaderManager->shaderModules.try_find( pso->shaderStage );
	assert( s && "shader module could not be found" );

	VkSpecializationInfo* p_specialization_info = nullptr;
	if ( !s->specialization_map_info.entries.empty() ) {
		p_specialization_info  = new ( VkSpecializationInfo );
		*p_specialization_info = {
		    .mapEntryCount = uint32_t( s->specialization_map_info.entries.size() ),
		    .pMapEntries   = s->specialization_map_info.entries.data(),
		    .dataSize      = s->specialization_map_info.data.size(),
		    .pData         = s->specialization_map_info.data.data(),
		};
	}

	VkPipelineShaderStageCreateInfo shaderStage = {
	    .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
	    .pNext               = nullptr,
	    .flags               = 0,
	    .stage               = VkShaderStageFlagBits( s->stage ),
	    .module              = s->module,
	    .pName               = "main",
	    .pSpecializationInfo = p_specialization_info,
	};

	VkComputePipelineCreateInfo cpi = {
	    .sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
	    .pNext              = nullptr,
	    .flags              = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
	    .stage              = shaderStage,
	    .layout             = pipelineLayout,
	    .basePipelineHandle = nullptr, // optional
	    .basePipelineIndex  = 0,       // -1 signals not to use base pipeline index
	};

	VkPipeline pipeline = nullptr;
	auto       result   = vkCreateComputePipelines( self->device, self->vulkanCache, 1, &cpi, nullptr, &pipeline );

	// cleanup temporary specialisation info objects
	delete ( p_specialization_info );

	assert( result == VK_SUCCESS && "pipeline must be created successfully" );
	return pipeline;
}

// ----------------------------------------------------------------------

static VkRayTracingShaderGroupTypeKHR le_to_vk( le::RayTracingShaderGroupType const& tp ) {
	// clang-format off
    switch(tp){
	    case (le::RayTracingShaderGroupType::eTrianglesHitGroup  ) : return VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
	    case (le::RayTracingShaderGroupType::eProceduralHitGroup ) : return VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
	    case (le::RayTracingShaderGroupType::eRayGen             ) : return  VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	    case (le::RayTracingShaderGroupType::eMiss               ) : return  VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	    case (le::RayTracingShaderGroupType::eCallable           ) : return VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR; 
    }
	// clang-format on
	assert( false ); // unreachable
	return VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
}

// ----------------------------------------------------------------------

static VkPipeline le_pipeline_cache_create_rtx_pipeline( le_pipeline_manager_o* self, rtx_pipeline_state_o const* pso ) {

	// Fetch VkPipelineLayout for this pso
	auto pipelineLayout = le_pipeline_manager_get_pipeline_layout( self, pso->shaderStages.data(), pso->shaderStages.size() );

	std::vector<VkPipelineShaderStageCreateInfo> pipelineStages;
	pipelineStages.reserve( pso->shaderStages.size() );

	le_shader_module_handle rayGenModule = nullptr; // We may need the ray gen shader module later

	for ( auto const& shader_stage : pso->shaderStages ) {

		auto s = self->shaderManager->shaderModules.try_find( shader_stage );
		assert( s && "could not find shader module" );

		if ( s->stage == le::ShaderStage::eRaygenBitKhr ) {
			rayGenModule = shader_stage;
		}

		VkPipelineShaderStageCreateInfo info = {
		    .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		    .pNext               = nullptr, // optional
		    .flags               = 0,       // optional
		    .stage               = le_to_vk( s->stage ),
		    .module              = s->module,
		    .pName               = "main",
		    .pSpecializationInfo = nullptr,
		};

		pipelineStages.emplace_back( info );
	}

	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shadingGroups;

	shadingGroups.reserve( pso->shaderGroups.size() );

	// Fill in shading groups from pso->groups

	for ( auto const& group : pso->shaderGroups ) {
		VkRayTracingShaderGroupCreateInfoKHR info = {
		    .sType                           = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		    .pNext                           = nullptr, // optional
		    .type                            = le_to_vk( group.type ),
		    .generalShader                   = group.generalShaderIdx,
		    .closestHitShader                = group.closestHitShaderIdx,
		    .anyHitShader                    = group.anyHitShaderIdx,
		    .intersectionShader              = group.intersectionShaderIdx,
		    .pShaderGroupCaptureReplayHandle = nullptr, // optional
		};

		shadingGroups.emplace_back( std::move( info ) );
	}

	VkRayTracingPipelineCreateInfoKHR create_info = {
	    .sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
	    .pNext                        = nullptr,
	    .flags                        = 0,
	    .stageCount                   = uint32_t( pipelineStages.size() ),
	    .pStages                      = pipelineStages.data(),
	    .groupCount                   = uint32_t( shadingGroups.size() ),
	    .pGroups                      = shadingGroups.data(),
	    .maxPipelineRayRecursionDepth = 16, // fixme: this should be either exposed through the api - and limited by the hardware limit
	    .pLibraryInfo                 = 0,
	    .pLibraryInterface            = 0,
	    .pDynamicState                = 0,
	    .layout                       = pipelineLayout,
	    .basePipelineHandle           = nullptr,
	    .basePipelineIndex            = 0,
	};

	VkPipeline pipeline = nullptr;
	auto       result   = vkCreateRayTracingPipelinesKHR( self->device, nullptr, self->vulkanCache, 1, &create_info, nullptr, &pipeline );

	assert( VK_SUCCESS == result );
	return pipeline;
}

// ----------------------------------------------------------------------

/// \brief returns hash key for given bindings, creates and retains new vkDescriptorSetLayout inside backend if necessary
static uint64_t le_pipeline_cache_produce_descriptor_set_layout( le_pipeline_manager_o* self, std::vector<le_shader_binding_info> const& bindings, VkDescriptorSetLayout* layout ) {
	static auto logger = LeLog( LOGGER_LABEL );

	auto& descriptorSetLayouts = self->descriptorSetLayouts; // FIXME: this method only needs rw access to this, and the device

	// -- Calculate hash based on le_shader_binding_infos for this set
	uint64_t set_layout_hash = le_shader_bindings_calculate_hash( bindings.data(), bindings.size() );

	auto foundLayout = descriptorSetLayouts.try_find( set_layout_hash );

	if ( foundLayout ) {

		// -- Layout was found in cache, reuse it.

		*layout = foundLayout->vk_descriptor_set_layout;

	} else {

		// -- Layout was not found in cache, we must create vk objects.

		std::vector<VkDescriptorSetLayoutBinding> vk_bindings;

		vk_bindings.reserve( bindings.size() );

		// We must add immutable samplers here if they have been requested.
		//
		// You can request immutable samplers by annotating texture names with
		// special endings.
		//
		// <https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html>
		//
		//  pImmutableSamplers affects initialization of samplers. If descriptorType
		//  specifies a `VK_DESCRIPTOR_TYPE_SAMPLER` or
		//  `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLE` type descriptor, then
		//  pImmutableSamplers can be used to initialize a set of immutable samplers.
		//  Immutable samplers are permanently bound into the set layout and must not be
		//  changed; updating a `VK_DESCRIPTOR_TYPE_SAMPLER` descriptor with immutable
		//  samplers is not allowed and updates to
		//  a `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` descriptor with immutable samplers
		//  does not modify the samplers (the image views are updated, but the sampler
		//  updates are ignored). If pImmutableSamplers is not NULL, then it is a pointer
		//  to an array of sampler handles that will be copied into the set layout and used
		//  for the corresponding binding. Only the sampler handles are copied; the sampler
		//  objects must not be destroyed before the final use of the set layout and any
		//  descriptor pools and sets created using it. If pImmutableSamplers is NULL, then
		//  the sampler slots are dynamic and sampler handles must be bound into descriptor
		//  sets using this layout. If descriptorType is not one of these descriptor types,
		//  then pImmutableSamplers is ignored.
		//
		//
		// Q: how will an immutable sampler affect the layout hash?
		//
		// A: The sentinel is part of the hashed data - we assume that immutable samplers
		// using the same sentinel and therefore the same conversion sampler are compatible.
		//
		//
		// Q: how is VkSampler lifetime managed?
		// A: all immutable samplers for a setlayout are stored with the set layout. once the
		//    setlayout is destroyed, the samplers are destroyed, too.

		// Note that we allocate VkSamplers on the free store so that their address stays constant
		// Any VkSampler allocated will get freed again in `le_pipeline_manager_destroy`.
		std::vector<VkSampler*> immutable_samplers;

		for ( const auto& b : bindings ) {

			VkSampler* maybe_immutable_sampler = nullptr;

			if ( b.immutable_sampler && ( b.immutable_sampler == VkSampler( le_shader_module_o::ImmutableSamplerRequestedValue::eYcBcR ) ) ) {

				maybe_immutable_sampler = new VkSampler( 0 );

				VkSamplerYcbcrConversionInfo* conversion_info =
				    static_cast<VkSamplerYcbcrConversionInfo*>(
				        le_backend_vk::private_backend_vk_i.get_sampler_ycbcr_conversion_info( self->backend ) );

				// TOOD: we must create a VkSampler (and reget_sampler_ycbcr_conversion_infoto
				// whatever is in our immutable sampler.
				VkSamplerCreateInfo sampler_create_info = {
				    .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, // VkStructureType
				    .pNext                   = conversion_info,                       // void *, optional
				    .flags                   = 0,                                     // VkSamplerCreateFlags, optional
				    .magFilter               = VK_FILTER_LINEAR,                      // VkFilter
				    .minFilter               = VK_FILTER_LINEAR,                      // VkFilter
				    .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,         // VkSamplerMipmapMode
				    .addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, // VkSamplerAddressMode
				    .addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, // VkSamplerAddressMode
				    .addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, // VkSamplerAddressMode
				    .mipLodBias              = 0,                                     // float
				    .anisotropyEnable        = 0,                                     // VkBool32
				    .maxAnisotropy           = 0,                                     // float
				    .compareEnable           = 0,                                     // VkBool32
				    .compareOp               = VK_COMPARE_OP_LESS,                    // VkCompareOp
				    .minLod                  = 0.f,                                   // float
				    .maxLod                  = 1.f,                                   // float
				    .borderColor             = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK, // VkBorderColor
				    .unnormalizedCoordinates = 0,                                     // VkBool32
				};

				VkResult result = vkCreateSampler( self->device, &sampler_create_info, nullptr, maybe_immutable_sampler );
				if ( result != VK_SUCCESS ) {
					logger.error( "could not create immutable sampler" );
				} else {
					immutable_samplers.push_back( maybe_immutable_sampler );
				}
			}

			VkDescriptorSetLayoutBinding binding = {
			    .binding            = b.binding,
			    .descriptorType     = VkDescriptorType( b.type ),
			    .descriptorCount    = b.count,                 // optional
			    .stageFlags         = VkShaderStageFlags( b.stage_bits ),
			    .pImmutableSamplers = maybe_immutable_sampler, // optional
			};

			if ( maybe_immutable_sampler && binding.descriptorCount > 1 ) {
				binding.descriptorCount = 1;
				logger.warn( "If binding has an immutable sampler, it must have just a single binding." );
			}

			vk_bindings.emplace_back( std::move( binding ) );
		}

		VkDescriptorSetLayoutCreateInfo setLayoutInfo = {
		    .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		    .pNext        = nullptr,                        // optional
		    .flags        = 0,                              // optional
		    .bindingCount = uint32_t( vk_bindings.size() ), // optional
		    .pBindings    = vk_bindings.data(),
		};

		vkCreateDescriptorSetLayout( self->device, &setLayoutInfo, nullptr, layout );

		// -- Create descriptorUpdateTemplate
		//
		// The template needs to be created so that data for a VkDescriptorSet
		// can be read from a vector of tightly packed
		// DescriptorData elements.
		//

		VkDescriptorUpdateTemplate updateTemplate;
		{
			std::vector<VkDescriptorUpdateTemplateEntry> entries;

			entries.reserve( bindings.size() );

			size_t base_offset = 0; // offset in bytes into DescriptorData vector, assuming vector is tightly packed.
			for ( const auto& b : bindings ) {

				VkDescriptorUpdateTemplateEntry entry = {
				    .dstBinding      = b.binding,
				    .dstArrayElement = 0, // starting element at this binding to update - always 0
				    .descriptorCount = b.count,
				    .descriptorType  = VkDescriptorType( b.type ),
				    .offset          = 0,
				    .stride          = 0,
				};

				// set offset based on type of binding, so that template reads from correct data

				switch ( b.type ) {
				case le::DescriptorType::eAccelerationStructureKhr:
					entry.offset = base_offset + offsetof( DescriptorData, accelerationStructureInfo );
					break;
				case le::DescriptorType::eUniformTexelBuffer:
					assert( false ); // not implemented
					break;
				case le::DescriptorType::eStorageTexelBuffer:
					assert( false ); // not implemented
					break;
				case le::DescriptorType::eInputAttachment:
					assert( false ); // not implemented
					break;
				case le::DescriptorType::eCombinedImageSampler:                          // fall-through, as this kind of descriptor uses ImageInfo or parts thereof
				case le::DescriptorType::eSampledImage:                                  // fall-through, as this kind of descriptor uses ImageInfo or parts thereof
				case le::DescriptorType::eStorageImage:                                  // fall-through, as this kind of descriptor uses ImageInfo or parts thereof
				case le::DescriptorType::eSampler:                                       // fall-through, as this kind of descriptor uses ImageInfo or parts thereof
					entry.offset = base_offset + offsetof( DescriptorData, imageInfo );  // <- point to first field of ImageInfo
					break;                                                               //
				case le::DescriptorType::eUniformBuffer:                                 // fall-through as this kind of descriptor uses BufferInfo
				case le::DescriptorType::eStorageBuffer:                                 // fall-through as this kind of descriptor uses BufferInfo
				case le::DescriptorType::eUniformBufferDynamic:                          // fall-through as this kind of descriptor uses BufferInfo
				case le::DescriptorType::eStorageBufferDynamic:                          //
					entry.offset = base_offset + offsetof( DescriptorData, bufferInfo ); // <- point to first element of BufferInfo
					break;
				default:
					assert( false && "invalid descriptor type" );
				}

				entry.stride = sizeof( DescriptorData );

				entries.emplace_back( std::move( entry ) );

				base_offset += sizeof( DescriptorData );
			}

			VkDescriptorUpdateTemplateCreateInfo info = {
			    .sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
			    .pNext                      = nullptr, // optional
			    .flags                      = 0,       // optional
			    .descriptorUpdateEntryCount = uint32_t( entries.size() ),
			    .pDescriptorUpdateEntries   = entries.data(),
			    .templateType               = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
			    .descriptorSetLayout        = *layout,
			    .pipelineBindPoint          = {}, // ignored as template type is not push_descriptors
			    .pipelineLayout             = {}, // ignored as template type is not push_descriptors
			    .set                        = {}, // ignored as template type is not push_descriptors
			};

			vkCreateDescriptorUpdateTemplate( self->device, &info, nullptr, &updateTemplate );
		}

		le_descriptor_set_layout_t le_layout_info;
		le_layout_info.vk_descriptor_set_layout      = *layout;
		le_layout_info.binding_info                  = bindings;
		le_layout_info.vk_descriptor_update_template = updateTemplate;
		le_layout_info.immutable_samplers            = immutable_samplers;

		bool result = descriptorSetLayouts.try_insert( set_layout_hash, &le_layout_info );

		assert( result && "descriptorSetLayout insertion must be successful" );
	}

	return set_layout_hash;
}

// ----------------------------------------------------------------------
// Calculates pipeline layout info by first consolidating all bindings
// over all referenced shader modules, and then ordering these by descriptor sets.
//
static le_pipeline_layout_info le_pipeline_manager_produce_pipeline_layout_info( le_pipeline_manager_o* self, le_shader_module_handle const* shader_modules, size_t shader_modules_count ) {
	le_pipeline_layout_info info{};

	std::vector<le_shader_binding_info> combined_bindings = shader_modules_merge_bindings( self->shaderManager, shader_modules, shader_modules_count );

	// -- Create array of DescriptorSetLayouts
	VkDescriptorSetLayout vkLayouts[ 8 ];
	{

		// -- Create one vkDescriptorSetLayout for each set in bindings

		std::vector<std::vector<le_shader_binding_info>> sets;

		{
			// --- Consolidate Bindings ---
			//
			// What do we want to achieve? We want to have placeholder bindings in
			// case bingings are not continous - a placeholder binding is a binding
			// that has a count of zero.

			for ( auto const& b : combined_bindings ) {

				while ( sets.size() <= b.setIndex ) {
					// we're going to need a new set
					sets.push_back( {} ); // push back empty vector
				}

				// --- there is a set available at b.setIndex.

				auto& current_set = sets.back();

				while ( current_set.size() <= b.binding ) {

					if ( current_set.size() == b.binding ) {
						// we can add the real thing
						current_set.push_back( b );
					} else {
						// we must add a placeholder binding
						le_shader_binding_info new_binding = {};

						new_binding.setIndex = sets.size() - 1;
						new_binding.binding  = current_set.size();
						new_binding.count    = 0; // setting count to zero signals to Vulkan that this is a placeholder binding.

						current_set.emplace_back( new_binding );
					}
				}
			}
		}

		info.set_layout_count = uint32_t( sets.size() );
		assert( sets.size() <= LE_MAX_BOUND_DESCRIPTOR_SETS ); // must be less or equal to maximum bound descriptor sets (currently 8 on NV)

		// deliberately commented out - this code is only here for additional error checking
		if ( false ) {
			// Assert that sets and bindings are not sparse (you must not have "holes" in sets, bindings.)
			// (check-shader-bindings) we might find a way to recover from this, but it might be difficult without a "linking" stage
			// which combines various shader stages.
			uint32 set_idx = 0;
			for ( auto const& s : sets ) {
				uint32_t binding = 0;
				for ( auto const& b : s ) {
					assert( b.binding == binding );
					assert( b.setIndex == set_idx );
					binding++;
				}
				set_idx++;
			}
		}

		for ( size_t i = 0; i != sets.size(); ++i ) {
			info.set_layout_keys[ i ] = le_pipeline_cache_produce_descriptor_set_layout( self, sets[ i ], vkLayouts + i );
		}
	}

	// -- Collect data over all shader stages: push_constant buffer size, active shader stages

	static_assert( sizeof( std::underlying_type<VkShaderStageFlagBits>::type ) == sizeof( uint32_t ), "ShaderStageFlagBits must be same size as uint32_t" );

	VkShaderStageFlags active_shader_stages      = 0;
	uint64_t           push_constant_buffer_size = 0;
	shader_modules_collect_info( self->shaderManager, shader_modules, shader_modules_count, &push_constant_buffer_size, &active_shader_stages );
	info.active_vk_shader_stages = uint32_t( active_shader_stages );
	info.push_constants_enabled  = ( push_constant_buffer_size > 0 ) ? 1 : 0;

	// -- Attempt to find this pipelineLayout from cache, if we can't find one, we create and retain it.
	info.pipeline_layout_key = shader_modules_get_pipeline_layout_hash( self->shaderManager, shader_modules, shader_modules_count );

	auto found_pl = self->pipelineLayouts.try_find( info.pipeline_layout_key );

	if ( nullptr == found_pl ) {

		VkPushConstantRange push_constant_range = {
		    .stageFlags = active_shader_stages,
		    .offset     = 0,
		    .size       = uint32_t( push_constant_buffer_size ),
		};

		VkPipelineLayoutCreateInfo layoutCreateInfo = {
		    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		    .pNext                  = nullptr,                           // optional
		    .flags                  = 0,                                 // optional
		    .setLayoutCount         = uint32_t( info.set_layout_count ), // optional
		    .pSetLayouts            = vkLayouts,
		    .pushConstantRangeCount = uint32_t( push_constant_buffer_size ? 1 : 0 ), // optional
		    .pPushConstantRanges    = push_constant_buffer_size ? &push_constant_range : nullptr,
		};

		// Create vkPipelineLayout
		VkPipelineLayout pipelineLayout = nullptr;
		vkCreatePipelineLayout( self->device, &layoutCreateInfo, nullptr, &pipelineLayout );

		// Attempt to store pipeline layout in cache
		bool result = self->pipelineLayouts.try_insert( info.pipeline_layout_key, &pipelineLayout );

		if ( false == result ) {
			// If we couldn't store the pipeline layout in cache, we must manually
			// dispose of be vulkan object, otherwise the cache will take care of cleanup.
			vkDestroyPipelineLayout( self->device, pipelineLayout, nullptr );
		}
	}

	return info;
}

// ----------------------------------------------------------------------
// HOT path - this gets executed every frame
static inline void le_pipeline_manager_produce_pipeline_layout_info(
    le_pipeline_manager_o*         self,
    le_shader_module_handle const* shader_modules,
    size_t                         shader_modules_count,
    le_pipeline_layout_info*       pipeline_layout_info,
    uint64_t*                      pipeline_layout_hash ) {

	*pipeline_layout_hash = shader_modules_get_pipeline_layout_hash( self->shaderManager, shader_modules, shader_modules_count );

	auto pl = self->pipelineLayoutInfos.try_find( *pipeline_layout_hash );

	if ( pl ) {
		*pipeline_layout_info = *pl;
	} else {
		// this will also create vulkan objects for pipeline layout / descriptor set layout and cache them
		*pipeline_layout_info = le_pipeline_manager_produce_pipeline_layout_info( self, shader_modules, shader_modules_count );
		// store in cache
		bool result = self->pipelineLayoutInfos.try_insert( *pipeline_layout_hash, pipeline_layout_info );
		assert( result && "pipeline layout info insertion must succeed" );
	}
}

// ----------------------------------------------------------------------

/// \brief Creates - or loads a pipeline from cache - based on current pipeline state
/// \note This method may lock the gpso/cpso cache and is therefore costly.
//
// + Only the 'command buffer recording'-slice of a frame shall be able to modify the cache.
//   The cache must be exclusively accessed through this method
//
// + NOTE: Access to this method must be sequential - no two frames may access this method
//   at the same time - and no two renderpasses may access this method at the same time.
static le_pipeline_and_layout_info_t le_pipeline_manager_produce_graphics_pipeline(
    le_pipeline_manager_o*   self,
    le_gpso_handle           gpso_handle,
    const BackendRenderPass& pass, uint32_t subpass ) {

	// TODO: Do we need this lock, or are the try_finds with their internal mutexes enough?
	auto lock = std::unique_lock( self->mtx ); // Enforce sequentiality via scoped lock: no two renderpasses may access cache concurrently.

	// TODO: Check whether the current gpso is dirty - if not, we should be able to use a cached version
	// via self.pipelines

	// What would taint the gpso: if any of the modules a gpso depends upon had changed, that would mean it
	// was tainted. We could keep an internal table of modules -> gpso and taint any gpso that made use of
	// a changed module.

	static auto                   logger                   = LeLog( LOGGER_LABEL );
	le_pipeline_and_layout_info_t pipeline_and_layout_info = {};

	// -- 0. Fetch pso from cache using its hash key
	graphics_pipeline_state_o const* pso = self->graphicsPso.try_find( gpso_handle );
	assert( pso );

	// -- 1. get pipeline layout info for a pipeline with these bindings
	// we try to fetch it from the cache first, if it doesn't exist, we must create it, and add it to the cache.

	uint64_t pipeline_layout_hash{};
	le_pipeline_manager_produce_pipeline_layout_info( self, pso->shaderModules.data(), pso->shaderModules.size(),
	                                                  &pipeline_and_layout_info.layout_info, &pipeline_layout_hash );

	// -- 2. get vk pipeline object
	// we try to fetch it from the cache first, if it doesn't exist, we must create it, and add it to the cache.

	uint64_t pipeline_hash = 0;
	{
		// Create a combined hash for pipeline, renderpass, and all contributing shader stages.

		uint64_t pso_renderpass_hash_data[ 12 ]       = {}; // we use a c-style array, with an entry count so that this is reliably allocated on the stack and not on the heap.
		uint64_t pso_renderpass_hash_data_num_entries = 0;  // number of entries in pso_renderpass_hash_data

		pso_renderpass_hash_data[ 0 ]        = reinterpret_cast<uint64_t>( gpso_handle ); // Hash associated with `pso`
		pso_renderpass_hash_data[ 1 ]        = pass.renderpassHash;                       // Hash for *compatible* renderpass
		pso_renderpass_hash_data_num_entries = 2;

		for ( auto const& s : pso->shaderModules ) {
			auto p_module = self->shaderManager->shaderModules.try_find( s );
			assert( p_module && "shader module not found" );
			pso_renderpass_hash_data[ pso_renderpass_hash_data_num_entries++ ] = p_module->hash; // Module state - may have been recompiled, hash must be current
		}

		// -- create combined hash for pipeline, renderpass
		pipeline_hash = SpookyHash::Hash64( pso_renderpass_hash_data, sizeof( uint64_t ) * pso_renderpass_hash_data_num_entries, pipeline_layout_hash );
	}

	// -- look up if pipeline with this hash already exists in cache
	auto p = self->pipelines.try_find( pipeline_hash );

	if ( p ) {
		// pipeline exists
		pipeline_and_layout_info.pipeline = *p;
	} else {
		// -- if not, create pipeline in pipeline cache and store / retain it
		pipeline_and_layout_info.pipeline = le_pipeline_cache_create_graphics_pipeline( self, pso, pass, subpass );
		logger.info( "New VK Graphics Pipeline created: %p", pipeline_hash );
		bool result = self->pipelines.try_insert( pipeline_hash, &pipeline_and_layout_info.pipeline );
		assert( result && " pipeline insertion must be successful " );
	}

	return pipeline_and_layout_info;
}

/// \brief Creates - or loads a pipeline from cache - based on current pipeline state
/// \note This method may lock the pso cache and is therefore costly.
//
// + Only the 'command buffer recording'-slice of a frame shall be able to modify the cache.
//   The cache must be exclusively accessed through this method
//
// + NOTE: Access to this method must be sequential - no two frames may access this method
//   at the same time - and no two renderpasses may access this method at the same time.
static le_pipeline_and_layout_info_t le_pipeline_manager_produce_rtx_pipeline( le_pipeline_manager_o* self, le_rtxpso_handle pso_handle, char** maybe_shader_group_data ) {
	le_pipeline_and_layout_info_t pipeline_and_layout_info = {};

	static auto logger = LeLog( LOGGER_LABEL );
	// -- 0. Fetch pso from cache using its hash key
	rtx_pipeline_state_o const* pso = self->rtxPso.try_find( pso_handle );
	assert( pso );

	// -- 1. get pipeline layout info for a pipeline with these bindings
	// we try to fetch it from the cache first, if it doesn't exist, we must create it, and add it to the cache.

	uint64_t pipeline_layout_hash{};
	le_pipeline_manager_produce_pipeline_layout_info( self, pso->shaderStages.data(), pso->shaderStages.size(),
	                                                  &pipeline_and_layout_info.layout_info, &pipeline_layout_hash );

	// -- 2. get vk pipeline object
	// we try to fetch it from the cache first, if it doesn't exist, we must create it, and add it to the cache.

	uint64_t pipeline_hash = 0;
	{
		// Create a hash over shader group data

		std::vector<uint64_t> pso_hash_data;
		pso_hash_data.reserve( 64 );

		pso_hash_data.emplace_back( reinterpret_cast<uint64_t>( pso_handle ) ); // Hash associated with `pso`

		for ( auto const& shader_stage : pso->shaderStages ) {
			auto s = self->shaderManager->shaderModules.try_find( shader_stage );
			assert( s && "could not find shader module" );
			pso_hash_data.emplace_back( s->hash ); // Module state - may have been recompiled, hash must be current
		}

		// -- create combined hash for pipeline, renderpass
		pipeline_hash = SpookyHash::Hash64( pso_hash_data.data(), pso_hash_data.size() * sizeof( uint64_t ), pipeline_layout_hash );

		// -- mix in hash over shader groups associated with the pso

		pipeline_hash = SpookyHash::Hash64( pso->shaderGroups.data(), pso->shaderGroups.size() * sizeof( le_rtx_shader_group_info ), pipeline_hash );
	}

	// -- look up if pipeline with this hash already exists in cache
	auto p = self->pipelines.try_find( pipeline_hash );

	if ( p ) {
		// -- Pipeline was found: return pipeline found in hash map
		pipeline_and_layout_info.pipeline = *p;
	} else {
		// -- Pipeline not found: Create pipeline in pipeline cache and store / retain it
		pipeline_and_layout_info.pipeline = le_pipeline_cache_create_rtx_pipeline( self, pso );

		logger.info( "New VK RTX Graphics Pipeline created: %p", pipeline_hash );

		// Store pipeline in pipeline cache
		bool result = self->pipelines.try_insert( pipeline_hash, &pipeline_and_layout_info.pipeline );

		assert( result && "pipeline insertion must be successful" );
	}

	if ( maybe_shader_group_data ) {

		// -- shader group data was requested

		auto g = self->rtx_shader_group_data.try_find( pipeline_hash );

		if ( g ) {
			*maybe_shader_group_data = *g;
		} else {
			// If shader group data was not found, we must must query, and store it.
			static VkPhysicalDeviceRayTracingPipelinePropertiesKHR props;

			le_backend_vk::vk_device_i.get_vk_physical_device_ray_tracing_properties( self->le_device, &props );

			size_t dataSize   = props.shaderGroupHandleSize * pso->shaderGroups.size();
			size_t bufferSize = dataSize + sizeof( LeShaderGroupDataHeader );

			// Allocate buffer to store handles
			char* handles = static_cast<char*>( malloc( bufferSize ) );

			// The buffer used to store handles contains a header,
			// First element is header size,

			LeShaderGroupDataHeader header;
			header.pipeline_obj                    = pipeline_and_layout_info.pipeline;
			header.data_byte_count                 = uint32_t( dataSize );
			header.rtx_shader_group_handle_size    = props.shaderGroupHandleSize;
			header.rtx_shader_group_base_alignment = props.shaderGroupBaseAlignment;

			memcpy( handles, &header, sizeof( LeShaderGroupDataHeader ) );

			{
				// Retrieve shader group handles from GPU

				bool result = vkGetRayTracingShaderGroupHandlesKHR(
				    self->device,
				    pipeline_and_layout_info.pipeline,
				    0, uint32_t( pso->shaderGroups.size() ),
				    dataSize, handles + sizeof( LeShaderGroupDataHeader ) );

				assert( result == VK_SUCCESS );
			}
			self->rtx_shader_group_data.try_insert( pipeline_hash, &handles );

			// we need to store this buffer with the pipeline - or at least associate is to the pso

			logger.info( "Queried rtx shader group handles:" );
			size_t n_el = props.shaderGroupHandleSize / sizeof( uint32_t );

			uint32_t* debug_handles = reinterpret_cast<uint32_t*>( handles + sizeof( LeShaderGroupDataHeader ) );
			for ( size_t i = 0; i != pso->shaderGroups.size(); i++ ) {
				std::ostringstream os;
				for ( size_t j = 0; j != n_el; j++ ) {
					os << std::dec << *debug_handles++ << ", ";
				}
				logger.info( os.str().c_str() );
			}

			*maybe_shader_group_data = handles;
		}
	}
	return pipeline_and_layout_info;
}

// ----------------------------------------------------------------------

static le_pipeline_and_layout_info_t le_pipeline_manager_produce_compute_pipeline( le_pipeline_manager_o* self, le_cpso_handle cpso_handle ) {

	static auto                     logger = LeLog( LOGGER_LABEL );
	compute_pipeline_state_o const* pso    = self->computePso.try_find( cpso_handle );
	assert( pso );

	le_pipeline_and_layout_info_t pipeline_and_layout_info = {};
	uint64_t                      pipeline_layout_hash{};

	le_pipeline_manager_produce_pipeline_layout_info( self, &pso->shaderStage, 1, &pipeline_and_layout_info.layout_info, &pipeline_layout_hash );

	// -- Get vk pipeline object
	// we try to fetch it from the cache first, if it doesn't exist, we must create it, and add it to the cache.

	uint64_t pipeline_hash = 0;
	{
		// Create a combined hash for pipeline, renderpass, and all contributing shader stages.

		// We use a fixed-size c-style array to collect all hashes for this pipeline,
		// and an entry count so that this is reliably allocated on the stack and not on the heap.
		//
		uint64_t hash_data[ 2 ] = {};
		uint64_t num_entries    = 0; // max 2

		hash_data[ num_entries++ ] = reinterpret_cast<uint64_t>( cpso_handle );                          // Hash associated with `pso`
		hash_data[ num_entries++ ] = le_shader_module_get_hash( self->shaderManager, pso->shaderStage ); // Module state - may have been recompiled, hash must be current

		// -- create combined hash for pipeline, and shader stage
		pipeline_hash = SpookyHash::Hash64( hash_data, sizeof( uint64_t ) * num_entries, pipeline_layout_hash );
	}

	// -- look up if pipeline with this hash already exists in cache.
	auto p = self->pipelines.try_find( pipeline_hash );

	if ( p ) {
		// -- if yes, return pipeline found in hash map
		pipeline_and_layout_info.pipeline = *p;
	} else {
		// -- if not, create pipeline in pipeline cache and store / retain it
		pipeline_and_layout_info.pipeline = le_pipeline_cache_create_compute_pipeline( self, pso );
		logger.info( "New VK Compute Pipeline created: %p", pipeline_hash );
		bool result = self->pipelines.try_insert( pipeline_hash, &pipeline_and_layout_info.pipeline );
		assert( result && "insertion must be successful" );
	}

	return pipeline_and_layout_info;
}

// ----------------------------------------------------------------------
// This method may get called through the pipeline builder -
// via RECORD in command buffer recording state
// in SETUP
bool le_pipeline_manager_introduce_graphics_pipeline_state( le_pipeline_manager_o* self, graphics_pipeline_state_o* pso, le_gpso_handle* handle ) {

	constexpr size_t hash_msg_size = sizeof( le_graphics_pipeline_builder_data );
	uint64_t         hash_value    = SpookyHash::Hash64( &pso->data, hash_msg_size, 0 );
	// Calculate a meta-hash over shader stage hash entries so that we can
	// detect if a shader component has changed
	//
	// Rather than a std::vector, we use a plain-c array to collect hash entries
	// for all stages, because we don't want to allocate anything on the heap,
	// and local fixed-size c-arrays are cheap.

	constexpr size_t maxShaderStages = 8;                 // we assume a maximum number of shader entries
	uint64_t         stageHashEntries[ maxShaderStages ]; // array of stage hashes for further hashing
	uint64_t         stageHashEntriesUsed = 0;            // number of used shader stage hash entries

	for ( auto const& module_handle : pso->shaderModules ) {
		auto p_module = self->shaderManager->shaderModules.try_find( module_handle );
		assert( p_module && "shader module not found" );
		stageHashEntries[ stageHashEntriesUsed++ ] = p_module->hash;
		assert( stageHashEntriesUsed <= maxShaderStages ); // We're gonna need a bigger boat.
	}

	// Mix in the meta-hash over shader stages with the previous hash over pipeline state
	// which gives the complete hash representing a pipeline state object.

	hash_value = SpookyHash::Hash64( stageHashEntries, stageHashEntriesUsed * sizeof( uint64_t ), hash_value );

	// -- If pipeline has explicit attribute binding stages that must be factored in with the hash.

	static_assert( std::has_unique_object_representations_v<le_vertex_input_binding_description>,
	               "vertex input binding description must be tightly packed, so that it "
	               "may be hashed (any padding will invalidate hash)." );

	if ( !pso->explicitVertexInputBindingDescriptions.empty() ) {
		hash_value = SpookyHash::Hash64( pso->explicitVertexInputBindingDescriptions.data(),
		                                 pso->explicitVertexInputBindingDescriptions.size() * sizeof( le_vertex_input_binding_description ),
		                                 hash_value );

		hash_value = SpookyHash::Hash64( pso->explicitVertexAttributeDescriptions.data(),
		                                 pso->explicitVertexAttributeDescriptions.size() * sizeof( le_vertex_input_attribute_description ),
		                                 hash_value );
	}

	// Cast hash_value to a pipeline handle, so we can use the type system with it.
	// Its value, of course, is still equivalent to hash_value.

	*handle = reinterpret_cast<le_gpso_handle>( hash_value );

	// Add pipeline state object to the shared store
	return self->graphicsPso.try_insert( *handle, pso );
};

// ----------------------------------------------------------------------
// This method may get called through the pipeline builder -
// via RECORD in command buffer recording state
// in SETUP
bool le_pipeline_manager_introduce_compute_pipeline_state( le_pipeline_manager_o* self, compute_pipeline_state_o* pso, le_cpso_handle* handle ) {

	le_shader_module_o* shader_module = self->shaderManager->shaderModules.try_find( pso->shaderStage );
	assert( shader_module && "could not find shader module" );
	*handle = reinterpret_cast<le_cpso_handle&>( shader_module->hash );

	return self->computePso.try_insert( *handle, pso );
};

// ----------------------------------------------------------------------
// This method may get called through the pipeline builder -
// via RECORD in command buffer recording state
// in SETUP
bool le_pipeline_manager_introduce_rtx_pipeline_state( le_pipeline_manager_o* self, rtx_pipeline_state_o* pso, le_rtxpso_handle* handle ) {

	// Calculate hash over all pipeline stages,
	// and pipeline shader group infos

	uint64_t hash_value = 0;

	// calculate hash over all shader module hashes.

	std::vector<uint64_t> shader_module_hashes;

	shader_module_hashes.reserve( pso->shaderStages.size() );
	for ( auto const& shader_stage : pso->shaderStages ) {
		shader_module_hashes.emplace_back( le_shader_module_get_hash( self->shaderManager, shader_stage ) );
	}

	hash_value = SpookyHash::Hash64(
	    shader_module_hashes.data(),
	    shader_module_hashes.size() * sizeof( uint64_t ),
	    hash_value );

	static_assert( std::has_unique_object_representations_v<le_rtx_shader_group_info>,
	               "shader group create info must be tightly packed, so that it may be used"
	               "for hashing. Otherwise you would end up with noise between the fields"
	               "invalidating the hash." );

	if ( !pso->shaderGroups.empty() ) {
		hash_value = SpookyHash::Hash64(
		    pso->shaderGroups.data(),
		    sizeof( le_rtx_shader_group_info ) * pso->shaderGroups.size(),
		    hash_value );
	}

	*handle = reinterpret_cast<le_rtxpso_handle>( hash_value );
	return self->rtxPso.try_insert( *handle, pso );
};

// ----------------------------------------------------------------------

static VkPipelineLayout le_pipeline_manager_get_pipeline_layout_public( le_pipeline_manager_o* self, uint64_t key ) {
	VkPipelineLayout const* pLayout = self->pipelineLayouts.try_find( key );
	assert( pLayout && "layout cannot be nullptr" );
	return *pLayout;
}

// ----------------------------------------------------------------------

static const le_descriptor_set_layout_t* le_pipeline_manager_get_descriptor_set_layout( le_pipeline_manager_o* self, uint64_t setlayout_key ) {
	return self->descriptorSetLayouts.try_find( setlayout_key );
};

// ----------------------------------------------------------------------

static le_shader_module_handle le_pipeline_manager_create_shader_module(
    le_pipeline_manager_o*            self,
    char const*                       path,
    const LeShaderSourceLanguageEnum& shader_source_language,
    const le::ShaderStage&            moduleType,
    char const*                       macro_definitions,
    le_shader_module_handle           handle,
    VkSpecializationMapEntry const*   specialization_map_entries,
    uint32_t                          specialization_map_entries_count,
    void*                             specialization_map_data,
    uint32_t                          specialization_map_data_num_bytes ) {
	return le_shader_manager_create_shader_module(
	    self->shaderManager,
	    path,
	    shader_source_language,
	    moduleType,
	    macro_definitions,
	    handle,
	    specialization_map_entries,
	    specialization_map_entries_count,
	    specialization_map_data,
	    specialization_map_data_num_bytes );
}

// ----------------------------------------------------------------------

static void le_pipeline_manager_update_shader_modules( le_pipeline_manager_o* self ) {
	le_shader_manager_update_shader_modules( self->shaderManager );
}

// ----------------------------------------------------------------------

static le_pipeline_manager_o* le_pipeline_manager_create( le_backend_o* backend ) {
	auto self = new le_pipeline_manager_o();

	using namespace le_backend_vk;

	self->backend   = backend;
	self->le_device = private_backend_vk_i.get_le_device( self->backend );
	vk_device_i.increase_reference_count( self->le_device );
	self->device = vk_device_i.get_vk_device( self->le_device );

	VkPipelineCacheCreateInfo info = {
	    .sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
	    .pNext           = nullptr, // optional
	    .flags           = 0,       // optional
	    .initialDataSize = 0,       // optional
	    .pInitialData    = nullptr,
	};

	vkCreatePipelineCache( self->device, &info, nullptr, &self->vulkanCache );
	self->shaderManager = le_shader_manager_create( self->device );

	return self;
}

// ----------------------------------------------------------------------

static void le_pipeline_manager_destroy( le_pipeline_manager_o* self ) {

	static auto logger = LeLog( LOGGER_LABEL );

	le_shader_manager_destroy( self->shaderManager );
	self->shaderManager = nullptr;

	// -- destroy any objects which were allocated via Vulkan API - these
	// need to be destroyed using the device they were allocated from.

	// -- destroy descriptorSetLayouts, and descriptorUpdateTemplates
	self->descriptorSetLayouts.iterator(
	    []( le_descriptor_set_layout_t* e, void* user_data ) {
		    VkDevice device = *static_cast<VkDevice*>( user_data );
		    for ( auto& s : e->immutable_samplers ) {
			    vkDestroySampler( device, *s, nullptr );
			    delete s;
		    }
		    if ( e->vk_descriptor_set_layout ) {
			    vkDestroyDescriptorSetLayout( device, e->vk_descriptor_set_layout, nullptr );
			    logger.info( "Destroyed VkDescriptorSetLayout: %p", e->vk_descriptor_set_layout );
		    }
		    if ( e->vk_descriptor_update_template ) {
			    vkDestroyDescriptorUpdateTemplate( device, e->vk_descriptor_update_template, nullptr );
			    logger.info( "Destroyed VkDescriptorUpdateTemplate: %p", e->vk_descriptor_update_template );
		    }
	    },
	    &self->device );

	// -- destroy pipelineLayouts
	self->pipelineLayouts.iterator(
	    []( VkPipelineLayout* e, void* user_data ) {
		    auto device = *static_cast<VkDevice*>( user_data );
		    vkDestroyPipelineLayout( device, *e, nullptr );
		    logger.info( "Destroyed VkPipelineLayout: %p", *e );
	    },
	    &self->device );

	// Clear pipelines before we destroy pipeline cache object.
	// we must first iterate over all pipeline objects to delete any pipelines

	self->pipelines.iterator(
	    []( VkPipeline* p, void* user_data ) {
		    auto device = *static_cast<VkDevice*>( user_data );
		    vkDestroyPipeline( device, *p, nullptr );
		    logger.info( "Destroyed VkPipeline: %p", *p );
	    },
	    &self->device );

	self->pipelines.clear();

	self->rtx_shader_group_data.iterator(
	    []( char** p_buffer, void* ) {
		    free( *p_buffer );
	    },
	    nullptr );

	// Destroy Pipeline Cache

	if ( self->vulkanCache ) {
		vkDestroyPipelineCache( self->device, self->vulkanCache, nullptr );
	}

	le_backend_vk::vk_device_i.decrease_reference_count( self->le_device );
	self->le_device = nullptr;

	delete self;
}

// ----------------------------------------------------------------------

void register_le_pipeline_vk_api( void* api_ ) {

	auto le_backend_vk_api_i = static_cast<le_backend_vk_api*>( api_ );
	{
		auto& i = le_backend_vk_api_i->le_pipeline_manager_i;

		i.create  = le_pipeline_manager_create;
		i.destroy = le_pipeline_manager_destroy;

		i.create_shader_module              = le_pipeline_manager_create_shader_module;
		i.update_shader_modules             = le_pipeline_manager_update_shader_modules;
		i.introduce_graphics_pipeline_state = le_pipeline_manager_introduce_graphics_pipeline_state;
		i.introduce_compute_pipeline_state  = le_pipeline_manager_introduce_compute_pipeline_state;
		i.introduce_rtx_pipeline_state      = le_pipeline_manager_introduce_rtx_pipeline_state;
		i.get_pipeline_layout               = le_pipeline_manager_get_pipeline_layout_public;
		i.get_descriptor_set_layout         = le_pipeline_manager_get_descriptor_set_layout;
		i.produce_graphics_pipeline         = le_pipeline_manager_produce_graphics_pipeline;
		i.produce_rtx_pipeline              = le_pipeline_manager_produce_rtx_pipeline;
		i.produce_compute_pipeline          = le_pipeline_manager_produce_compute_pipeline;
	}
	{
		auto& i = le_backend_vk_api_i->le_shader_module_i;
		//		i.get_hash  = le_shader_module_get_hash;
		i.get_stage = le_shader_module_get_stage;
	}
	{
		// Store callback address with api so that callback gets automatically forwarded to the correct
		// address when backend reloads : see le_core.h / callback forwarding
		auto& i            = le_backend_vk_api_i->private_shader_file_watcher_i;
		i.on_callback_addr = ( void* )le_shader_file_watcher_on_callback;
	}
}
