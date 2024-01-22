# How to debug linker and hot-reloading issues on Linux:

You can audit the linker by inserting a shim - see rtld-audit(7)
manpage.

In order to create a shim library, use this recipe:

+ Create a source file with this content:
```c main.c
#define _GNU_SOURCE

#include <stdio.h>
#include <link.h>
unsigned int
la_version( unsigned int version ) {
	printf("\t AUDIT: loaded auditing interface\n" );
    fflush(stdout);
	return version;
}

unsigned int
la_objclose( uintptr_t* cookie ) {
    printf("\t AUDIT: objclose: %p\n", cookie);
    fflush(stdout);
	return 0;
}

 void
la_activity( uintptr_t* cookie, unsigned int flag ) {
	printf( "\t AUDIT: la_activity(): cookie = %p; flag = %s\n", cookie,
	        ( flag == LA_ACT_CONSISTENT ) ? "LA_ACT_CONSISTENT" : ( flag == LA_ACT_ADD )  ? "LA_ACT_ADD"
	                                                          : ( flag == LA_ACT_DELETE ) ? "LA_ACT_DELETE"
	                                                                                      : "???" );
    fflush(stdout);
};

unsigned int
la_objopen( struct link_map* map, Lmid_t lmid, uintptr_t* cookie ) {
	printf( "\t AUDIT: la_objopen(): loading \"%s\"; lmid = %s; cookie=%p\n",
	        map->l_name,
	        ( lmid == LM_ID_BASE ) ? "LM_ID_BASE" : ( lmid == LM_ID_NEWLM ) ? "LM_ID_NEWLM"
	                                                                        : "???",
	        cookie );
    fflush(stdout);
	return LA_FLG_BINDTO | LA_FLG_BINDFROM;
}

char*
la_objsearch( const char* name, uintptr_t* cookie, unsigned int flag ) {
	printf( "\t AUDIT: la_objsearch(): name = %s; cookie = %p", name, cookie );
	printf( "; flag = %s\n",
	        ( flag == LA_SER_ORIG ) ? "LA_SER_ORIG" : ( flag == LA_SER_LIBPATH ) ? "LA_SER_LIBPATH"
	                                              : ( flag == LA_SER_RUNPATH )   ? "LA_SER_RUNPATH"
	                                              : ( flag == LA_SER_DEFAULT )   ? "LA_SER_DEFAULT"
	                                              : ( flag == LA_SER_CONFIG )    ? "LA_SER_CONFIG"
	                                              : ( flag == LA_SER_SECURE )    ? "LA_SER_SECURE"
	                                                                             : "???" );

    fflush(stdout);
	return (char*)( name );
}
```

+ Compile this to a .so library (using cmake)
```CMake
    cmake_minimum_required(VERSION 3.5)
    project(audit LANGUAGES C)
    # include(GNUInstallDirs)
    add_library(audit SHARED main.c)
```

+ This will generate a file `libaudit.so` - use this file (you may
  have to replace `PATH_TO_LIBAUDIT` with the correct path so that
  libaudit.so can be found) when starting your application
  from the command line:

    EXPORT LD_AUDIT=./$PATH_TO_LIBAUDIT/libaudit.so $MYAPP

