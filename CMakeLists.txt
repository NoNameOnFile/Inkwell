cmake_minimum_required(VERSION 3.20)
project(inkwell VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    # Sources are UTF-8. Qt also requires a real __cplusplus value on MSVC.
    add_compile_options(/utf-8)
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>"
        "$<$<COMPILE_LANGUAGE:CXX>:/permissive->"
    )
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(/external:anglebrackets /external:W0)
    endif()
endif()

function(inkwell_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wextra>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wpedantic>
    )
endfunction()

add_library(inkwell_core
    src/utf8.cpp
    src/text.cpp
    src/editor.cpp
    src/layout.cpp
    src/spell.cpp
    src/grammar.cpp
    src/lint.cpp
    src/markup.cpp
    src/plugin.cpp
)
target_include_directories(inkwell_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(CMAKE_DL_LIBS)
    target_link_libraries(inkwell_core PUBLIC ${CMAKE_DL_LIBS})
endif()
inkwell_warnings(inkwell_core)

add_library(inkwell_sample_plugin MODULE plugins/sample.cpp)
target_include_directories(inkwell_sample_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(inkwell_sample_plugin PROPERTIES PREFIX "" OUTPUT_NAME sample)
inkwell_warnings(inkwell_sample_plugin)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    message(STATUS "zlib not found; fetching zlib 1.4")
    # A static library keeps the Windows exe from needing a zlib DLL beside it.
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "Build zlib tests" FORCE)
    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "Build zlib shared library" FORCE)
    set(ZLIB_BUILD_STATIC ON CACHE BOOL "Build zlib static library" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "Install zlib" FORCE)
    FetchContent_Declare(
        zlib
        URL
            https://github.com/madler/zlib/releases/download/v1.4/zlib-1.4.tar.gz
            https://zlib.net/zlib-1.4.tar.gz
        URL_HASH SHA256=02dd159783677a749c04adbdd02b55b8a2bcc7e054f51b76b549199a916ad584
    )
    FetchContent_MakeAvailable(zlib)
    if(NOT TARGET ZLIB::ZLIB AND TARGET zlibstatic)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
endif()
if(NOT TARGET ZLIB::ZLIB)
    message(FATAL_ERROR
        "zlib is required. Install it and set CMAKE_PREFIX_PATH, or allow CMake to download zlib 1.4.")
endif()
qt_standard_project_setup()

qt_add_library(inkwell_gui STATIC
    src/gui/editorview.hpp
    src/gui/editorview.cpp
    src/gui/drawer.hpp
    src/gui/drawer.cpp
    src/gui/book.hpp
    src/gui/book.cpp
    src/gui/details.hpp
    src/gui/details.cpp
    src/gui/pagesettings.hpp
    src/gui/pagesettings.cpp
    src/gui/theme.hpp
    src/gui/theme.cpp
    src/gui/export.hpp
    src/gui/export.cpp
    src/gui/exportpage.cpp
    src/gui/mainwindow.hpp
    src/gui/mainwindow.cpp
)
target_include_directories(inkwell_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/gui)
target_link_libraries(inkwell_gui PUBLIC inkwell_core Qt6::Widgets ZLIB::ZLIB)
inkwell_warnings(inkwell_gui)

qt_add_executable(inkwell src/gui/main.cpp)
target_link_libraries(inkwell PRIVATE inkwell_gui)
inkwell_warnings(inkwell)

if(UNIX)
    add_executable(inkwell-tui
        src/main.cpp
        src/term.cpp
        src/app.cpp
    )
    target_link_libraries(inkwell-tui PRIVATE inkwell_core)
    inkwell_warnings(inkwell-tui)
endif()

file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/inkwell_sample_plugin_path.h"
    CONTENT "#define INKWELL_SAMPLE_PLUGIN R\"inkwell($<TARGET_FILE:inkwell_sample_plugin>)inkwell\"\n"
)

add_executable(inkwell_tests tests/tests.cpp)
target_link_libraries(inkwell_tests PRIVATE inkwell_core)
target_include_directories(inkwell_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>")
inkwell_warnings(inkwell_tests)
add_dependencies(inkwell_tests inkwell_sample_plugin)

qt_add_executable(inkwell_gui_tests tests/gui_smoke.cpp)
target_link_libraries(inkwell_gui_tests PRIVATE inkwell_gui)
inkwell_warnings(inkwell_gui_tests)
target_compile_definitions(inkwell_gui_tests PRIVATE INKWELL_PREVIEW="${CMAKE_BINARY_DIR}/gui-preview.png")

enable_testing()
add_test(NAME inkwell_tests COMMAND inkwell_tests)
add_test(NAME inkwell_gui_tests COMMAND inkwell_gui_tests)
set_tests_properties(inkwell_gui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

include(GNUInstallDirs)
install(TARGETS inkwell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX)
    install(TARGETS inkwell-tui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES dist/org.inkwell.Inkwell.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES dist/icons/hicolor/128x128/apps/org.inkwell.Inkwell.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps)
    install(FILES dist/icons/hicolor/256x256/apps/org.inkwell.Inkwell.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    install(FILES dist/icons/hicolor/512x512/apps/org.inkwell.Inkwell.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)
    install(FILES dist/org.inkwell.Inkwell.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
endif()

qt_add_resources(inkwell inkwell_icons
    PREFIX "/inkwell"
    BASE "dist/icons/hicolor/256x256/apps"
    FILES dist/icons/hicolor/256x256/apps/org.inkwell.Inkwell.png
)
