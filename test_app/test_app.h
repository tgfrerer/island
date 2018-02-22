#ifndef GUARD_TEST_APP_H
#define GUARD_TEST_APP_H
#endif

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_test_app_api( void *api );

struct test_app_o;

struct test_app_api {

	static constexpr auto id       = "test_app";
	static constexpr auto pRegFun  = register_test_app_api;

	struct test_app_interface_t{
		test_app_o *( *create )();
		void ( *destroy )( test_app_o *self );
		bool ( *update )( test_app_o *self );

		void (* initialize)(); // static methods
		void (* terminate)();   // static methods
	};

	test_app_interface_t test_app_i;

};

#ifdef __cplusplus


class TestApp : NoCopy, NoMove {
	const test_app_api &                      testAppApiI = *Registry::getApi<test_app_api>();
	const test_app_api::test_app_interface_t &testAppI    = testAppApiI.test_app_i;

	test_app_o* self;

public:

	TestApp()
	    : self(testAppI.create())
	{}

	bool update(){
		return testAppI.update(self);
	}

	~TestApp(){
		testAppI.destroy(self);
	}

	static void initialize(){
		static auto api = Registry::getApi<test_app_api>()->test_app_i;
		api.initialize();
	}

	static void terminate(){
		static auto api = Registry::getApi<test_app_api>()->test_app_i;
		api.terminate();
	}

};



} // extern "C"
#endif
