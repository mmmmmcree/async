include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/install_backend.cmake")
if(LCF_ASYNC_USE_MODULES)
    install(TARGETS async_asio_runtime EXPORT LCFAsyncAsioTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>")
endif()
async_install_backend(asio LCFAsyncAsioTargets)
