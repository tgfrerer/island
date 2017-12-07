if (${PLUGINS_DYNAMIC})
    add_library(pal_dl_audit SHARED audit.cpp)
endif()
