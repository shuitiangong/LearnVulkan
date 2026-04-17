if(NOT DEFINED GLSLC_EXECUTABLE OR GLSLC_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "GLSLC_EXECUTABLE is not set.")
endif()

if(NOT DEFINED SHADER_DIR OR SHADER_DIR STREQUAL "")
    message(FATAL_ERROR "SHADER_DIR is not set.")
endif()

if(NOT DEFINED OUTPUT_DIR OR OUTPUT_DIR STREQUAL "")
    message(FATAL_ERROR "OUTPUT_DIR is not set.")
endif()

if(NOT DEFINED SHADERS OR SHADERS STREQUAL "")
    message(FATAL_ERROR "SHADERS is empty. Provide a semicolon-separated shader list.")
endif()

get_filename_component(shader_dir_abs "${SHADER_DIR}" ABSOLUTE)
get_filename_component(output_dir_abs "${OUTPUT_DIR}" ABSOLUTE)

if(NOT EXISTS "${shader_dir_abs}")
    message(FATAL_ERROR "Shader source directory does not exist: ${shader_dir_abs}")
endif()

file(MAKE_DIRECTORY "${output_dir_abs}")

foreach(shader IN LISTS SHADERS)
    if(IS_ABSOLUTE "${shader}")
        set(shader_src "${shader}")
    else()
        set(shader_src "${shader_dir_abs}/${shader}")
    endif()

    if(NOT EXISTS "${shader_src}")
        message(FATAL_ERROR "Shader source not found: ${shader_src}")
    endif()

    if(IS_ABSOLUTE "${shader}")
        file(RELATIVE_PATH shader_rel "${shader_dir_abs}" "${shader_src}")
    else()
        set(shader_rel "${shader}")
    endif()

    if(shader_rel MATCHES "^\\.\\.")
        message(FATAL_ERROR "Shader must be inside SHADER_DIR: ${shader_src}")
    endif()

    set(shader_out "${output_dir_abs}/${shader_rel}.spv")
    get_filename_component(shader_out_dir "${shader_out}" DIRECTORY)
    file(MAKE_DIRECTORY "${shader_out_dir}")

    execute_process(
        COMMAND "${GLSLC_EXECUTABLE}" "${shader_src}" -o "${shader_out}"
        RESULT_VARIABLE glslc_result
        OUTPUT_VARIABLE glslc_stdout
        ERROR_VARIABLE glslc_stderr
    )

    if(NOT glslc_result EQUAL 0)
        message(FATAL_ERROR
            "glslc failed for ${shader_src}\n"
            "stdout:\n${glslc_stdout}\n"
            "stderr:\n${glslc_stderr}"
        )
    endif()

    message(STATUS "Compiled shader: ${shader_rel} -> ${shader_out}")
endforeach()
