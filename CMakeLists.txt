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
    src/main.cpp
    src/toy2d.cpp
    src/context.cpp
    src/swapchain.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

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
