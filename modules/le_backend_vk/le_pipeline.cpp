#include "le_backend_vk.h"
#include "le_backend_types_internal.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <set>
#include <unordered_map>

#include <filesystem> // for parsing shader source file paths
#include <fstream>    // for reading shader source files
#include <cstring>    // for memcpy
#include <mutex>
#include <shared_mutex>
#include <atomic>

#include "le_shader_compiler.h"

#include "util/spirv_reflect/spirv_reflect.h"

#include "le_file_watcher.h" // for watching shader source files
#include "le_log.h"
#include "3rdparty/src/spooky/SpookyV2.h" // for hashing renderpass gestalt, so that we can test for *compatible* renderpasses

static constexpr auto LOGGER_LABEL = "le_pipeline";

struct specialization_map_info_t {
	std::vector<vk::SpecializationMapEntry> entries;
	std::vector<char>                       data;
};

struct le_shader_module_o {
	uint64_t                                         hash                = 0;     ///< hash taken from spirv code + hash_shader_defines
	uint64_t                                         hash_shader_defines = 0;     ///< hash taken from shader defines string
	uint64_t                                         hash_pipelinelayout = 0;     ///< hash taken from descriptors over all sets
	std::string                                      macro_defines       = "";    ///< #defines to pass to shader compiler
	std::vector<le_shader_binding_info>              bindings;                    ///< info for each binding, sorted asc.
	std::vector<uint32_t>                            spirv    = {};               ///< spirv source code for this module
	std::filesystem::path                            filepath = {};               ///< path to source file
	std::vector<std::string>                         vertexAttributeNames;        ///< (used for debug only) name for vertex attribute
	std::vector<vk::VertexInputAttributeDescription> vertexAttributeDescriptions; ///< descriptions gathered from reflection if shader type is vertex
	std::vector<vk::VertexInputBindingDescription>   vertexBindingDescriptions;   ///< descriptions gathered from reflection if shader type is vertex
	VkShaderModule                                   module                    = nullptr;
	le::ShaderStage                                  stage                     = {};
	uint64_t                                         push_constant_buffer_size = 0; ///< number of bytes for push constant buffer, zero indicates no push constant buffer in use.
	le::ShaderSourceLanguage                         source_language           = le::ShaderSourceLanguage::eDefault;
	specialization_map_info_t                        specialization_map_info; ///< information concerning specialization constants for this shader stage
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
	std::vector<U *>  objects; // owning, object is copied on add_entry

