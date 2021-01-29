#ifndef GUARD_bitonic_merge_sort_example_app_H
#define GUARD_bitonic_merge_sort_example_app_H
#endif

#include "le_core/le_core.h"

struct bitonic_merge_sort_example_app_o;

// clang-format off
struct bitonic_merge_sort_example_app_api {

	struct bitonic_merge_sort_example_app_interface_t {
		bitonic_merge_sort_example_app_o * ( *create               )();
		void         ( *destroy                  )( bitonic_merge_sort_example_app_o *self );
		bool         ( *update                   )( bitonic_merge_sort_example_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	bitonic_merge_sort_example_app_interface_t bitonic_merge_sort_example_app_i;
};
// clang-format on

LE_MODULE( bitonic_merge_sort_example_app );
LE_MODULE_LOAD_DEFAULT( bitonic_merge_sort_example_app );

#ifdef __cplusplus

namespace bitonic_merge_sort_example_app {
static const auto &api       = bitonic_merge_sort_example_app_api_i;
static const auto &bitonic_merge_sort_example_app_i = api -> bitonic_merge_sort_example_app_i;
} // namespace bitonic_merge_sort_example_app

class BitonicMergeSortExampleApp : NoCopy, NoMove {

	bitonic_merge_sort_example_app_o *self;

  public:
	BitonicMergeSortExampleApp()
	    : self( bitonic_merge_sort_example_app::bitonic_merge_sort_example_app_i.create() ) {
	}

	bool update() {
		return bitonic_merge_sort_example_app::bitonic_merge_sort_example_app_i.update( self );
	}

	~BitonicMergeSortExampleApp() {
		bitonic_merge_sort_example_app::bitonic_merge_sort_example_app_i.destroy( self );
	}

	static void initialize() {
		bitonic_merge_sort_example_app::bitonic_merge_sort_example_app_i.initialize();
	}

	static void terminate() {
		bitonic_merge_sort_example_app::bitonic_merge_sort_example_app_i.terminate();
	}
};

#endif
