cmake_minimum_required(VERSION 3.20)
project(inkwell VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
target_link_libraries(inkwell_core PUBLIC ${CMAKE_DL_LIBS})
target_compile_options(inkwell_core PRIVATE -Wall -Wextra -Wpedantic)

add_library(inkwell_sample_plugin MODULE plugins/sample.cpp)
target_include_directories(inkwell_sample_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(inkwell_sample_plugin PROPERTIES PREFIX "" OUTPUT_NAME sample)
target_compile_options(inkwell_sample_plugin PRIVATE -Wall -Wextra -Wpedantic)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(ZLIB REQUIRED)
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
target_compile_options(inkwell_gui PRIVATE -Wall -Wextra -Wpedantic)

qt_add_executable(inkwell src/gui/main.cpp)
target_link_libraries(inkwell PRIVATE inkwell_gui)
target_compile_options(inkwell PRIVATE -Wall -Wextra -Wpedantic)

if(UNIX)
    add_executable(inkwell-tui
        src/main.cpp
        src/term.cpp
        src/app.cpp
    )
    target_link_libraries(inkwell-tui PRIVATE inkwell_core)
    target_compile_options(inkwell-tui PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(inkwell_tests tests/tests.cpp)
target_link_libraries(inkwell_tests PRIVATE inkwell_core)
target_compile_options(inkwell_tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(inkwell_tests PRIVATE INKWELL_SAMPLE_PLUGIN="$<TARGET_FILE:inkwell_sample_plugin>")
add_dependencies(inkwell_tests inkwell_sample_plugin)

qt_add_executable(inkwell_gui_tests tests/gui_smoke.cpp)
target_link_libraries(inkwell_gui_tests PRIVATE inkwell_gui)
target_compile_options(inkwell_gui_tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(inkwell_gui_tests PRIVATE INKWELL_PREVIEW="${CMAKE_BINARY_DIR}/gui-preview.png")

enable_testing()
add_test(NAME inkwell_tests COMMAND inkwell_tests)
add_test(NAME inkwell_gui_tests COMMAND inkwell_gui_tests)
set_tests_properties(inkwell_gui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

include(GNUInstallDirs)
install(TARGETS inkwell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX)
    install(TARGETS inkwell-tui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(FILES dist/org.inkwell.Inkwell.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES dist/icons/hicolor/128x128/apps/org.inkwell.Inkwell.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps)
install(FILES dist/icons/hicolor/256x256/apps/org.inkwell.Inkwell.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
install(FILES dist/icons/hicolor/512x512/apps/org.inkwell.Inkwell.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)
install(FILES dist/org.inkwell.Inkwell.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

qt_add_resources(inkwell inkwell_icons
    PREFIX "/inkwell"
    BASE "dist/icons/hicolor/256x256/apps"
    FILES dist/icons/hicolor/256x256/apps/org.inkwell.Inkwell.png
)