  public:
	// Insert a new obj into table, object is copied.
	// return true if successful, false if entry aready existed.
	// in case return value is false, object was not copied.
	bool try_insert( T const &handle, U *obj ) {
		mtx.lock();
		size_t i = 0;
		for ( auto const &h : handles ) {
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
	U *const try_find( T const &needle ) {
		mtx.lock_shared();
		U *const *obj = objects.data();
		for ( auto const &h : handles ) {
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

	typedef void ( *iterator_fun )( U *e, void *user_data );

	// do something on all objects
	void iterator( iterator_fun fun, void *user_data ) {
		mtx.lock();
		for ( auto &e : objects ) {
			fun( e, user_data );
		}
		mtx.unlock();
	}

	void clear() {
		mtx.lock();
		for ( auto &obj : objects ) {
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

	std::shared_mutex          mtx;
	std::unordered_map<S, T *> store; // owning, object is copied on successful try_insert

  public:
	T *try_find( S needle ) {
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
	bool try_insert( S handle, T *obj ) {

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

	typedef void ( *iterator_fun )( T *e, void *user_data );

	// do something on all objects
	void iterator( iterator_fun fun, void *user_data ) {
		mtx.lock();
		for ( auto &e : store ) {
			fun( e.second, user_data );
		}
		mtx.unlock();
	}

	void clear() {
		mtx.lock();
		for ( auto &e : store ) {
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
};

struct le_shader_manager_o {
	vk::Device device = nullptr;

	HashMap<le_shader_module_handle, le_shader_module_o> shaderModules; // OWNING. Stores all shader modules used in backend, indexed via shader_module_handle

	ProtectedModuleDependencies protected_module_dependencies; // must lock mutex before using.

	std::set<le_shader_module_handle> modifiedShaderModules; // non-owning pointers to shader modules which need recompiling (used by file watcher)

	le_shader_compiler_o *shader_compiler   = nullptr; // owning
	le_file_watcher_o *   shaderFileWatcher = nullptr; // owning
	std::atomic<uint64_t> modules_count     = { 0 };   // zero as handle means unset (first available handle will be 1, as pre-increment)
};

// NOTE: It might make sense to have one pipeline manager per worker thread, and
//       to consolidate after the frame has been processed.
struct le_pipeline_manager_o {
	le_device_o *le_device = nullptr; // arc-owning, increases reference count, decreases on destruction
	vk::Device   device    = nullptr;

	std::mutex mtx;

	vk::PipelineCache vulkanCache = nullptr;

	le_shader_manager_o *shaderManager = nullptr; // owning: does it make sense to have a shader manager additionally to the pipeline manager?

	HashTable<le_gpso_handle, graphics_pipeline_state_o> graphicsPso;
	HashTable<le_cpso_handle, compute_pipeline_state_o>  computePso;
	HashTable<le_rtxpso_handle, rtx_pipeline_state_o>    rtxPso;

	HashMap<uint64_t, VkPipeline>              pipelines;             // indexed by pipeline_hash
	HashTable<uint64_t, char *>                rtx_shader_group_data; // indexed by pipeline_hash
	HashMap<uint64_t, le_pipeline_layout_info> pipelineLayoutInfos;

	HashMap<uint64_t, le_descriptor_set_layout_t> descriptorSetLayouts;
	HashMap<uint64_t, vk::PipelineLayout>         pipelineLayouts; // indexed by hash of array of descriptorSetLayoutCache keys per pipeline layout
};

static vk::Format vk_format_from_spv_reflect_format( SpvReflectFormat const &format ) {
	// clang-format off
	switch (format)
	{
		case SPV_REFLECT_FORMAT_UNDEFINED           : return  vk::Format(VK_FORMAT_UNDEFINED);
		case SPV_REFLECT_FORMAT_R32_UINT            : return  vk::Format(VK_FORMAT_R32_UINT);
		case SPV_REFLECT_FORMAT_R32_SINT            : return  vk::Format(VK_FORMAT_R32_SINT);
		case SPV_REFLECT_FORMAT_R32_SFLOAT          : return  vk::Format(VK_FORMAT_R32_SFLOAT);
		case SPV_REFLECT_FORMAT_R32G32_UINT         : return  vk::Format(VK_FORMAT_R32G32_UINT);
		case SPV_REFLECT_FORMAT_R32G32_SINT         : return  vk::Format(VK_FORMAT_R32G32_SINT);
		case SPV_REFLECT_FORMAT_R32G32_SFLOAT       : return  vk::Format(VK_FORMAT_R32G32_SFLOAT);
		case SPV_REFLECT_FORMAT_R32G32B32_UINT      : return  vk::Format(VK_FORMAT_R32G32B32_UINT);
		case SPV_REFLECT_FORMAT_R32G32B32_SINT      : return  vk::Format(VK_FORMAT_R32G32B32_SINT);
		case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT    : return  vk::Format(VK_FORMAT_R32G32B32_SFLOAT);
		case SPV_REFLECT_FORMAT_R32G32B32A32_UINT   : return  vk::Format(VK_FORMAT_R32G32B32A32_UINT);
		case SPV_REFLECT_FORMAT_R32G32B32A32_SINT   : return  vk::Format(VK_FORMAT_R32G32B32A32_SINT);
		case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT : return  vk::Format(VK_FORMAT_R32G32B32A32_SFLOAT);
		case SPV_REFLECT_FORMAT_R64_UINT            : return  vk::Format(VK_FORMAT_R64_UINT);
		case SPV_REFLECT_FORMAT_R64_SINT            : return  vk::Format(VK_FORMAT_R64_SINT);
		case SPV_REFLECT_FORMAT_R64_SFLOAT          : return  vk::Format(VK_FORMAT_R64_SFLOAT);
		case SPV_REFLECT_FORMAT_R64G64_UINT         : return  vk::Format(VK_FORMAT_R64G64_UINT);
		case SPV_REFLECT_FORMAT_R64G64_SINT         : return  vk::Format(VK_FORMAT_R64G64_SINT);
		case SPV_REFLECT_FORMAT_R64G64_SFLOAT       : return  vk::Format(VK_FORMAT_R64G64_SFLOAT);
		case SPV_REFLECT_FORMAT_R64G64B64_UINT      : return  vk::Format(VK_FORMAT_R64G64B64_UINT);
		case SPV_REFLECT_FORMAT_R64G64B64_SINT      : return  vk::Format(VK_FORMAT_R64G64B64_SINT);
		case SPV_REFLECT_FORMAT_R64G64B64_SFLOAT    : return  vk::Format(VK_FORMAT_R64G64B64_SFLOAT);
		case SPV_REFLECT_FORMAT_R64G64B64A64_UINT   : return  vk::Format(VK_FORMAT_R64G64B64A64_UINT);
		case SPV_REFLECT_FORMAT_R64G64B64A64_SINT   : return  vk::Format(VK_FORMAT_R64G64B64A64_SINT);
		case SPV_REFLECT_FORMAT_R64G64B64A64_SFLOAT : return  vk::Format(VK_FORMAT_R64G64B64A64_SFLOAT);
		default	                                    : assert(false); return vk::Format();
	} // clang-format on
}

static uint32_t byte_stride_from_spv_type_description( SpvReflectNumericTraits const &traits ) {

	uint32_t unit_size = traits.scalar.width / 8;

	assert( unit_size != 0 );

	uint32_t result = unit_size;
	result          = std::max<uint32_t>( result, unit_size * traits.vector.component_count );
	result          = std::max<uint32_t>( result, unit_size * traits.matrix.column_count * traits.matrix.row_count );
	result          = std::max<uint32_t>( result, traits.matrix.stride );

	return result;
}

static vk::DescriptorType descriptor_type_from_spv_descriptor_type( SpvReflectDescriptorType const &spv_descriptor_type ) {
	// clang-format off
	switch(spv_descriptor_type)
	{
		case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER                    : return vk::DescriptorType( VK_DESCRIPTOR_TYPE_SAMPLER);
		case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER     : return vk::DescriptorType( VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE              : return vk::DescriptorType( VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE              : return vk::DescriptorType( VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER       : return vk::DescriptorType( VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER       : return vk::DescriptorType( VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
		case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER             : // Deliberate fall-through: we make all uniform buffers dynamic.
		case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC     : return vk::DescriptorType( VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER             : // Deliberate fall-through: we make storage buffers dynamic.
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC     : return vk::DescriptorType( VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
		case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT           : return vk::DescriptorType( VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
		case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR : return vk::DescriptorType( VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
        default: assert(false); return vk::DescriptorType();
	}
	// clang-format on
}

// ----------------------------------------------------------------------

static inline vk::VertexInputRate vk_input_rate_from_le_input_rate( const le_vertex_input_rate &input_rate ) {
	switch ( input_rate ) {
	case ( le_vertex_input_rate::ePerInstance ):
		return vk::VertexInputRate::eInstance;
	case ( le_vertex_input_rate::ePerVertex ):
		return vk::VertexInputRate::eVertex;
	}
	assert( false ); // something's wrong: control should never come here, switch needs to cover all cases.
	return vk::VertexInputRate::eVertex;
}

// ----------------------------------------------------------------------

// clang-format off
/// \returns corresponding vk::Format for a given le_input_attribute_description struct
static inline vk::Format vk_format_from_le_vertex_input_attribute_description( le_vertex_input_attribute_description const & d){

	if ( d.vecsize == 0 || d.vecsize > 4 ){
		assert(false); // vecsize must be between 1 and 4
		return vk::Format::eUndefined;
	}

	switch ( d.type ) {
	case le_num_type::eFloat:
		switch ( d.vecsize ) {
		case 4: return vk::Format::eR32G32B32A32Sfloat;
		case 3: return vk::Format::eR32G32B32Sfloat;
		case 2: return vk::Format::eR32G32Sfloat;
		case 1: return vk::Format::eR32Sfloat;
		}
	    break;
	case le_num_type::eHalf:
		switch ( d.vecsize ) {
		case 4: return vk::Format::eR16G16B16A16Sfloat;
		case 3: return vk::Format::eR16G16B16Sfloat;
		case 2: return vk::Format::eR16G16Sfloat;
		case 1: return vk::Format::eR16Sfloat;
		}
	    break;
	case le_num_type::eUShort: // fall through to eShort
	case le_num_type::eShort:
		if (d.isNormalised){
			switch ( d.vecsize ) {
			case 4: return vk::Format::eR16G16B16A16Unorm;
			case 3: return vk::Format::eR16G16B16Unorm;
			case 2: return vk::Format::eR16G16Unorm;
			case 1: return vk::Format::eR16Unorm;
			}
		}else{
			switch ( d.vecsize ) {
			case 4: return vk::Format::eR16G16B16A16Uint;
			case 3: return vk::Format::eR16G16B16Uint;
			case 2: return vk::Format::eR16G16Uint;
			case 1: return vk::Format::eR16Uint;
			}
		}
	    break;
	case le_num_type::eInt:
		switch ( d.vecsize ) {
		case 4: return vk::Format::eR32G32B32A32Sint;
		case 3: return vk::Format::eR32G32B32Sint;
		case 2: return vk::Format::eR32G32Sint;
		case 1: return vk::Format::eR32Sint;
		}
	    break;
	case le_num_type::eUInt:
		switch ( d.vecsize ) {
		case 4: return vk::Format::eR32G32B32A32Uint;
		case 3: return vk::Format::eR32G32B32Uint;
		case 2: return vk::Format::eR32G32Uint;
		case 1: return vk::Format::eR32Uint;
		}
	    break;
	case le_num_type::eULong:
		switch ( d.vecsize ) {
		case 4: return vk::Format::eR64G64B64A64Uint;
		case 3: return vk::Format::eR64G64B64Uint;
		case 2: return vk::Format::eR64G64Uint;
		case 1: return vk::Format::eR64Uint;
		}
	    break;
	case le_num_type::eChar:  // fall through to uChar
	case le_num_type::eUChar:
		if (d.isNormalised){
			switch ( d.vecsize ) {
			case 4: return vk::Format::eR8G8B8A8Unorm;
			case 3: return vk::Format::eR8G8B8Unorm;
			case 2: return vk::Format::eR8G8Unorm;
			case 1: return vk::Format::eR8Unorm;
			}
		} else {
			switch ( d.vecsize ) {
			case 4: return vk::Format::eR8G8B8A8Uint;
			case 3: return vk::Format::eR8G8B8Uint;
			case 2: return vk::Format::eR8G8Uint;
			case 1: return vk::Format::eR8Uint;
			}
		}
	    break;
    default:
        assert(false);
	}

	assert(false); // abandon all hope
	return vk::Format::eUndefined;
}
// clang-format on

// Converts a le shader stage enum to a vulkan shader stage flag bit
// Currently these are kept in sync which means conversion is a simple
// matter of initialising one from the other.
static inline vk::ShaderStageFlagBits le_to_vk( const le::ShaderStage &stage ) {
	return vk::ShaderStageFlagBits( stage );
}

// ----------------------------------------------------------------------
/// \brief   file loader utility method
/// \details loads file given by filepath and returns a vector of chars if successful
/// \note    returns an empty vector if not successful
static std::vector<char> load_file( const std::filesystem::path &file_path, bool *success ) {

	static auto       logger = LeLog( LOGGER_LABEL );
	std::vector<char> contents;

	size_t        fileSize = 0;
	std::ifstream file( file_path, std::ios::in | std::ios::binary | std::ios::ate );

	if ( !file.is_open() ) {
		logger.error( "Unable to open file: '%s'", file_path.c_str() );
		*success = false;
		return contents;
	}

	logger.debug( "Opened file : '%s'", std::filesystem::canonical( ( file_path ) ).c_str() );

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
// Returns the hash for a given shaderModule
static inline uint64_t le_shader_module_get_hash( le_shader_manager_o *manager, le_shader_module_handle handle ) {
	auto module = manager->shaderModules.try_find( handle );
	assert( module != nullptr );
	return module->hash;
}

// Returns the stage for a given shader module
static LeShaderStageEnum le_shader_module_get_stage( le_pipeline_manager_o *manager, le_shader_module_handle handle ) {
	auto module = manager->shaderManager->shaderModules.try_find( handle );
	assert( module != nullptr );
	return { module->stage };
}

// ----------------------------------------------------------------------

static bool check_is_data_spirv( const void *raw_data, size_t data_size ) {

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
    le_shader_compiler_o *     shader_compiler,
    void *                     raw_data,
    size_t                     numBytes,
    LeShaderSourceLanguageEnum shader_source_language,
    LeShaderStageEnum          moduleType,
    const char *               original_file_name,
    std::vector<uint32_t> &    spirvCode,
    std::set<std::string> &    includesSet,
    std::string const &        shaderDefines ) {

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
		    static_cast<const char *>( raw_data ), numBytes,
		    shader_source_language,
		    moduleType,
		    original_file_name,
		    shaderDefines.c_str(),
		    shaderDefines.size(),
		    compilation_result );

		if ( compiler_i.result_get_success( compilation_result ) == true ) {
			const char *addr;
			size_t      res_sz;
			compiler_i.result_get_bytes( compilation_result, &addr, &res_sz );
			spirvCode.resize( res_sz / 4 );
			memcpy( spirvCode.data(), addr, res_sz );

			// -- grab a list of includes which this compilation unit depends on:
			const char *pStr  = nullptr;
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
static void le_pipeline_cache_flag_affected_modules_for_source_path( le_shader_manager_o *self, const char *shader_source_file_path ) {
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

	auto const &moduleDependencies = self->protected_module_dependencies.moduleDependencies[ shader_source_file_path ];

	// -- add all affected modules to the set of modules which depend on this shader source file.

	for ( auto const &m : moduleDependencies ) {
		self->modifiedShaderModules.insert( m );
	}
};

// ----------------------------------------------------------------------
static void le_shader_file_watcher_on_callback( const char *path, void *user_data ) {
	auto shader_manager = static_cast<le_shader_manager_o *>( user_data );
	// call a method on backend to tell it that the file path has changed.
	// backend to figure out which modules are affected.
	static auto logger = LeLog( LOGGER_LABEL );
	logger.debug( "Source file update detected: '%s'", path );
	le_pipeline_cache_flag_affected_modules_for_source_path( shader_manager, path );
}
// ----------------------------------------------------------------------

// Thread-safety: needs exclusive access to shader_manager->moduleDependencies for full duration
// We use a lock for this reason.
static void le_pipeline_cache_set_module_dependencies_for_watched_file( le_shader_manager_o *self, le_shader_module_handle module, std::set<std::string> &sourcePaths ) {

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

	for ( const auto &s : sourcePaths ) {

		// If no previous entry for this source path existed, we must insert a watch for this path
		// the watch will call a backend method which figures out how many modules were affected.
		if ( 0 == self->protected_module_dependencies.moduleDependencies.count( s ) ) {

			// this is the first time this file appears on our radar. Let's create a file watcher for it.

			le_file_watcher_watch_settings settings;
			settings.filePath                                       = s.c_str();
			settings.callback_user_data                             = self;
			settings.callback_fun                                   = ( void ( * )( char const *, void * ) )( le_core_forward_callback( le_backend_vk_api_i->private_shader_file_watcher_i.on_callback_addr ) );
			auto watch_id                                           = le_file_watcher::le_file_watcher_i.add_watch( self->shaderFileWatcher, &settings );
			self->protected_module_dependencies.moduleWatchIds[ s ] = watch_id;
		}

		logger.debug( "\t + '%s'", std::filesystem::relative( s ).c_str() );

		self->protected_module_dependencies.moduleDependencies[ s ].insert( module );
	}
}

// Thread-safety: needs exclusive access to shader_manager->moduleDependencies for full duration.
// We use a lock for this reason.
static void le_pipeline_cache_remove_module_from_dependencies( le_shader_manager_o *self, le_shader_module_handle module ) {
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
static uint64_t shader_modules_get_pipeline_layout_hash( le_shader_manager_o *shader_manager, le_shader_module_handle const *shader_modules, size_t numModules ) {

	assert( numModules <= 16 ); // note max 16 shader modules.

	// We use a stack-allocated c-array instead of vector so that
	// temporary allocations happens on the stack and not on the
	// free store. The number of shader modules will always be very
	// small.

	uint64_t pipeline_layout_hash_data[ 16 ];

	le_shader_module_handle const *end_shader_modules = shader_modules + numModules;

	uint64_t *elem = pipeline_layout_hash_data; // Get first element

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
static void shader_modules_collect_info( le_shader_manager_o *shader_manager, le_shader_module_handle const *shader_modules, size_t numModules, size_t *push_constant_buffer_size_max, vk::ShaderStageFlags *shader_stage_flags ) {
	for ( le_shader_module_handle const *s = shader_modules, *s_end = shader_modules + numModules; s != s_end; s++ ) {
		auto p_module = ( shader_manager->shaderModules.try_find( *s ) );
		assert( p_module && "shader module was not found" );
		if ( p_module ) {
			*push_constant_buffer_size_max = std::max<uint64_t>( p_module->push_constant_buffer_size, *push_constant_buffer_size_max );
			*shader_stage_flags |= le_to_vk( p_module->stage );
		}
	}
}

inline static uint64_t le_shader_bindings_calculate_hash( le_shader_binding_info const *info_vec, size_t info_count ) {
	uint64_t hash = 0;

	le_shader_binding_info const *info_begin = info_vec;
	auto const                    info_end   = info_vec + info_count;

	for ( le_shader_binding_info const *info = info_begin; info != info_end; info++ ) {
		hash = SpookyHash::Hash64( info, offsetof( le_shader_binding_info, name_hash ), hash );
	}

	return hash;
}

static void shader_module_update_reflection( le_shader_module_o *module ) {

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
		std::vector<vk::VertexInputAttributeDescription> vertexAttributeDescriptions; // <- gets stored in module at end
		std::vector<vk::VertexInputBindingDescription>   vertexBindingDescriptions;   // <- gets stored in module at end
		std::vector<std::string>                         vertexAttributeNames;        // <- gets stored in module at end

		size_t input_count = spv_module.input_variable_count;

		struct AttributeBindingDescription {
			vk::VertexInputAttributeDescription attribute;
			vk::VertexInputBindingDescription   binding;
			std::string                         name;
		};

		std::vector<AttributeBindingDescription> input_descriptions;
		input_descriptions.reserve( input_count );

		for ( size_t i = 0; i != input_count; i++ ) {
			auto &input = spv_module.input_variables[ i ];
			if ( input->location != uint32_t( ~0 ) ) {

				input_descriptions.emplace_back();
				auto &d = input_descriptions.back();

				d.name = input->name;
				d.attribute
				    .setBinding( input->location )                                    // by default, one binding per location
				    .setLocation( input->location )                                   // by default, one binding per location
				    .setOffset( 0 )                                                   // non-interleaved means offset must be 0
				    .setFormat( vk_format_from_spv_reflect_format( input->format ) ); // derive format from spv type
				d.binding
				    .setBinding( input->location )
				    .setInputRate( vk::VertexInputRate::eVertex )
				    .setStride( byte_stride_from_spv_type_description( input->type_description->traits.numeric ) );
			}
		}

		std::sort(
		    input_descriptions.begin(), input_descriptions.end(),
		    []( AttributeBindingDescription const &lhs, AttributeBindingDescription const &rhs ) -> bool {
			    return lhs.attribute.location < rhs.attribute.location;
		    } );

		for ( auto &d : input_descriptions ) {
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
		auto const &set = spv_module.descriptor_sets[ set_idx ];

		for ( size_t binding_idx = 0; binding_idx != set.binding_count; binding_idx++ ) {
			auto const &binding = set.bindings[ binding_idx ];

			le_shader_binding_info info{};

			info.setIndex   = binding->set;
			info.binding    = binding->binding;
			info.type       = descriptor_type_from_spv_descriptor_type( binding->descriptor_type );
			info.stage_bits = enumToNum( module->stage );
			info.count      = binding->count;

			// Dynamic uniform buffers need to specify a range given in bytes.
			if ( info.type == vk::DescriptorType::eUniformBufferDynamic ) {
				info.range = binding->block.size;
			}

			// For buffer Types the name of the binding we're interested in is the type name.
			if ( info.type == vk::DescriptorType::eUniformBufferDynamic ||
			     info.type == vk::DescriptorType::eStorageBufferDynamic ) {
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
static bool shader_module_check_bindings_valid( le_shader_binding_info const *bindings, size_t numBindings ) {
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
static std::vector<le_shader_binding_info> shader_modules_merge_bindings( le_shader_manager_o *shader_manager, le_shader_module_handle const *shader_handles, size_t shader_handles_count ) {

	static auto logger = LeLog( LOGGER_LABEL );
	// maxNumBindings holds the upper bound for the total number of bindings
	// assuming no overlaps in bindings between shader stages.

	std::vector<le_shader_module_o *> shader_stages;
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

	for ( auto &s : shader_stages ) {
		all_bindings.insert( all_bindings.end(), ( s )->bindings.begin(), ( s )->bindings.end() );
	}

	auto get_filepaths_affected_by_message = []( std::vector<le_shader_module_o *> const &shader_stages,
	                                             uint32_t                                 stage_bitfield ) {
		std::ostringstream os;

		// print out filenames for shader stage which matches stage bitflag

		for ( auto &s : shader_stages ) {
			if ( enumToNum( ( s )->stage ) & stage_bitfield ) {
				os << "\t '" << ( s )->filepath << "'" << std::endl;
			}
		}
		return os.str();
	};

	// -- Sort all_bindings so that they are ordered by set, location

	std::sort( all_bindings.begin(), all_bindings.end() );

	// -- Merge bindings, so that elements with common set, binding number are kept together.

	std::vector<le_shader_binding_info> combined_bindings;
	le_shader_binding_info *            last_binding = nullptr;

	for ( auto &b : all_bindings ) {

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

static void le_shader_manager_shader_module_update( le_shader_manager_o *self, le_shader_module_handle handle ) {

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
	bool loadSuccessful = false;
	auto source_text    = load_file( module->filepath, &loadSuccessful );

	if ( !loadSuccessful ) {
		// file could not be loaded. bail out.
		return;
	}

	std::vector<uint32_t> spirv_code;
	std::set<std::string> includesSet{ { module->filepath.string() } }; // let first element be the original source file path

	translate_to_spirv_code( self->shader_compiler, source_text.data(), source_text.size(), { module->source_language }, { module->stage }, module->filepath.string().c_str(), spirv_code, includesSet, module->macro_defines );

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
	self->device.destroyShaderModule( module->module );
	module->module = nullptr;

	// -- create new vulkan shader module object
	vk::ShaderModuleCreateInfo createInfo( vk::ShaderModuleCreateFlags(), module->spirv.size() * sizeof( uint32_t ), module->spirv.data() );
	module->module = self->device.createShaderModule( createInfo );
}

// ----------------------------------------------------------------------
// this method is called via renderer::update - before frame processing.
static void le_shader_manager_update_shader_modules( le_shader_manager_o *self ) {

	// -- find out which shader modules have been tainted

	// this will call callbacks on any watched file objects as a side effect
	// callbacks will modify le_backend->modifiedShaderModules
	le_file_watcher::le_file_watcher_i.poll_notifications( self->shaderFileWatcher );

	// -- update only modules which have been tainted

	for ( auto &s : self->modifiedShaderModules ) {
		le_shader_manager_shader_module_update( self, s );
	}

	self->modifiedShaderModules.clear();
}

// ----------------------------------------------------------------------

le_shader_manager_o *le_shader_manager_create( VkDevice_T *device ) {
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

static void le_shader_manager_destroy( le_shader_manager_o *self ) {

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
	self->shaderModules.iterator( []( le_shader_module_o *module, void *user_data ) {
		vk::Device device{ *static_cast<vk::Device *>( user_data ) };
		device.destroyShaderModule( module->module );
	},
	                              &self->device );

	self->shaderModules.clear();
	delete self;
}

static le_shader_module_handle le_shader_manager_get_next_available_handle( le_shader_manager_o *self ) {
	return reinterpret_cast<le_shader_module_handle>( ++self->modules_count );
}

// ----------------------------------------------------------------------
/// \brief create vulkan shader module based on file path
/// \details FIXME: this method can get called nearly anywhere - it should not be publicly accessible.
/// ideally, this method is only allowed to be called in the setup phase.
///
static le_shader_module_handle le_shader_manager_create_shader_module(
    le_shader_manager_o *             self,
    char const *                      path,
    const LeShaderSourceLanguageEnum &shader_source_language,
    const LeShaderStageEnum &         moduleType,
    char const *                      macro_defines_,
    le_shader_module_handle           handle,
    VkSpecializationMapEntry const *  specialization_map_entries,
    uint32_t                          specialization_map_entries_count,
    void *                            specialization_map_data,
    uint32_t                          specialization_map_data_num_bytes ) {

	static auto logger = LeLog( LOGGER_LABEL );

	// If handle_name == 0, then this means that this module does not have an explicit name,
	// we will give it a running number from the shader_manager.
	if ( handle == nullptr ) {
		handle = le_shader_manager_get_next_available_handle( self );
	}

	bool loadSuccessful = false;
	auto raw_file_data  = load_file( path, &loadSuccessful ); // returns a raw byte vector

	if ( !loadSuccessful ) {
		logger.error( "Could not load shader file: '%s'", path );
		assert( false && "file loading was unsuccessful" );
		return nullptr;
	}

	// ---------| invariant: load was successful

	// We use the canonical path to store a fingerprint of the file
	auto canonical_path_as_string = std::filesystem::canonical( path ).string();

	// -- Make sure the file contains spir-v code.

	std::vector<uint32_t> spirv_code;
	std::set<std::string> includesSet = { { canonical_path_as_string } }; // let first element be the source file path

	std::string macro_defines = macro_defines_ ? std::string( macro_defines_ ) : "";

	translate_to_spirv_code( self->shader_compiler, raw_file_data.data(), raw_file_data.size(), shader_source_language, moduleType, path, spirv_code, includesSet, macro_defines );

	// We include specialization data into hash calculation for this module, because specialization data
	// is stored with the module, and therefore it contributes to the module's phenotype.
	//
	uint64_t hash_specialization_constants = 0;

	if ( specialization_map_entries_count != 0 ) {
		hash_specialization_constants = SpookyHash::Hash64( specialization_map_data, specialization_map_data_num_bytes, hash_specialization_constants );
		hash_specialization_constants = SpookyHash::Hash64( specialization_map_entries, sizeof( vk::SpecializationMapEntry ) * specialization_map_entries_count, hash_specialization_constants );
	}

	le_shader_module_o module{};
	module.stage               = moduleType;
	module.filepath            = canonical_path_as_string;
	module.macro_defines       = macro_defines;
	module.hash_shader_defines = SpookyHash::Hash64( module.macro_defines.data(), module.macro_defines.size(), hash_specialization_constants );
	module.hash                = SpookyHash::Hash64( spirv_code.data(), spirv_code.size() * sizeof( uint32_t ), module.hash_shader_defines );
	module.spirv               = std::move( spirv_code );
	module.source_language     = shader_source_language;
	module.specialization_map_info.data.assign(
	    static_cast<char *>( specialization_map_data ),
	    static_cast<char *>( specialization_map_data ) + specialization_map_data_num_bytes );
	module.specialization_map_info.entries.assign(
	    reinterpret_cast<vk::SpecializationMapEntry const *>( specialization_map_entries ),
	    reinterpret_cast<vk::SpecializationMapEntry const *>( specialization_map_entries ) + specialization_map_entries_count );

	static_assert( sizeof( vk::SpecializationMapEntry ) == sizeof( VkSpecializationMapEntry ), "SpecializationMapEntry must be of same size, whether using vkhpp or not." );

	le_shader_module_o *cached_module = self->shaderModules.try_find( handle );

	if ( cached_module && cached_module->hash == module.hash ) {
		// A module with the same handle already exists, and it has the same hash: no more work to do.
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

	vk::ShaderModuleCreateInfo createInfo( vk::ShaderModuleCreateFlags(), module.spirv.size() * sizeof( uint32_t ), module.spirv.data() );
	module.module = self->device.createShaderModule( createInfo );
	logger.info( "Vk shader module created %p", module.module );

	if ( cached_module == nullptr ) {
		// there is no prior module - let's create a module and try to retain it in shader manager
		bool insert_successful = self->shaderModules.try_insert( handle, &module );
		if ( !insert_successful ) {
			logger.error( "Could not retain shader module" );
			self->device.destroyShaderModule( module.module );
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
		self->device.destroyShaderModule( old_module.module );
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
static vk::PipelineLayout le_pipeline_manager_get_pipeline_layout( le_pipeline_manager_o *self, le_shader_module_handle const *shader_modules, size_t numModules ) {

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
static vk::Pipeline le_pipeline_cache_create_graphics_pipeline( le_pipeline_manager_o *self, graphics_pipeline_state_o const *pso, const LeRenderPass &pass, uint32_t subpass ) {

	std::vector<vk::PipelineShaderStageCreateInfo> pipelineStages;
	pipelineStages.reserve( pso->shaderModules.size() );

	std::vector<vk::SpecializationInfo *> p_specialization_infos;
	p_specialization_infos.reserve( pso->shaderModules.size() );

	le_shader_module_o *vertexShaderModule = nullptr; // We may need the vertex shader module later

	for ( auto const &shader_stage : pso->shaderModules ) {

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
		vk::SpecializationInfo *&p_specialization_info = p_specialization_infos.back();

		// Fetch specialization constant data from shader
		// associate it with p_specialization_info

		if ( !s->specialization_map_info.entries.empty() ) {
			p_specialization_info = new ( vk::SpecializationInfo );
			p_specialization_info
			    ->setMapEntryCount( s->specialization_map_info.entries.size() )
			    .setPMapEntries( s->specialization_map_info.entries.data() )
			    .setDataSize( s->specialization_map_info.data.size() )
			    .setPData( s->specialization_map_info.data.data() );
		}

		vk::PipelineShaderStageCreateInfo info{};
		info
		    .setFlags( {} )                                  // must be 0 - "reserved for future use"
		    .setStage( le_to_vk( s->stage ) )                //
		    .setModule( s->module )                          //
		    .setPName( "main" )                              //
		    .setPSpecializationInfo( p_specialization_info ) // set specialisation constants, if any
		    ;

		pipelineStages.emplace_back( info );
	}

	std::vector<vk::VertexInputBindingDescription>   vertexBindingDescriptions;        // Where to get data from
	std::vector<vk::VertexInputAttributeDescription> vertexInputAttributeDescriptions; // How it feeds into the shader's vertex inputs

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
			for ( auto const &b : pso->explicitVertexInputBindingDescriptions ) {

				vk::VertexInputBindingDescription bindingDescription;
				bindingDescription
				    .setBinding( b.binding )
				    .setStride( b.stride )
				    .setInputRate( vk_input_rate_from_le_input_rate( b.input_rate ) );

				vertexBindingDescriptions.emplace_back( std::move( bindingDescription ) );
			}

			for ( auto const &a : pso->explicitVertexAttributeDescriptions ) {
				vk::VertexInputAttributeDescription attributeDescription;
				attributeDescription
				    .setLocation( a.location )
				    .setBinding( a.binding )
				    .setOffset( a.binding_offset )
				    .setFormat( vk_format_from_le_vertex_input_attribute_description( a ) );

				vertexInputAttributeDescriptions.emplace_back( std::move( attributeDescription ) );
			}
		}
	}

	// Combine vertex input `binding` state and vertex input `attribute` state into
	// something that vk will accept
	vk::PipelineVertexInputStateCreateInfo vertexInputStageInfo;
	vertexInputStageInfo
	    .setFlags( vk::PipelineVertexInputStateCreateFlags() )
	    .setVertexBindingDescriptionCount( uint32_t( vertexBindingDescriptions.size() ) )
	    .setPVertexBindingDescriptions( vertexBindingDescriptions.data() )
	    .setVertexAttributeDescriptionCount( uint32_t( vertexInputAttributeDescriptions.size() ) )
	    .setPVertexAttributeDescriptions( vertexInputAttributeDescriptions.data() ) //
	    ;

	// Fetch vk::PipelineLayout for this pso
	auto pipelineLayout = le_pipeline_manager_get_pipeline_layout( self, pso->shaderModules.data(), pso->shaderModules.size() );

	//
	// We must match blend attachment states with number of attachments for
	// the current renderpass - each attachment may have their own blend state.
	// Our pipeline objects will have 16 stages which are readable.
	//
	assert( pass.numColorAttachments <= VK_MAX_COLOR_ATTACHMENTS );
	//
	vk::PipelineColorBlendStateCreateInfo colorBlendState;
	colorBlendState
	    .setLogicOpEnable( VK_FALSE )
	    .setLogicOp( ::vk::LogicOp::eClear )
	    .setAttachmentCount( pass.numColorAttachments )
	    .setPAttachments( pso->data.blendAttachmentStates.data() )
	    .setBlendConstants( pso->data.blend_factor_constants );

	// Viewport and Scissor are tracked as dynamic states, and although this object will not
	// get used, we must still fulfill the contract of providing a valid object to vk.
	//
	static vk::PipelineViewportStateCreateInfo defaultViewportState{ vk::PipelineViewportStateCreateFlags(), 1, nullptr, 1, nullptr };

	// We will allways keep Scissor, Viewport and LineWidth as dynamic states,
	// otherwise we might have way too many pipelines flying around.
	std::array<vk::DynamicState, 3> dynamicStates = { {
	    vk::DynamicState::eScissor,
	    vk::DynamicState::eViewport,
	    vk::DynamicState::eLineWidth,
	} };

	vk::PipelineDynamicStateCreateInfo dynamicState;
	dynamicState
	    .setDynamicStateCount( dynamicStates.size() )
	    .setPDynamicStates( dynamicStates.data() );

	// We must patch pipeline multisample state here - this is because we may not know the renderpass a pipeline
	// is used with, and the number of samples such renderpass supports.
	auto multisampleCreateInfo = pso->data.multisampleState;

	multisampleCreateInfo.setRasterizationSamples( pass.sampleCount );

	// setup pipeline
	vk::GraphicsPipelineCreateInfo gpi;
	gpi
	    .setFlags( vk::PipelineCreateFlagBits::eAllowDerivatives ) //
	    .setStageCount( uint32_t( pipelineStages.size() ) )        // set shaders
	    .setPStages( pipelineStages.data() )                       // set shaders
	    .setPVertexInputState( &vertexInputStageInfo )             //
	    .setPInputAssemblyState( &pso->data.inputAssemblyState )   //
	    .setPTessellationState( &pso->data.tessellationState )     //
	    .setPViewportState( &defaultViewportState )                // not used as these states are dynamic, defaultState is a dummy value to pacify driver
	    .setPRasterizationState( &pso->data.rasterizationInfo )    //
	    .setPMultisampleState( &multisampleCreateInfo )            // <- we patch this with correct sample count for renderpass, because otherwise not possible
	    .setPDepthStencilState( &pso->data.depthStencilState )     //
	    .setPColorBlendState( &colorBlendState )                   //
	    .setPDynamicState( &dynamicState )                         //
	    .setLayout( pipelineLayout )                               //
	    .setRenderPass( pass.renderPass )                          // must be a valid renderpass.
	    .setSubpass( subpass )                                     //
	    .setBasePipelineHandle( nullptr )                          //
	    .setBasePipelineIndex( 0 )                                 // -1 signals not to use a base pipeline index

	    ;

	auto creation_result = self->device.createGraphicsPipeline( self->vulkanCache, gpi );

	for ( auto &p_spec : p_specialization_infos ) {
		delete ( p_spec );
	}
	p_specialization_infos.clear();

	assert( creation_result.result == vk::Result::eSuccess && "pipeline must be created successfully" );
	return creation_result.value;
}

// ----------------------------------------------------------------------

static vk::Pipeline le_pipeline_cache_create_compute_pipeline( le_pipeline_manager_o *self, compute_pipeline_state_o const *pso ) {

	// Fetch vk::PipelineLayout for this pso
	auto pipelineLayout = le_pipeline_manager_get_pipeline_layout( self, &pso->shaderStage, 1 );
	auto s              = self->shaderManager->shaderModules.try_find( pso->shaderStage );
	assert( s && "shader module could not be found" );

	vk::SpecializationInfo *p_specialization_info = nullptr;
	if ( !s->specialization_map_info.entries.empty() ) {
		p_specialization_info = new ( vk::SpecializationInfo );
		p_specialization_info
		    ->setMapEntryCount( s->specialization_map_info.entries.size() )
		    .setPMapEntries( s->specialization_map_info.entries.data() )
		    .setDataSize( s->specialization_map_info.data.size() )
		    .setPData( s->specialization_map_info.data.data() );
	}

	vk::PipelineShaderStageCreateInfo shaderStage{};
	shaderStage
	    .setFlags( {} )                                  // must be 0 - "reserved for future use"
	    .setStage( le_to_vk( s->stage ) )                //
	    .setModule( s->module )                          //
	    .setPName( "main" )                              //
	    .setPSpecializationInfo( p_specialization_info ) // Set specialization info (useful for setting workgroup size for example)
	    ;

	vk::ComputePipelineCreateInfo cpi;
	cpi
	    .setFlags( vk::PipelineCreateFlagBits::eAllowDerivatives )
	    .setStage( shaderStage )
	    .setLayout( pipelineLayout )
	    .setBasePipelineHandle( nullptr )
	    .setBasePipelineIndex( 0 ) // -1 signals not to use base pipeline index
	    ;

	auto creation_result = self->device.createComputePipeline( self->vulkanCache, cpi );
	assert( creation_result.result == vk::Result::eSuccess && "pipeline must be created successfully" );

	delete ( p_specialization_info );
	return creation_result.value;
}

// ----------------------------------------------------------------------

#ifdef LE_FEATURE_RTX
static vk::RayTracingShaderGroupTypeKHR le_to_vk( le::RayTracingShaderGroupType const &tp ) {
	// clang-format off
    switch(tp){
	    case (le::RayTracingShaderGroupType::eTrianglesHitGroup  ) : return vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
	    case (le::RayTracingShaderGroupType::eProceduralHitGroup ) : return vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup;
	    case (le::RayTracingShaderGroupType::eRayGen             ) : return vk::RayTracingShaderGroupTypeKHR::eGeneral;
	    case (le::RayTracingShaderGroupType::eMiss               ) : return vk::RayTracingShaderGroupTypeKHR::eGeneral;
	    case (le::RayTracingShaderGroupType::eCallable           ) : return vk::RayTracingShaderGroupTypeKHR::eGeneral; 
    }
	// clang-format on
	assert( false ); // unreachable
	return vk::RayTracingShaderGroupTypeKHR::eGeneral;
}

// ----------------------------------------------------------------------

static vk::Pipeline le_pipeline_cache_create_rtx_pipeline( le_pipeline_manager_o *self, rtx_pipeline_state_o const *pso ) {

	// Fetch vk::PipelineLayout for this pso
	auto pipelineLayout = le_pipeline_manager_get_pipeline_layout( self, pso->shaderStages.data(), pso->shaderStages.size() );

	std::vector<vk::PipelineShaderStageCreateInfo> pipelineStages;
	pipelineStages.reserve( pso->shaderStages.size() );

	le_shader_module_handle rayGenModule = nullptr; // We may need the ray gen shader module later

	for ( auto const &shader_stage : pso->shaderStages ) {

		auto s = self->shaderManager->shaderModules.try_find( shader_stage );
		assert( s && "could not find shader module" );

		if ( s->stage == le::ShaderStage::eRaygenBitKhr ) {
			rayGenModule = shader_stage;
		}

		vk::PipelineShaderStageCreateInfo info{};
		info
		    .setFlags( {} )                    // must be 0 - "reserved for future use"
		    .setStage( le_to_vk( s->stage ) )  //
		    .setModule( s->module )            //
		    .setPName( "main" )                //
		    .setPSpecializationInfo( nullptr ) //
		    ;

		pipelineStages.emplace_back( info );
	}

	std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shadingGroups;

	shadingGroups.reserve( pso->shaderGroups.size() );

	// Fill in shading groups from pso->groups

	for ( auto const &group : pso->shaderGroups ) {
		vk::RayTracingShaderGroupCreateInfoKHR info;
		info
		    .setType( le_to_vk( group.type ) )
		    .setGeneralShader( group.generalShaderIdx )
		    .setClosestHitShader( group.closestHitShaderIdx )
		    .setAnyHitShader( group.anyHitShaderIdx )
		    .setIntersectionShader( group.intersectionShaderIdx );
		shadingGroups.emplace_back( std::move( info ) );
	}

	vk::RayTracingPipelineCreateInfoKHR create_info;
	create_info
	    .setFlags( {} )
	    .setStageCount( uint32_t( pipelineStages.size() ) )
	    .setPStages( pipelineStages.data() )
	    .setGroupCount( uint32_t( shadingGroups.size() ) )
	    .setPGroups( shadingGroups.data() )
	    .setMaxPipelineRayRecursionDepth( 16 ) // FIXME: we should probably reduce this,
	                                           // or expose it via the api, but definitely
	                                           // limit it to hardware limit
	    .setLayout( pipelineLayout )
	    .setBasePipelineHandle( nullptr )
	    .setBasePipelineIndex( 0 );

	auto result = self->device.createRayTracingPipelineKHR( nullptr, self->vulkanCache, create_info );
	assert( vk::Result::eSuccess == result.result );
	return result.value;
}
#endif

// ----------------------------------------------------------------------

/// \brief returns hash key for given bindings, creates and retains new vkDescriptorSetLayout inside backend if necessary
static uint64_t le_pipeline_cache_produce_descriptor_set_layout( le_pipeline_manager_o *self, std::vector<le_shader_binding_info> const &bindings, vk::DescriptorSetLayout *layout ) {

	auto &descriptorSetLayouts = self->descriptorSetLayouts; // FIXME: this method only needs rw access to this, and the device

	// -- Calculate hash based on le_shader_binding_infos for this set
	uint64_t set_layout_hash = le_shader_bindings_calculate_hash( bindings.data(), bindings.size() );

	auto foundLayout = descriptorSetLayouts.try_find( set_layout_hash );

	if ( foundLayout ) {

		// -- Layout was found in cache, reuse it.

		*layout = foundLayout->vk_descriptor_set_layout;

	} else {

		// -- Layout was not found in cache, we must create vk objects.

		std::vector<vk::DescriptorSetLayoutBinding> vk_bindings;

		vk_bindings.reserve( bindings.size() );

		for ( const auto &b : bindings ) {
			vk::DescriptorSetLayoutBinding binding{};
			binding.setBinding( b.binding )
			    .setDescriptorType( vk::DescriptorType( b.type ) )
			    .setDescriptorCount( b.count )
			    .setStageFlags( vk::ShaderStageFlags( b.stage_bits ) )
			    .setPImmutableSamplers( nullptr );
			vk_bindings.emplace_back( std::move( binding ) );
		}

		vk::DescriptorSetLayoutCreateInfo setLayoutInfo;
		setLayoutInfo
		    .setFlags( vk::DescriptorSetLayoutCreateFlags() )
		    .setBindingCount( uint32_t( vk_bindings.size() ) )
		    .setPBindings( vk_bindings.data() );

		*layout = self->device.createDescriptorSetLayout( setLayoutInfo );

		// -- Create descriptorUpdateTemplate
		//
		// The template needs to be created so that data for a vk::DescriptorSet
		// can be read from a vector of tightly packed
		// DescriptorData elements.
		//

		vk::DescriptorUpdateTemplate updateTemplate;
		{
			std::vector<vk::DescriptorUpdateTemplateEntry> entries;

			entries.reserve( bindings.size() );

			size_t base_offset = 0; // offset in bytes into DescriptorData vector, assuming vector is tightly packed.
			for ( const auto &b : bindings ) {
				vk::DescriptorUpdateTemplateEntry entry;

				auto descriptorType = vk::DescriptorType( b.type );

				entry.setDstBinding( b.binding );
				entry.setDescriptorCount( b.count );
				entry.setDescriptorType( descriptorType );
				entry.setDstArrayElement( 0 ); // starting element at this binding to update - always 0

				// set offset based on type of binding, so that template reads from correct data

				switch ( descriptorType ) {
				case vk::DescriptorType::eAccelerationStructureKHR:
					entry.setOffset( base_offset + offsetof( DescriptorData, accelerationStructureInfo ) );
					break;
				case vk::DescriptorType::eUniformTexelBuffer:
					assert( false ); // not implemented
					break;
				case vk::DescriptorType::eStorageTexelBuffer:
					assert( false ); // not implemented
					break;
				case vk::DescriptorType::eInputAttachment:
					assert( false ); // not implemented
					break;
				case vk::DescriptorType::eCombinedImageSampler:                              // fall-through, as this kind of descriptor uses ImageInfo or parts thereof
				case vk::DescriptorType::eSampledImage:                                      // fall-through, as this kind of descriptor uses ImageInfo or parts thereof
				case vk::DescriptorType::eStorageImage:                                      // fall-through, as this kind of descriptor uses ImageInfo or parts thereof
				case vk::DescriptorType::eSampler:                                           // fall-through, as this kind of descriptor uses ImageInfo or parts thereof
					entry.setOffset( base_offset + offsetof( DescriptorData, imageInfo ) );  // <- point to first field of ImageInfo
					break;                                                                   //
				case vk::DescriptorType::eUniformBuffer:                                     // fall-through as this kind of descriptor uses BufferInfo
				case vk::DescriptorType::eStorageBuffer:                                     // fall-through as this kind of descriptor uses BufferInfo
				case vk::DescriptorType::eUniformBufferDynamic:                              // fall-through as this kind of descriptor uses BufferInfo
				case vk::DescriptorType::eStorageBufferDynamic:                              //
					entry.setOffset( base_offset + offsetof( DescriptorData, bufferInfo ) ); // <- point to first element of BufferInfo
					break;
				default:
					assert( false && "invalid descriptor type" );
				}

				entry.setStride( sizeof( DescriptorData ) );

				entries.emplace_back( std::move( entry ) );

				base_offset += sizeof( DescriptorData );
			}

			vk::DescriptorUpdateTemplateCreateInfo info;
			info
			    .setFlags( {} ) // no flags for now
			    .setDescriptorUpdateEntryCount( uint32_t( entries.size() ) )
			    .setPDescriptorUpdateEntries( entries.data() )
			    .setTemplateType( vk::DescriptorUpdateTemplateType::eDescriptorSet )
			    .setDescriptorSetLayout( *layout )
			    .setPipelineBindPoint( {} ) // ignored, since update_template_type is not push_descriptors
			    .setPipelineLayout( {} )    // ignored, since update_template_type is not push_descriptors
			    .setSet( 0 )                // ignored, since update_template_type is not push_descriptors
			    ;

			updateTemplate = self->device.createDescriptorUpdateTemplate( info );
		}

		le_descriptor_set_layout_t le_layout_info;
		le_layout_info.vk_descriptor_set_layout      = *layout;
		le_layout_info.binding_info                  = bindings;
		le_layout_info.vk_descriptor_update_template = updateTemplate;

		bool result = descriptorSetLayouts.try_insert( set_layout_hash, &le_layout_info );

		assert( result && "descriptorSetLayout insertion must be successful" );
	}

	return set_layout_hash;
}

// ----------------------------------------------------------------------
// Calculates pipeline layout info by first consolidating all bindings
// over all referenced shader modules, and then ordering these by descriptor sets.
//
static le_pipeline_layout_info le_pipeline_manager_produce_pipeline_layout_info( le_pipeline_manager_o *self, le_shader_module_handle const *shader_modules, size_t shader_modules_count ) {
	le_pipeline_layout_info info{};

	std::vector<le_shader_binding_info> combined_bindings = shader_modules_merge_bindings( self->shaderManager, shader_modules, shader_modules_count );

	// -- Create array of DescriptorSetLayouts
	std::array<vk::DescriptorSetLayout, 8> vkLayouts{};
	{

		// -- Create one vkDescriptorSetLayout for each set in bindings

		std::vector<std::vector<le_shader_binding_info>> sets;

		// Split combined bindings at set boundaries
		uint32_t set_idx = 0;
		for ( auto it = combined_bindings.begin(); it != combined_bindings.end(); ) {

			// Find next element with different set id
			auto itN = std::find_if( it, combined_bindings.end(), [ &set_idx ]( const le_shader_binding_info &el ) -> bool {
				return el.setIndex != set_idx;
			} );

			sets.emplace_back( it, itN );

			// If we're not at the end, get the setIndex for the next set,
			if ( itN != combined_bindings.end() ) {
				assert( set_idx + 1 == itN->setIndex ); // we must enforce that sets are non-sparse.
				set_idx = itN->setIndex;
			}

			it = itN;
		}

		info.set_layout_count = uint32_t( sets.size() );
		assert( sets.size() <= VK_MAX_BOUND_DESCRIPTOR_SETS ); // must be less or equal to maximum bound descriptor sets (currently 8 on NV)

		{
			// Assert that sets and bindings are not sparse (you must not have "holes" in sets, bindings.)
			// FIXME: (check-shader-bindings) we must find a way to recover from this, but it might be difficult without a "linking" stage
			// which combines various shader stages.
			set_idx = 0;
			for ( auto const &s : sets ) {
				uint32_t binding = 0;
				for ( auto const &b : s ) {
					assert( b.binding == binding );
					assert( b.setIndex == set_idx );
					binding++;
				}
				set_idx++;
			}
		}

		for ( size_t i = 0; i != sets.size(); ++i ) {
			info.set_layout_keys[ i ] = le_pipeline_cache_produce_descriptor_set_layout( self, sets[ i ], &vkLayouts[ i ] );
		}
	}

	// -- Collect data over all shader stages: push_constant buffer size, active shader stages

	static_assert( sizeof( std::underlying_type<vk::ShaderStageFlagBits>::type ) == sizeof( uint32_t ), "ShaderStageFlagBits must be same size as uint32_t" );

	vk::ShaderStageFlags active_shader_stages;
	uint64_t             push_constant_buffer_size = 0;
	shader_modules_collect_info( self->shaderManager, shader_modules, shader_modules_count, &push_constant_buffer_size, &active_shader_stages );
	info.active_vk_shader_stages = uint32_t( active_shader_stages );

	// -- Attempt to find this pipelineLayout from cache, if we can't find one, we create and retain it.
	info.pipeline_layout_key = shader_modules_get_pipeline_layout_hash( self->shaderManager, shader_modules, shader_modules_count );

	auto found_pl = self->pipelineLayouts.try_find( info.pipeline_layout_key );

	if ( nullptr == found_pl ) {

		vk::PushConstantRange push_constant_range;
		push_constant_range
		    .setOffset( 0 )
		    .setSize( push_constant_buffer_size )
		    .setStageFlags( active_shader_stages );

		vk::Device                   device = self->device;
		vk::PipelineLayoutCreateInfo layoutCreateInfo;
		layoutCreateInfo
		    .setFlags( vk::PipelineLayoutCreateFlags() ) // "reserved for future use"
		    .setSetLayoutCount( uint32_t( info.set_layout_count ) )
		    .setPSetLayouts( vkLayouts.data() )
		    .setPushConstantRangeCount( push_constant_buffer_size ? 1 : 0 )
		    .setPPushConstantRanges( push_constant_buffer_size ? &push_constant_range : nullptr );

		// Create vkPipelineLayout
		vk::PipelineLayout pipelineLayout = device.createPipelineLayout( layoutCreateInfo );
		// Attempt to store pipeline layout in cache
		bool result = self->pipelineLayouts.try_insert( info.pipeline_layout_key, &pipelineLayout );

		if ( false == result ) {
			// If we couldn't store the pipeline layout in cache, we must manually
			// dispose of be vulkan object, otherwise the cache will take care of cleanup.
			device.destroyPipelineLayout( pipelineLayout );
		}
	}

	return info;
}

// ----------------------------------------------------------------------
// HOT path - this gets executed every frame
static inline void le_pipeline_manager_produce_pipeline_layout_info(
    le_pipeline_manager_o *        self,
    le_shader_module_handle const *shader_modules,
    size_t                         shader_modules_count,
    le_pipeline_layout_info *      pipeline_layout_info,
    uint64_t *                     pipeline_layout_hash ) {

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
    le_pipeline_manager_o *self,
    le_gpso_handle         gpso_handle,
    const LeRenderPass &pass, uint32_t subpass ) {

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
	graphics_pipeline_state_o const *pso = self->graphicsPso.try_find( gpso_handle );
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

		for ( auto const &s : pso->shaderModules ) {
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
static le_pipeline_and_layout_info_t le_pipeline_manager_produce_rtx_pipeline( le_pipeline_manager_o *self, le_rtxpso_handle pso_handle, char **maybe_shader_group_data ) {
	le_pipeline_and_layout_info_t pipeline_and_layout_info = {};
#ifdef LE_FEATURE_RTX

	static auto logger = LeLog( LOGGER_LABEL );
	// -- 0. Fetch pso from cache using its hash key
	rtx_pipeline_state_o const *pso = self->rtxPso.try_find( pso_handle );
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

		for ( auto const &shader_stage : pso->shaderStages ) {
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
			using namespace le_backend_vk;
			VkPhysicalDeviceRayTracingPipelinePropertiesKHR props;
			bool                                            result = vk_device_i.get_vk_physical_device_ray_tracing_properties( self->le_device, &props );
			assert( result && "properties must be successfully acquired." );

			size_t dataSize   = props.shaderGroupHandleSize * pso->shaderGroups.size();
			size_t bufferSize = dataSize + sizeof( LeShaderGroupDataHeader );

			// Allocate buffer to store handles
			char *handles = static_cast<char *>( malloc( bufferSize ) );

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
				auto success = self->device.getRayTracingShaderGroupHandlesKHR(
				    pipeline_and_layout_info.pipeline,
				    0, uint32_t( pso->shaderGroups.size() ),
				    dataSize, handles + sizeof( LeShaderGroupDataHeader ) );
				assert( success == vk::Result::eSuccess );
			}
			self->rtx_shader_group_data.try_insert( pipeline_hash, &handles );

			// we need to store this buffer with the pipeline - or at least associate is to the pso

			logger.info( "Queried rtx shader group handles:" );
			size_t n_el = props.shaderGroupHandleSize / sizeof( uint32_t );

			uint32_t *debug_handles = reinterpret_cast<uint32_t *>( handles + sizeof( LeShaderGroupDataHeader ) );
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
#else
	assert( false && "backend compiled without RTX features, but RTX feature requested." );
#endif
	return pipeline_and_layout_info;
}

// ----------------------------------------------------------------------

static le_pipeline_and_layout_info_t le_pipeline_manager_produce_compute_pipeline( le_pipeline_manager_o *self, le_cpso_handle cpso_handle ) {

	static auto                     logger = LeLog( LOGGER_LABEL );
	compute_pipeline_state_o const *pso    = self->computePso.try_find( cpso_handle );
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
bool le_pipeline_manager_introduce_graphics_pipeline_state( le_pipeline_manager_o *self, graphics_pipeline_state_o *pso, le_gpso_handle *handle ) {

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

	for ( auto const &module_handle : pso->shaderModules ) {
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
bool le_pipeline_manager_introduce_compute_pipeline_state( le_pipeline_manager_o *self, compute_pipeline_state_o *pso, le_cpso_handle *handle ) {

	le_shader_module_o *shader_module = self->shaderManager->shaderModules.try_find( pso->shaderStage );
	assert( shader_module && "could not find shader module" );
	*handle = reinterpret_cast<le_cpso_handle &>( shader_module->hash );

	return self->computePso.try_insert( *handle, pso );
};

// ----------------------------------------------------------------------
// This method may get called through the pipeline builder -
// via RECORD in command buffer recording state
// in SETUP
bool le_pipeline_manager_introduce_rtx_pipeline_state( le_pipeline_manager_o *self, rtx_pipeline_state_o *pso, le_rtxpso_handle *handle ) {

	// Calculate hash over all pipeline stages,
	// and pipeline shader group infos

	uint64_t hash_value = 0;

	// calculate hash over all shader module hashes.

	std::vector<uint64_t> shader_module_hashes;

	shader_module_hashes.reserve( pso->shaderStages.size() );
	for ( auto const &shader_stage : pso->shaderStages ) {
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

static VkPipelineLayout le_pipeline_manager_get_pipeline_layout_public( le_pipeline_manager_o *self, uint64_t key ) {
	vk::PipelineLayout const *pLayout = self->pipelineLayouts.try_find( key );
	assert( pLayout && "layout cannot be nullptr" );
	return *pLayout;
}

// ----------------------------------------------------------------------

static const le_descriptor_set_layout_t *le_pipeline_manager_get_descriptor_set_layout( le_pipeline_manager_o *self, uint64_t setlayout_key ) {
	return self->descriptorSetLayouts.try_find( setlayout_key );
};

// ----------------------------------------------------------------------

static le_shader_module_handle le_pipeline_manager_create_shader_module(
    le_pipeline_manager_o *           self,
    char const *                      path,
    const LeShaderSourceLanguageEnum &shader_source_language,
    const LeShaderStageEnum &         moduleType,
    char const *                      macro_definitions,
    le_shader_module_handle           handle,
    VkSpecializationMapEntry const *  specialization_map_entries,
    uint32_t                          specialization_map_entries_count,
    void *                            specialization_map_data,
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

static void le_pipeline_manager_update_shader_modules( le_pipeline_manager_o *self ) {
	le_shader_manager_update_shader_modules( self->shaderManager );
}

// ----------------------------------------------------------------------

static le_pipeline_manager_o *le_pipeline_manager_create( le_device_o *le_device ) {
	auto self = new le_pipeline_manager_o();

	using namespace le_backend_vk;
	self->le_device = le_device;
	vk_device_i.increase_reference_count( le_device );
	self->device = vk_device_i.get_vk_device( le_device );

	vk::PipelineCacheCreateInfo pipelineCacheInfo;
	pipelineCacheInfo
	    .setFlags( vk::PipelineCacheCreateFlags() ) // "reserved for future use"
	    .setInitialDataSize( 0 )
	    .setPInitialData( nullptr );

	self->vulkanCache   = self->device.createPipelineCache( pipelineCacheInfo );
	self->shaderManager = le_shader_manager_create( self->device );

	return self;
}

// ----------------------------------------------------------------------

static void le_pipeline_manager_destroy( le_pipeline_manager_o *self ) {

	static auto logger = LeLog( LOGGER_LABEL );

	le_shader_manager_destroy( self->shaderManager );
	self->shaderManager = nullptr;

	// -- destroy any objects which were allocated via Vulkan API - these
	// need to be destroyed using the device they were allocated from.

	// -- destroy descriptorSetLayouts, and descriptorUpdateTemplates
	self->descriptorSetLayouts.iterator(
	    []( le_descriptor_set_layout_t *e, void *user_data ) {
		    vk::Device *device = static_cast<vk::Device *>( user_data );
		    if ( e->vk_descriptor_set_layout ) {
			    device->destroyDescriptorSetLayout( e->vk_descriptor_set_layout );
			    logger.info( "Destroyed VkDescriptorSetLayout: %p", e->vk_descriptor_set_layout );
		    }
		    if ( e->vk_descriptor_update_template ) {
			    device->destroyDescriptorUpdateTemplate( e->vk_descriptor_update_template );
			    logger.info( "Destroyed VkDescriptorUpdateTemplate: %p", e->vk_descriptor_update_template );
		    }
	    },
	    &self->device );

	// -- destroy pipelineLayouts
	self->pipelineLayouts.iterator(
	    []( vk::PipelineLayout *e, void *user_data ) {
		    vk::Device *device = static_cast<vk::Device *>( user_data );
		    device->destroyPipelineLayout( *e );
		    logger.info( "Destroyed VkPipelineLayout: %p", *e );
	    },
	    &self->device );

	// Clear pipelines before we destroy pipeline cache object.
	// we must first iterate over all pipeline objects to delete any pipelines

	self->pipelines.iterator(
	    []( VkPipeline *p, void *user_data ) {
		    auto device = static_cast<vk::Device *>( user_data );
		    device->destroyPipeline( *p );
		    logger.info( "Destroyed VkPipeline: %p", *p );
	    },
	    &self->device );

	self->pipelines.clear();

	self->rtx_shader_group_data.iterator(
	    []( char **p_buffer, void * ) {
		    free( *p_buffer );
	    },
	    nullptr );

	// Destroy Pipeline Cache

	if ( self->vulkanCache ) {
		self->device.destroyPipelineCache( self->vulkanCache );
	}

	le_backend_vk::vk_device_i.decrease_reference_count( self->le_device );
	self->le_device = nullptr;

	delete self;
}

// ----------------------------------------------------------------------

void register_le_pipeline_vk_api( void *api_ ) {

	auto le_backend_vk_api_i = static_cast<le_backend_vk_api *>( api_ );
	{
		auto &i = le_backend_vk_api_i->le_pipeline_manager_i;

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
		auto &i = le_backend_vk_api_i->le_shader_module_i;
		//		i.get_hash  = le_shader_module_get_hash;
		i.get_stage = le_shader_module_get_stage;
	}
	{
		// Store callback address with api so that callback gets automatically forwarded to the correct
		// address when backend reloads : see le_core.h / callback forwarding
		auto &i            = le_backend_vk_api_i->private_shader_file_watcher_i;
		i.on_callback_addr = ( void * )le_shader_file_watcher_on_callback;
	}
}
