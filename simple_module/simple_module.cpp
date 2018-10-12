#include "simple_module.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "le_dependency_manager/le_dependency_manager.h"

#include "iostream"
#include "iomanip"
#include <chrono>
#include <thread>
#include <vector>

struct simple_module_o {
	LeDependencyManager manager;
};

// ----------------------------------------------------------------------

static simple_module_o *simple_module_create() {
	auto self = new simple_module_o();
	return self;
}

// ----------------------------------------------------------------------

static void simple_module_destroy( simple_module_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static bool test_dependency_manager() {
	auto dm = LeDependencyManager();

	using namespace le_dependency_manager;

	dm.nextLayer( "layer initial" );
	dm.addResource( 2, eAccessTypeRead );
	dm.addResource( 33, eAccessTypeWrite );

	dm.nextLayer( "layer 0", true );
	dm.addResource( 33, eAccessTypeRead );
	dm.addResource( 12, eAccessTypeWrite );

	dm.nextLayer( "layer 1" );
	dm.addResource( 2, eAccessTypeReadWrite );
	dm.addResource( 1, eAccessTypeWrite );

	dm.nextLayer( "layer 2", true );
	dm.addResource( 2, eAccessTypeWrite );
	dm.addResource( 3, eAccessTypeReadWrite );

	dm.resolveDependencies();
	dm.printSortOrder();

	uint32_t *sort_start;
	size_t    sort_count;
	dm.getLayerSortIndices( &sort_start, &sort_count );
	std::vector<uint32_t> sort_order{sort_start, sort_start + sort_count};

	for ( size_t i = 0; i != sort_order.size(); ++i ) {

		std::cout << "layer #" << i << " : " << sort_order[ i ] << std::endl
		          << std::flush;
	}

	return true;
}

// ----------------------------------------------------------------------

static bool run_tests() {
	bool                       result = true;
	std::array<std::string, 4> test{"Hello world", "", " and another string ...", "weird string\0"};

	for ( size_t i = 0; i != test.size(); ++i ) {
		bool testResult = ( hash_64_fnv1a( test[ i ].c_str() ) == hash_64_fnv1a_const( test[ i ].c_str() ) );
		std::cout << "Test hash_64 #" << i << " :" << ( testResult ? "Passed" : "Failed" ) << std::endl
		          << std::flush;
		result &= testResult;
	}

	for ( size_t i = 0; i != test.size(); ++i ) {
		bool testResult = ( hash_32_fnv1a( test[ i ].c_str() ) == hash_32_fnv1a_const( test[ i ].c_str() ) );
		std::cout << "Test hash_32 #" << i << " :" << ( testResult ? "Passed" : "Failed" ) << std::endl
		          << std::flush;
		result &= testResult;
	}

	test_dependency_manager();

	return result;
}

// ----------------------------------------------------------------------

static void simple_module_update( simple_module_o *self ) {
	static int firstRun = true;

	if ( firstRun ) {
		
        std::cout << "Test results:" << std::endl << std::flush;

		if ( run_tests() ) {
			std::cout << "All Tests passed." << std::flush;
		} else {
			std::cout << "Some Tests failed." << std::flush;
		};

        std::cout << std::endl << std::flush;

		firstRun = false;
	} else {
		using std::chrono_literals::operator""ms;
		std::this_thread::sleep_for( 100ms );
	}
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_simple_module_api( void *api ) {
	auto &simple_module_i = static_cast<simple_module_api *>( api )->simple_module_i;

	simple_module_i.create  = simple_module_create;
	simple_module_i.destroy = simple_module_destroy;
	simple_module_i.update  = simple_module_update;
}
