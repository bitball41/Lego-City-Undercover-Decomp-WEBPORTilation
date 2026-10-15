cmake_minimum_required(VERSION 3.13)
project(lcuswitch CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Use response files when linking objects because of the large number of source files
set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_OBJECTS ON)
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    add_compile_options(-fdiagnostics-color=always)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_compile_options(-fcolor-diagnostics)
    # Required to silence "unused argument" warnings for -stdlib=libc++ when using distcc
    add_compile_options(-Wno-unused-command-line-argument)
    # Required to fix source paths in debug info when using distcc
    add_compile_options(-fdebug-prefix-map=/tmp=${CMAKE_CURRENT_BINARY_DIR})
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SOURCES_GAME ${PROJECT_SOURCE_DIR}/src/*.cpp ${PROJECT_SOURCE_DIR}/src/*.c ${PROJECT_SOURCE_DIR}/src/*.hpp ${PROJECT_SOURCE_DIR}/src/*.h)

add_executable(lcuswitch ${SOURCES_GAME})

# Writing sources to a file so build.py script knows the sources
set(SOURCES_OUTPUT_FILE "${CMAKE_BINARY_DIR}/.build_sources")
file(WRITE ${SOURCES_OUTPUT_FILE} "")

get_target_property(SOURCES_COMBINED lcuswitch SOURCES)

foreach (SOURCE ${SOURCES_COMBINED})
    file(APPEND ${SOURCES_OUTPUT_FILE} "${SOURCE}\n")
endforeach ()

set(NN_SDK 1.3.1)
set(NN_SDK 1.3.1)
set(NN_SDK_TYPE "Release")

set(LCU_PLATFORM "switch" CACHE STRING "Build target: switch, host, or wasm")

include_directories(lib)
if(LCU_PLATFORM STREQUAL "switch")
    include_directories(lib/aarch64)
endif()

target_include_directories(lcuswitch PRIVATE src)
target_include_directories(lcuswitch PUBLIC lib/nnheaders/include)
target_include_directories(lcuswitch PUBLIC lib)

target_compile_definitions(lcuswitch PRIVATE NON_MATCHING)
target_compile_definitions(lcuswitch PRIVATE MATCHING_HACK)
target_compile_options(lcuswitch PRIVATE -fno-exceptions)
target_compile_options(lcuswitch PRIVATE -fno-strict-aliasing)
target_compile_options(lcuswitch PRIVATE -fsigned-char)
target_compile_options(lcuswitch PRIVATE -Wall -Wextra -Wdeprecated)
target_compile_options(lcuswitch PRIVATE -Wextra-semi)
# target_compile_options(lcuswitch PRIVATE -fno-inline -fno-inline-functions)
# target_compile_options(lcuswitch PRIVATE -fno-builtin)
target_compile_options(lcuswitch PRIVATE -fPIC)

# Disable some warnings that are unavoidable
target_compile_options(lcuswitch PRIVATE -Wno-unused-parameter -Wno-unused-private-field -Wno-unused-variable)
target_compile_options(lcuswitch PRIVATE -Wno-invalid-offsetof)
target_compile_options(lcuswitch PRIVATE -Wno-deprecated)
# target_compile_options(mcswitch PRIVATE -Wno-delete-incomplete) # clangd warns about this one in MultiPlayerLevel.cpp
target_compile_options(lcuswitch PRIVATE -Wno-sign-compare)

add_definitions(-D SAFETY_CHECKS)

if(NOT LCU_PLATFORM STREQUAL "switch")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(lcuswitch PRIVATE -Wno-unknown-pragmas -Wno-unused-but-set-variable)
    endif()
    target_compile_definitions(lcuswitch PRIVATE LCU_HOST=1)
    target_include_directories(lcuswitch BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/host/include
        ${CMAKE_CURRENT_SOURCE_DIR}/port/include)
    if(LCU_PLATFORM STREQUAL "wasm")
        target_compile_options(lcuswitch PRIVATE "SHELL:-sUSE_ZLIB=1")
        target_link_options(lcuswitch PRIVATE "SHELL:-sUSE_ZLIB=1")
    else()
        find_package(ZLIB REQUIRED)
        target_link_libraries(lcuswitch PRIVATE ZLIB::ZLIB)
    endif()
    file(GLOB_RECURSE LCU_HOST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/port/*.cpp)
    target_sources(lcuswitch PRIVATE ${LCU_HOST_SOURCES})
    if(LCU_PLATFORM STREQUAL "wasm")
        set_target_properties(lcuswitch PROPERTIES OUTPUT_NAME "lcu" SUFFIX ".js")
        target_link_options(lcuswitch PRIVATE
            "SHELL:-sALLOW_MEMORY_GROWTH=1"
            "SHELL:-sINITIAL_MEMORY=67108864"
            "SHELL:-sENVIRONMENT=web,node"
            "SHELL:-sMODULARIZE=1"
            "SHELL:-sEXPORT_NAME=createLcuModule"
            "SHELL:-sINVOKE_RUN=0"
            "SHELL:-sNO_EXIT_RUNTIME=1"
            "SHELL:-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8,UTF8ToString,stringToUTF8,lengthBytesUTF8"
            "SHELL:-sEXPORTED_FUNCTIONS=_host_boot,_host_frame,_host_set_pad,_host_set_language,_host_framebuffer,_host_fb_width,_host_fb_height,_host_submit_asset,_host_clear_assets,_host_stud_count,_host_get_status,_host_mount_dir,_malloc,_free"
            "SHELL:-sSTACK_SIZE=1048576")
        add_custom_command(TARGET lcuswitch POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html
                ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.js
                ${CMAKE_CURRENT_SOURCE_DIR}/web/archive.js
                ${CMAKE_CURRENT_SOURCE_DIR}/web/loader.js
                ${CMAKE_CURRENT_SOURCE_DIR}/web/play.html
                ${CMAKE_CURRENT_SOURCE_DIR}/web/serve.py
                $<TARGET_FILE_DIR:lcuswitch>)
    endif()
endif()
