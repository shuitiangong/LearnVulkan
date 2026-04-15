set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MINGW_ROOT)

set(MINGW_ROOT "C:/mingw64" CACHE PATH "Root directory of a 64-bit MinGW installation")

if(NOT MINGW_ROOT)
    message(FATAL_ERROR
        "MINGW_ROOT is not set. Configure with a 64-bit MinGW path, for example: "
        "-DMINGW_ROOT=C:/mingw64"
    )
endif()

set(_mingw_bin "${MINGW_ROOT}/bin")
set(_gcc "${_mingw_bin}/gcc.exe")
set(_gxx "${_mingw_bin}/g++.exe")
set(_windres "${_mingw_bin}/windres.exe")
set(_make "${_mingw_bin}/mingw32-make.exe")

foreach(_tool IN ITEMS _gcc _gxx _make)
    if(NOT EXISTS "${${_tool}}")
        message(FATAL_ERROR "Required MinGW tool was not found: ${${_tool}}")
    endif()
endforeach()

set(CMAKE_C_COMPILER "${_gcc}" CACHE FILEPATH "C compiler" FORCE)
set(CMAKE_CXX_COMPILER "${_gxx}" CACHE FILEPATH "C++ compiler" FORCE)
set(CMAKE_MAKE_PROGRAM "${_make}" CACHE FILEPATH "Make program" FORCE)

if(EXISTS "${_windres}")
    set(CMAKE_RC_COMPILER "${_windres}" CACHE FILEPATH "RC compiler" FORCE)
endif()

list(PREPEND CMAKE_PROGRAM_PATH "${_mingw_bin}")
