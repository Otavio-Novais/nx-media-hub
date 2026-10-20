cmake_minimum_required(VERSION 3.15)

set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

# build options (PLATFORM_DESKTOP / PLATFORM_SWITCH, USE_DEKO3D, ...)
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

# OFF hides "Filmes e Séries" (catalog, sources, TMDB/addon settings) while it is in development
option(ENABLE_MOVIES "Movies and series: TMDB catalog and Stremio addons" ON)
option(USE_SHARED_LIB "Whether to use shared libs provided by system" OFF)
cmake_dependent_option(USE_SYSTEM_FMT "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TINYXML2 "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TWEENY "" OFF "NOT USE_SHARED_LIB" ON)

# toolchain
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# project info
project(nx-media-hub)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "5")
set(VERSION_ALTER "2")
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

# libcurl (on Switch: devkitPro's switch-curl with mbedtls)
pkg_search_module(CURL REQUIRED libcurl)
list(APPEND APP_PLATFORM_INCLUDE ${CURL_INCLUDE_DIRS})
link_directories(${CURL_LIBRARY_DIRS})
if (PLATFORM_SWITCH)
    list(APPEND APP_PLATFORM_LIB ${CURL_STATIC_LIBRARIES})
else ()
    list(APPEND APP_PLATFORM_LIB ${CURL_LIBRARIES})
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

# Optional .env (git-ignored) with development defaults used when config.json leaves them empty:
#   tmdbApiKey=<key>
#   addons=<url>[,<url>...]
if (ENABLE_MOVIES AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.env)
    set(CONFIG_DEFINES)
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/.env ENV_TMDB_KEY REGEX "^tmdbApiKey=")
    string(REGEX REPLACE "^tmdbApiKey=[ \t]*([^ \t\r]*).*" "\\1" ENV_TMDB_KEY "${ENV_TMDB_KEY}")
    if (ENV_TMDB_KEY)
        message(STATUS "Using the TMDB API key from .env as default")
        list(APPEND CONFIG_DEFINES "DEFAULT_TMDB_API_KEY=\"${ENV_TMDB_KEY}\"")
    endif ()
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/.env ENV_ADDONS REGEX "^addons=")
    string(REGEX REPLACE "^addons=[ \t]*([^ \t\r]*).*" "\\1" ENV_ADDONS "${ENV_ADDONS}")
    if (ENV_ADDONS)
        # Do not print the URLs: configured addon URLs can carry API keys (ex: Torrentio + debrid)
        string(REPLACE "," ";" ENV_ADDON_LIST "${ENV_ADDONS}")
        list(LENGTH ENV_ADDON_LIST ENV_ADDON_COUNT)
        message(STATUS "Using ${ENV_ADDON_COUNT} addon(s) from .env as default")
        list(APPEND CONFIG_DEFINES "DEFAULT_ADDONS=\"${ENV_ADDONS}\"")
    endif ()
    if (CONFIG_DEFINES)
        set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/source/core/Config.cpp PROPERTIES
            COMPILE_DEFINITIONS "${CONFIG_DEFINES}")
    endif ()
endif ()
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
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}")
if (ENABLE_MOVIES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_MOVIES)
else ()
    message(STATUS "Movies and series disabled (ENABLE_MOVIES=OFF)")
endif ()
target_link_libraries(${PROJECT_NAME} PRIVATE borealis ${APP_PLATFORM_LIB})

# host unit tests for the platform-independent code (ctest --test-dir build-desktop)
if (PLATFORM_DESKTOP)
    enable_testing()
    add_executable(m3u_parser_test tests/M3uParserTest.cpp source/api/M3uParser.cpp)
    set_target_properties(m3u_parser_test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_include_directories(m3u_parser_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(m3u_parser_test PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME m3u_parser COMMAND m3u_parser_test)
endif ()
if (PLATFORM_DESKTOP)
    add_executable(tmdb_parser_test tests/TmdbParserTest.cpp source/api/TmdbParser.cpp)
    set_target_properties(tmdb_parser_test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_include_directories(tmdb_parser_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${BOREALIS_LIBRARY}/include)
    target_compile_definitions(tmdb_parser_test PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME tmdb_parser COMMAND tmdb_parser_test)
endif ()
if (PLATFORM_DESKTOP)
    add_executable(addon_parser_test tests/AddonParserTest.cpp source/api/AddonParser.cpp)
    set_target_properties(addon_parser_test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_include_directories(addon_parser_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${BOREALIS_LIBRARY}/include)
    target_compile_definitions(addon_parser_test PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME addon_parser COMMAND addon_parser_test)
endif ()
if (PLATFORM_DESKTOP)
    add_executable(text_encoding_test tests/TextEncodingTest.cpp source/core/TextEncoding.cpp)
    set_target_properties(text_encoding_test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_include_directories(text_encoding_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME text_encoding COMMAND text_encoding_test)
endif ()
if (PLATFORM_DESKTOP)
    add_executable(library_test tests/LibraryTest.cpp source/core/Library.cpp source/core/Storage.cpp)
    set_target_properties(library_test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_include_directories(library_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${BOREALIS_LIBRARY}/include)
    add_test(NAME library COMMAND library_test)
endif ()
