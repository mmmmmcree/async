# stdexec is a temporary implementation provider, selected by capability rather than C++ version.
include("${CMAKE_CURRENT_LIST_DIR}/checks/std_execution.cmake")
async_check_std_execution(async_has_native_execution)
if(async_has_native_execution)
    set(async_execution_provider std)
    target_compile_features(${lib_target} ${lib_scope} cxx_std_${CMAKE_CXX_STANDARD})
    target_compile_definitions(${lib_target} ${lib_scope} LCF_ASYNC_USE_STD_EXECUTION=1)
else()
    set(async_execution_provider stdexec)
    lcf_dependency(
        NAME stdexec TARGET STDEXEC::stdexec PACKAGE STDEXEC
        GIT https://github.com/NVIDIA/stdexec.git
        GIT_TAG dfc8c95718e8c66ff5496bc3b185a9162c93821e
        INCLUDE_DIRECTORIES include
        FIND_PACKAGES Threads LINK_LIBRARIES Threads::Threads
        COMPILE_FEATURES cxx_std_20)
    target_compile_definitions(${lib_target} ${lib_scope} LCF_ASYNC_USE_STD_EXECUTION=0)
    target_compile_options(${lib_target} ${lib_scope}
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:externConstexpr;/bigobj>")
endif()
message(STATUS "${lib_name}: execution provider = ${async_execution_provider}")
