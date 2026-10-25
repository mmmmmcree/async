include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")
set(package_file "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}")
set(export_name ${PROJECT_NAME}Targets)

set(install_modules)
set(package_architecture)
if(lib_build_type STREQUAL "INTERFACE")
    set(package_architecture ARCH_INDEPENDENT)
endif()
if(${lib_option_prefix}_USE_MODULES)
    set(install_modules FILE_SET CXX_MODULES
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}/modules")
endif()
install(TARGETS ${lib_target} EXPORT ${export_name}
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    ${install_modules})
install(EXPORT ${export_name}
    NAMESPACE ${lib_namespace}::
    CXX_MODULES_DIRECTORY modules
    DESTINATION "${package_dir}")

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/package-config.cmake.in"
    "${package_file}Config.cmake"
    INSTALL_DESTINATION "${package_dir}")
write_basic_package_version_file(
    "${package_file}ConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion
    ${package_architecture})
install(FILES
    "${package_file}Config.cmake"
    "${package_file}ConfigVersion.cmake"
    DESTINATION "${package_dir}")
