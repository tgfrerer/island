#ifndef GUARD_PAL_WINDOW_H
#define GUARD_PAL_WINDOW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void register_pal_window_api( void *api );

struct pal_window_o;

struct pal_window_api {
	static constexpr auto id      = "pal_window";
	static constexpr auto pRegFun = register_pal_window_api;

	struct window_interface_t {
		pal_window_o *( *create )();
		void ( *destroy )( pal_window_o *obj );
		bool ( *should_close )( pal_window_o *obj );
		void ( *update )( pal_window_o *obj );
		void ( *draw )( pal_window_o *obj );
	} window_i;
};

#ifdef __cplusplus
} // extern "C"

#include "registry/ApiRegistry.hpp"

namespace pal {

class Window {
	pal_window_api::window_interface_t const *mInterface;
	pal_window_o *                            mObj;

  private:
	// Note this class disables copy constructor and copy assignment operator,

	// Also, this class disables move operators, as a move will trigger the
	// destructor, and we hijack the destructor to print to the log.

	// copy assignment operator
	Window &operator=( const Window &rhs ) = delete;

	// copy constructor
	Window( const Window &rhs ) = delete;

	// move assignment operator
	Window &operator=( Window &&rhs ) = delete;

	// move constructor
	Window( const Window &&rhs ) = delete;

  public:
	// default constructor
	Window()
	    : mInterface( &Registry::getApi<pal_window_api>()->window_i )
	    , mObj( mInterface->create() ) {
	}

	~Window() {
		mInterface->destroy( mObj );
	}

	bool shouldClose() {
		return mInterface->should_close( mObj );
	}

	void update() {
		mInterface->update( mObj );
	}

	void draw(){
		mInterface->draw(mObj);
	}
};

} // namespace pal

#endif // __cplusplus

#endif
