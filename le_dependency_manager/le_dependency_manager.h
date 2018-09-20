#ifndef GUARD_le_dependency_manager_H
#define GUARD_le_dependency_manager_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_dependency_manager_o;

void register_le_dependency_manager_api( void *api );

// clang-format off
struct le_dependency_manager_api {


	static constexpr auto id      = "le_dependency_manager";
	static constexpr auto pRegFun = register_le_dependency_manager_api;

	struct le_dependency_manager_interface_t {

		le_dependency_manager_o * ( * create                   ) ( );
		void                      ( * destroy                  ) ( le_dependency_manager_o* self );
		void                      ( * next_root_layer          ) ( le_dependency_manager_o *self, char const *debug_name );
		void                      ( * next_layer               ) ( le_dependency_manager_o *self, char const *debug_name );
		void                      ( * add_resource             ) ( le_dependency_manager_o *self, uint64_t resourceID, uint8_t access_type ) ;
		void                      ( * resolve_dependencies     ) ( le_dependency_manager_o *self );
		void                      ( * get_layer_sort_indices   ) ( le_dependency_manager_o* self, uint32_t** pIndices, size_t* pIndicesCount);
		void                      ( * print_sort_order         ) ( le_dependency_manager_o* self);
	};

	le_dependency_manager_interface_t       le_dependency_manager_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_dependency_manager {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_dependency_manager_api>( true );
#	else
const auto api = Registry::addApiStatic<le_dependency_manager_api>();
#	endif

static const auto &manager_i = api -> le_dependency_manager_i;

enum AccessType : uint8_t {
	eAccessTypeRead      = 1 << 0,
	eAccessTypeWrite     = 1 << 1,
	eAccessTypeReadWrite = eAccessTypeRead | eAccessTypeWrite,
};

} // namespace le_dependency_manager

class LeDependencyManager : NoCopy, NoMove {

	le_dependency_manager_o *self;

  public:
	LeDependencyManager()
	    : self( le_dependency_manager::manager_i.create() ) {
	}

	~LeDependencyManager() {
		le_dependency_manager::manager_i.destroy( self );
	}

	void addResource( const uint64_t &resourceId, const le_dependency_manager::AccessType &access_type ) {
		le_dependency_manager::manager_i.add_resource( self, resourceId, access_type );
	}

	void nextLayer( char const *debug_name = nullptr, bool isRoot = false ) {
		if ( isRoot ) {
			le_dependency_manager::manager_i.next_root_layer( self, debug_name );
		} else {
			le_dependency_manager::manager_i.next_layer( self, debug_name );
		}
	}

	void resolveDependencies() {
		le_dependency_manager::manager_i.resolve_dependencies( self );
	}

	void getLayerSortIndices( uint32_t **pIndices, size_t *pIndicesCount ) {
		le_dependency_manager::manager_i.get_layer_sort_indices( self, pIndices, pIndicesCount );
	}

	void printSortOrder() {
		le_dependency_manager::manager_i.print_sort_order( self );
	}
};

#endif // __cplusplus

#endif
