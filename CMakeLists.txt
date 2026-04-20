cmake_minimum_required(VERSION 3.22)

project(LearnVulkan VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_USE_RESPONSE_FILE_FOR_INCLUDES OFF)
set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES OFF)

set(VULKAN_SDK_ROOT "D:/Program Files/VulkanSDK/1.4.341.1" CACHE PATH "Vulkan SDK root directory")

if(WIN32 AND MINGW AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "You are using a 32-bit MinGW toolchain, but the Vulkan SDK at "
        "'${VULKAN_SDK_ROOT}' only provides 64-bit desktop libraries. "
        "Please switch to a 64-bit MinGW toolchain (x86_64-w64-mingw32)."
    )
endif()

set(ENV{VULKAN_SDK} "${VULKAN_SDK_ROOT}")
list(PREPEND CMAKE_PREFIX_PATH
    "${VULKAN_SDK_ROOT}"
    "${VULKAN_SDK_ROOT}/cmake"
)

find_package(Vulkan REQUIRED)

set(GLM_TARGET "")
find_package(glm CONFIG QUIET)
if(TARGET glm::glm)
    set(GLM_TARGET glm::glm)
    message(STATUS "Using system GLM package")
elseif(EXISTS "${VULKAN_SDK_ROOT}/Include/glm/glm.hpp")
    add_library(glm_from_vulkan_sdk INTERFACE)
    add_library(glm::glm ALIAS glm_from_vulkan_sdk)
    target_include_directories(glm_from_vulkan_sdk INTERFACE
        "${VULKAN_SDK_ROOT}/Include"
    )
    set(GLM_TARGET glm::glm)
    message(STATUS "Using GLM headers from ${VULKAN_SDK_ROOT}/Include")
else()
    message(FATAL_ERROR
        "GLM was not found. Install a glm package, or make sure "
        "'${VULKAN_SDK_ROOT}/Include/glm/glm.hpp' exists."
    )
endif()

set(SDL_TARGET "")
set(SDL_RUNTIME_DLL "")
find_package(SDL3 CONFIG REQUIRED)
set(SDL_TARGET SDL3::SDL3)
set(SDL_RUNTIME_DLL "${VULKAN_SDK_ROOT}/Bin/SDL3.dll")
message(STATUS "Using SDL3 from ${VULKAN_SDK_ROOT}")

add_executable(${PROJECT_NAME}
    src/command_manager.cpp
    src/main.cpp
    src/toy2d.cpp
    src/tool.cpp
    src/context.cpp
    src/swapchain.cpp
    src/render_process.cpp
    src/renderer.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

set(TOY2D_SHADER_SOURCES "shader.vert;shader.frag" CACHE STRING
    "Semicolon-separated GLSL shader files (relative to ${CMAKE_CURRENT_SOURCE_DIR}/shader), e.g. triangle.vert;triangle.frag"
)
option(TOY2D_COMPILE_SHADERS "Compile and copy shaders for runtime" ON)

if(TOY2D_COMPILE_SHADERS AND TOY2D_SHADER_SOURCES STREQUAL "")
    set(TOY2D_SHADER_SOURCES "shader.vert;shader.frag")
endif()

if(TOY2D_SHADER_SOURCES)
    set(GLSLC_EXECUTABLE "${Vulkan_GLSLC_EXECUTABLE}")
    if(NOT GLSLC_EXECUTABLE)
        find_program(GLSLC_EXECUTABLE
            NAMES glslc glslc.exe
            HINTS
                "${VULKAN_SDK_ROOT}/Bin"
                "$ENV{VULKAN_SDK}/Bin"
        )
    endif()

    if(NOT GLSLC_EXECUTABLE)
        message(FATAL_ERROR
            "Could not find glslc. Please install Vulkan SDK or set GLSLC_EXECUTABLE manually."
        )
    endif()

    set(TOY2D_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shader")
    set(TOY2D_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shader_spv")
    set(TOY2D_SHADER_DEPENDS "")
    set(TOY2D_SHADER_OUTPUTS "")

    foreach(shader IN LISTS TOY2D_SHADER_SOURCES)
        if(IS_ABSOLUTE "${shader}")
            set(shader_source "${shader}")
            file(RELATIVE_PATH shader_rel "${TOY2D_SHADER_DIR}" "${shader_source}")
        else()
            set(shader_rel "${shader}")
            set(shader_source "${TOY2D_SHADER_DIR}/${shader_rel}")
        endif()

        if(shader_rel MATCHES "^\\.\\.")
            message(FATAL_ERROR "Shader '${shader}' must be inside ${TOY2D_SHADER_DIR}")
        endif()

        if(NOT EXISTS "${shader_source}")
            message(FATAL_ERROR "Shader source does not exist: ${shader_source}")
        endif()

        list(APPEND TOY2D_SHADER_DEPENDS "${shader_source}")
        list(APPEND TOY2D_SHADER_OUTPUTS "${TOY2D_SHADER_OUTPUT_DIR}/${shader_rel}.spv")
    endforeach()

    string(REPLACE ";" "\\;" TOY2D_SHADER_SOURCES_ARG "${TOY2D_SHADER_SOURCES}")

    add_custom_command(
        OUTPUT ${TOY2D_SHADER_OUTPUTS}
        COMMAND ${CMAKE_COMMAND}
            -DGLSLC_EXECUTABLE=${GLSLC_EXECUTABLE}
            -DSHADER_DIR=${TOY2D_SHADER_DIR}
            -DOUTPUT_DIR=${TOY2D_SHADER_OUTPUT_DIR}
            -DSHADERS=${TOY2D_SHADER_SOURCES_ARG}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_glsl.cmake
        DEPENDS
            ${TOY2D_SHADER_DEPENDS}
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_glsl.cmake"
        COMMENT "Compiling GLSL shaders"
        VERBATIM
    )

    add_custom_target(compile_shaders ALL DEPENDS ${TOY2D_SHADER_OUTPUTS})
    add_dependencies(${PROJECT_NAME} compile_shaders)

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shader"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${TOY2D_SHADER_OUTPUTS}
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shader"
        COMMENT "Copying compiled shaders to runtime directory"
    )
endif()

target_include_directories(${PROJECT_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/toy2d"
)

if(MINGW)
    target_link_libraries(${PROJECT_NAME} PRIVATE mingw32)
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Vulkan::Vulkan
        ${GLM_TARGET}
        ${SDL_TARGET}
)

add_custom_target(run
    COMMAND $<TARGET_FILE:${PROJECT_NAME}>
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY $<TARGET_FILE_DIR:${PROJECT_NAME}>
    USES_TERMINAL
    COMMENT "Build and run ${PROJECT_NAME}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EXISTS "${SDL_RUNTIME_DLL}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endif()
