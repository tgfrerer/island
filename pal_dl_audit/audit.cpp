#include <link.h>

#ifdef __cplusplus
extern "C" {
#endif // end __cplusplus

extern "C" unsigned int la_version( unsigned int version );
//extern "C" void la_activity( uintptr_t *cookie, unsigned int flag);

extern "C" unsigned int la_objclose( uintptr_t *cookie );
extern "C" void         la_activity( uintptr_t *cookie, unsigned int flag );
extern "C" unsigned int la_objopen( struct link_map *map, Lmid_t lmid, uintptr_t *cookie );

#ifdef __cplusplus
} // end extern "C"
#endif // end __cplusplus,

#include <iostream>

unsigned int la_version( unsigned int version );

extern "C"
unsigned int
la_version( unsigned int version ) {
	std::cout << "\t AUDIT: loaded autiting interface" << std::endl;
	std::cout << std::flush;
	return version;
}

extern "C"
unsigned int
la_objclose( uintptr_t *cookie ) {
	std::cout << "\t AUDIT: objclose: " << std::hex << cookie << std::endl;
	std::cout << std::flush;
	return 0;
}

extern "C"
void
la_activity( uintptr_t *cookie, unsigned int flag ) {
	printf("\t AUDIT: la_activity(): cookie = %p; flag = %s\n", cookie,
	      (flag == LA_ACT_CONSISTENT) ? "LA_ACT_CONSISTENT" :
	      (flag == LA_ACT_ADD) ?        "LA_ACT_ADD" :
	      (flag == LA_ACT_DELETE) ?     "LA_ACT_DELETE" :
	      "???");
	std::cout << std::flush;
};

extern "C"
unsigned int
la_objopen( struct link_map *map, Lmid_t lmid, uintptr_t *cookie ) {
	printf( "\t AUDIT: la_objopen(): loading \"%s\"; lmid = %s; cookie=%p\n",
	        map->l_name,
	        ( lmid == LM_ID_BASE ) ? "LM_ID_BASE" : ( lmid == LM_ID_NEWLM ) ? "LM_ID_NEWLM" : "???",
	        cookie );
	std::cout << std::flush;
	return LA_FLG_BINDTO | LA_FLG_BINDFROM;
}

//extern "C" void la_activity( uintptr_t *cookie, unsigned int flag){
//	std::cout << "la_activity";
//}
