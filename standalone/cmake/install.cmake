include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/lcf_async/standalone")
if(LCF_ASYNC_USE_MODULES)
    install(TARGETS async_standalone EXPORT LCFAsyncStandaloneTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>"
        FILE_SET CXX_MODULES DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LCFAsync/modules/standalone")
else()
    install(TARGETS async_standalone EXPORT LCFAsyncStandaloneTargets)
endif()
install(EXPORT LCFAsyncStandaloneTargets NAMESPACE lcf:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LCFAsync"
    CXX_MODULES_DIRECTORY cxx-modules-standalone)
