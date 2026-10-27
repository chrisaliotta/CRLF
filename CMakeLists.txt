cmake_minimum_required(VERSION 3.16)

project(CRLF VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# windres mangles quoted paths containing spaces (this project lives under
# "OneDrive - Quantalytix"), so hand it a space-free path relative to the
# build directory instead of the absolute <SOURCE>. The project's only .rc
# is the icon script, which needs no includes or defines.
if(MINGW)
    file(RELATIVE_PATH APP_ICON_RC_RELATIVE
         "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}/resources/app_icon.rc")
    set(CMAKE_RC_COMPILE_OBJECT
        "<CMAKE_RC_COMPILER> --use-temp-file -O coff ${APP_ICON_RC_RELATIVE} <OBJECT>")
endif()

set(APP_SOURCES
    app/MainWindow.h
    app/MainWindow.cpp
    app/SingleInstance.h
    app/SingleInstance.cpp
    app/TabWorkspace.h
    app/TabWorkspace.cpp
    app/TitleBar.h
    app/TitleBar.cpp
    app/Actions.h
    app/Actions.cpp
    app/FindBar.h
    app/FindBar.cpp
)

set(EDITOR_SOURCES
    editor/CodeEditor.h
    editor/CodeEditor.cpp
    editor/FoldingLayout.h
    editor/BlockData.h
    editor/Gutter.h
    editor/Gutter.cpp
    editor/FoldController.h
    editor/FoldController.cpp
)

set(HEX_SOURCES
    hex/HexView.h
    hex/HexView.cpp
)

set(SYNTAX_SOURCES
    syntax/SyntaxTypes.h
    syntax/LanguageDefinition.h
    syntax/LanguageDefinition.cpp
    syntax/LanguageRegistry.h
    syntax/LanguageRegistry.cpp
    syntax/Theme.h
    syntax/Theme.cpp
    syntax/Highlighter.h
    syntax/Highlighter.cpp
)

set(IO_SOURCES
    io/DocumentFile.h
    io/DocumentFile.cpp
    io/SessionStore.h
    io/SessionStore.cpp
)

qt_add_executable(${PROJECT_NAME}
    MANUAL_FINALIZATION
    main.cpp
    Types.h
    ${APP_SOURCES}
    ${EDITOR_SOURCES}
    ${HEX_SOURCES}
    ${SYNTAX_SOURCES}
    ${IO_SOURCES}
    resources/resources.qrc
    resources/app_icon.rc
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Network)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_NAME="${PROJECT_NAME}"
    APP_VERSION="${PROJECT_VERSION}"
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# `cmake --install` stages a self-contained copy of the app (Qt and MinGW
# runtime DLLs included) inside the build tree; the installer packages it.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/dist/${PROJECT_NAME}" CACHE PATH
        "Staging directory packaged by the installer" FORCE)
endif()

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET ${PROJECT_NAME}
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

file(TO_NATIVE_PATH "${CMAKE_INSTALL_PREFIX}" INSTALLER_STAGING_DIR)
file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/resources/images/CRLF_basic.ico" INSTALLER_ICON)
file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/installer" INSTALLER_OUTPUT_DIR)
configure_file(packaging/${PROJECT_NAME}.iss.in ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.iss @ONLY)

qt_finalize_executable(${PROJECT_NAME})
