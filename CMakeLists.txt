cmake_minimum_required(VERSION 3.15)

set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

# build options (PLATFORM_DESKTOP / PLATFORM_SWITCH, USE_DEKO3D, ...)
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

option(USE_SHARED_LIB "Whether to use shared libs provided by system" OFF)
cmake_dependent_option(USE_SYSTEM_FMT "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TINYXML2 "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TWEENY "" OFF "NOT USE_SHARED_LIB" ON)

# toolchain
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# project info
project(nx-media-hub)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "1")
set(VERSION_ALTER "0")
set(PROJECT_AUTHOR "otavio")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)

if (USE_LIBROMFS)
    add_libromfs(${PROJECT_NAME} ${PROJECT_RESOURCES})
endif ()

add_subdirectory(${BOREALIS_LIBRARY} borealis)

find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

# libmpv (on Switch: the static package prebuilt by wiliwili, see scripts/build_switch.sh)
find_package(PkgConfig REQUIRED)
pkg_search_module(MPV REQUIRED mpv)
message(STATUS "Found libmpv: ${MPV_VERSION}")
list(APPEND APP_PLATFORM_INCLUDE ${MPV_INCLUDE_DIRS})
link_directories(${MPV_LIBRARY_DIRS})
if (PLATFORM_SWITCH)
    list(APPEND APP_PLATFORM_LIB ${MPV_STATIC_LIBRARIES})
else ()
    list(APPEND APP_PLATFORM_LIB ${MPV_LIBRARIES})
endif ()

# sources
file(GLOB_RECURSE MAIN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
if (PLATFORM_SWITCH)
    set(SWITCH_WRAPPER ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
    # DEBUG enables nxlinkStdio(): logs reach `nxlink -s` (no-op when not launched by nxlink)
    set_source_files_properties(${SWITCH_WRAPPER} PROPERTIES COMPILE_DEFINITIONS DEBUG)
    list(APPEND MAIN_SRC ${SWITCH_WRAPPER})
endif ()

program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# release files
if (PLATFORM_DESKTOP)
    if (NOT USE_LIBROMFS)
        add_custom_target(${PROJECT_NAME}.data
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_CURRENT_BINARY_DIR}/resources)
        add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}.data)
    endif ()
elseif (PLATFORM_SWITCH)
    add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
        COMMAND ${NX_NACPTOOL_EXE} --create "NX Media Hub" "${PROJECT_AUTHOR}" "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
        # Borealis defaults to the zh-Hans shared font on Switch, whose wide Latin glyphs (pinyin í, á...)
        # break pt-BR text; romfs:/font/font.ttf replaces it as the primary font
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/resources/font
        COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_RESOURCES}/font/switch_font.ttf ${CMAKE_BINARY_DIR}/resources/font/font.ttf
        COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
    )
endif ()

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${APP_PLATFORM_INCLUDE})
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections ${APP_PLATFORM_OPTION})
target_link_libraries(${PROJECT_NAME} PRIVATE borealis ${APP_PLATFORM_LIB})
