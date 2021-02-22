#include "test_file_watch_app.h"

#include "le_file_watcher/le_file_watcher.h"
#include "le_log/le_log.h"

struct test_file_watch_app_o {
	le::FileWatcher file_watcher;
	LeLog           log{};
	bool            quit = false;
};

typedef test_file_watch_app_o app_o;

// ----------------------------------------------------------------------

static void app_initialize(){};

// ----------------------------------------------------------------------

static void app_terminate(){};

// ----------------------------------------------------------------------

static bool file_callback( const char *path, void *user_data ) {
	auto app = static_cast<test_file_watch_app_o *>( user_data );
	app->log.info( "File modified '%s', will exit now", path );
	app->quit = true;
	return true;
}

static bool directory_callback( le::FileWatcher::Event event, const char *path, void *user_data ) {
	auto        app    = static_cast<test_file_watch_app_o *>( user_data );
	const char *action = nullptr;
	switch ( event ) {
	case le_file_watcher_api::Event::FILE_CREATED:
		action = "file created";
		break;
	case le_file_watcher_api::Event::FILE_DELETED:
		action = "file deleted";
		break;
	case le_file_watcher_api::Event::FILE_MODIFIED:
		action = "file modified";
		break;
	case le_file_watcher_api::Event::FILE_MOVED:
		action = "file moved";
		break;
	case le_file_watcher_api::Event::DIRECTORY_CREATED:
		action = "folder created";
		break;
	case le_file_watcher_api::Event::DIRECTORY_DELETED:
		action = "folder deleted";
		break;
	case le_file_watcher_api::Event::DIRECTORY_MOVED:
		action = "folder moved";
		break;
	}
	app->log.info( "%s %s", action, path );
	return true;
}

// ----------------------------------------------------------------------

static test_file_watch_app_o *test_file_watch_app_create() {
	auto app = new ( test_file_watch_app_o );
	app->log.info( "App Created" );
	app->file_watcher.watch_file( "./local_resources/file.txt", file_callback, app );
	app->file_watcher.watch_directory( "./local_resources", directory_callback, app );
	return app;
}

// ----------------------------------------------------------------------

static bool test_file_watch_app_update( test_file_watch_app_o *self ) {
	self->file_watcher.poll();
	return !self->quit; // keep app alive
}

// ----------------------------------------------------------------------

static void test_file_watch_app_destroy( test_file_watch_app_o *self ) {
	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( test_file_watch_app, api ) {

	auto  test_file_watch_app_api_i = static_cast<test_file_watch_app_api *>( api );
	auto &test_file_watch_app_i     = test_file_watch_app_api_i->test_file_watch_app_i;

	test_file_watch_app_i.initialize = app_initialize;
	test_file_watch_app_i.terminate  = app_terminate;

	test_file_watch_app_i.create  = test_file_watch_app_create;
	test_file_watch_app_i.destroy = test_file_watch_app_destroy;
	test_file_watch_app_i.update  = test_file_watch_app_update;
}
