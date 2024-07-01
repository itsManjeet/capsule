file(GLOB_RECURSE ModuleFiles "*.src")

install(FILES ${ModuleFiles}
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/SrcLang)