include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
foreach(backend asio stdexec standalone)
    set(async_package_${backend} OFF)
    if(TARGET async_${backend})
        set(async_package_${backend} ON)
    endif()
endforeach()
configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/LCFAsyncConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LCFAsyncConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LCFAsync")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/LCFAsyncConfigVersion.cmake"
    VERSION 0.1.0 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/LCFAsyncConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LCFAsyncConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LCFAsync")
