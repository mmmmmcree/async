execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --config "${CONFIG}"
    --target "${PROBE_TARGET}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
set(diagnostics "${output}\n${error}")
file(WRITE "${BUILD_DIR}/${PROBE_TARGET}.log" "${diagnostics}")
if(result EQUAL 0)
    message(FATAL_ERROR "Module unexpectedly exports ${PRIVATE_NAME}")
endif()
if(NOT diagnostics MATCHES "error[^\n]*${PRIVATE_NAME}|${PRIVATE_NAME}[^\n]*error"
    AND NOT diagnostics MATCHES "error: no member named 'details' in namespace 'lcf::async(::exec)?'")
    message(FATAL_ERROR "Compilation failed for an unrelated reason:\n${diagnostics}")
endif()
