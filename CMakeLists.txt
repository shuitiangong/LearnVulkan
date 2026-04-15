cmake_minimum_required(VERSION 3.22)

project(LearnVulkan VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(VULKAN_SDK_ROOT "D:/Program Files/VulkanSDK/1.4.341.1" CACHE PATH "Vulkan SDK root directory")
set(SDL_PREFER_MAJOR_VERSION "3" CACHE STRING "Preferred SDL major version: 3 or 2")
set_property(CACHE SDL_PREFER_MAJOR_VERSION PROPERTY STRINGS 3 2)

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

set(SDL_TARGET "")
set(SDL_RUNTIME_DLL "")
set(SDL_MAIN_TARGET "")

if(SDL_PREFER_MAJOR_VERSION STREQUAL "2")
    find_package(SDL2 CONFIG REQUIRED)
    set(SDL_TARGET SDL2::SDL2)
    set(SDL_RUNTIME_DLL "${VULKAN_SDK_ROOT}/Bin/SDL2.dll")
    if(TARGET SDL2::SDL2main)
        set(SDL_MAIN_TARGET SDL2::SDL2main)
    endif()
    message(STATUS "Using SDL2 from ${VULKAN_SDK_ROOT}")
else()
    find_package(SDL3 CONFIG QUIET)
    if(SDL3_FOUND)
        set(SDL_TARGET SDL3::SDL3)
        set(SDL_RUNTIME_DLL "${VULKAN_SDK_ROOT}/Bin/SDL3.dll")
        message(STATUS "Using SDL3 from ${VULKAN_SDK_ROOT}")
    else()
        find_package(SDL2 CONFIG REQUIRED)
        set(SDL_TARGET SDL2::SDL2)
        set(SDL_RUNTIME_DLL "${VULKAN_SDK_ROOT}/Bin/SDL2.dll")
        if(TARGET SDL2::SDL2main)
            set(SDL_MAIN_TARGET SDL2::SDL2main)
        endif()
        message(STATUS "SDL3 not found, falling back to SDL2 from ${VULKAN_SDK_ROOT}")
    endif()
endif()

add_executable(${PROJECT_NAME}
    src/main.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(MINGW)
    target_link_libraries(${PROJECT_NAME} PRIVATE mingw32)
endif()

if(SDL_MAIN_TARGET)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL_MAIN_TARGET})
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Vulkan::Vulkan
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
