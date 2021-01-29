#include "bitonic_merge_sort_example_app/bitonic_merge_sort_example_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	BitonicMergeSortExampleApp::initialize();

	{
		// We instantiate BitonicMergeSortExampleApp in its own scope - so that
		// it will be destroyed before BitonicMergeSortExampleApp::terminate
		// is called.

		BitonicMergeSortExampleApp BitonicMergeSortExampleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = BitonicMergeSortExampleApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last BitonicMergeSortExampleApp is destroyed
	BitonicMergeSortExampleApp::terminate();

	return 0;
}
