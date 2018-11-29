#include "test_dependency/test_dependency.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	TestDependency::initialize();

	{
		// We instantiate TestDependency in its own scope - so that
		// it will be destroyed before TestDependency::terminate
		// is called.

		TestDependency TestDependency{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = TestDependency.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last TestDependency is destroyed
	TestDependency::terminate();

	return 0;
}
