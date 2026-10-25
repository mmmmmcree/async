# Project options
option(${lib_option_prefix}_INSTALL "Install ${lib_name} and its CMake package" ${PROJECT_IS_TOP_LEVEL})
option(${lib_option_prefix}_USE_MODULES "Build the ${lib_name} named module" OFF)

# Development entry point; component options are local to tests/.
option(${lib_option_prefix}_BUILD_TESTS "Enable ${lib_name} development targets" ${PROJECT_IS_TOP_LEVEL})
