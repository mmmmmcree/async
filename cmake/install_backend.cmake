include_guard(GLOBAL)
include(GNUInstallDirs)

function(async_install_backend backend export_set)
    install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/lcf_async/${backend}")
    if(LCF_ASYNC_USE_MODULES)
        install(TARGETS async_${backend} EXPORT ${export_set}
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>"
            FILE_SET CXX_MODULES DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LCFAsync/modules/${backend}")
    else()
        install(TARGETS async_${backend} EXPORT ${export_set})
    endif()
    install(EXPORT ${export_set} NAMESPACE lcf:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LCFAsync"
        CXX_MODULES_DIRECTORY cxx-modules-${backend})
endfunction()
