cmake_minimum_required(VERSION 3.16)

project(CRLF VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg)

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

# libvterm 0.3.3 (MIT). The screen for the interactive shell.
add_library(vterm STATIC
    third_party/libvterm/src/encoding.c
    third_party/libvterm/src/keyboard.c
    third_party/libvterm/src/mouse.c
    third_party/libvterm/src/parser.c
    third_party/libvterm/src/pen.c
    third_party/libvterm/src/screen.c
    third_party/libvterm/src/state.c
    third_party/libvterm/src/unicode.c
    third_party/libvterm/src/vterm.c
)
target_include_directories(vterm
    PUBLIC third_party/libvterm/include
    PRIVATE third_party/libvterm/src
)
set_target_properties(vterm PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
if(NOT MSVC)
    target_compile_options(vterm PRIVATE -w)
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
    app/DocumentTabBar.h
    app/DocumentTabBar.cpp
    app/ExplorerPanel.h
    app/ExplorerPanel.cpp
    app/ExplorerModel.h
    app/ExplorerModel.cpp
    app/FolderWatch.h
    app/FolderWatch.cpp
    app/FileIconTheme.h
    app/FileIconTheme.cpp
    app/TemplateDialog.h
    app/TemplateDialog.cpp
    app/ProcessLaunch.h
    app/ProcessLaunch.cpp
    app/CommandRunner.h
    app/CommandRunner.cpp
    app/SyntaxRunner.h
    app/SyntaxRunner.cpp
    app/CompletionSession.h
    app/CompletionSession.cpp
    app/JsonLineProcess.h
    app/JsonLineProcess.cpp
    app/AgentSession.h
    app/AgentSession.cpp
    app/AgentTerminals.h
    app/AgentTerminals.cpp
    app/AgentHost.h
    app/AgentHost.cpp
    app/AgentPanel.h
    app/AgentPanel.cpp
    app/AgentController.h
    app/AgentController.cpp
    app/ProcessOrchestrator.h
    app/ProcessOrchestrator.cpp
    app/ProcessesPanel.h
    app/ProcessesPanel.cpp
    app/OutputPanel.h
    app/OutputPanel.cpp
    app/ProblemsPanel.h
    app/ProblemsPanel.cpp
    app/ConPtySession.h
    app/ConPtySession.cpp
    app/TerminalView.h
    app/TerminalView.cpp
    app/TerminalPanel.h
    app/TerminalPanel.cpp
    app/BottomPanel.h
    app/BottomPanel.cpp
    app/OpenWith.h
    app/OpenWith.cpp
    app/GitClient.h
    app/GitClient.cpp
    app/SourceControl.h
    app/SourceControl.cpp
    app/SourceControlPanel.h
    app/SourceControlPanel.cpp
    app/DiffView.h
    app/DiffView.cpp
)

# Reading git's output and comparing text. No widgets, so the tests build it too.
set(GIT_SOURCES
    git/GitTypes.h
    git/GitTypes.cpp
    git/GitParse.h
    git/GitParse.cpp
    git/GitDiscovery.h
    git/GitDiscovery.cpp
    git/LineDiff.h
    git/LineDiff.cpp
)

# The Agent Client Protocol and the access policy. No widgets, so the tests build it too.
set(AGENT_SOURCES
    agent/JsonRpc.h
    agent/JsonRpc.cpp
    agent/AcpTypes.h
    agent/AcpTypes.cpp
    agent/AgentPolicy.h
    agent/AgentPolicy.cpp
)

set(EDITOR_SOURCES
    editor/ChangeMark.h
    editor/CodeEditor.h
    editor/CodeEditor.cpp
    editor/CompletionItem.h
    editor/CompletionPopup.h
    editor/CompletionPopup.cpp
    editor/Diagnostic.h
    editor/Diagnostic.cpp
    editor/FoldingLayout.h
    editor/BlockData.h
    editor/Gutter.h
    editor/Gutter.cpp
    editor/FoldController.h
    editor/FoldController.cpp
    editor/SymbolIndex.h
    editor/SymbolIndex.cpp
    editor/ImportAliases.h
    editor/ImportAliases.cpp
    editor/ExternalLookup.h
    editor/ExternalLookup.cpp
    editor/SymbolLocation.h
    editor/TailwindNames.h
    editor/TailwindNames.cpp
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
    syntax/SymbolScanner.h
    syntax/SymbolScanner.cpp
)

set(IO_SOURCES
    io/DocumentFile.h
    io/DocumentFile.cpp
    io/FileOperations.h
    io/FileOperations.cpp
    io/NamePattern.h
    io/NamePattern.cpp
    io/SessionStore.h
    io/SessionStore.cpp
    io/ProjectCommands.h
    io/ProjectCommands.cpp
    io/ProjectMerge.h
    io/ProjectMerge.cpp
    io/WorkspaceSeed.h
    io/WorkspaceSeed.cpp
    io/CommandVariables.h
    io/CommandVariables.cpp
    io/WslPath.h
    io/WslPath.cpp
)

qt_add_executable(${PROJECT_NAME}
    MANUAL_FINALIZATION
    main.cpp
    Types.h
    ${APP_SOURCES}
    ${AGENT_SOURCES}
    ${GIT_SOURCES}
    ${EDITOR_SOURCES}
    ${HEX_SOURCES}
    ${SYNTAX_SOURCES}
    ${IO_SOURCES}
    resources/resources.qrc
    resources/app_icon.rc
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Unit tests for git output parsing, repository discovery, and the line diff.
# Run with: ctest --test-dir build
option(CRLF_BUILD_TESTS "Build the unit tests in tests/" ON)
if(CRLF_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(GitTests tests/GitTests.cpp ${GIT_SOURCES} io/NamePattern.cpp io/WslPath.cpp)
    target_include_directories(GitTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(GitTests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME GitTests COMMAND GitTests)

    # Runs a real git against a temporary repository.
    add_executable(GitClientTests tests/GitClientTests.cpp app/GitClient.cpp app/ProcessLaunch.cpp
        app/ProcessOrchestrator.cpp ${GIT_SOURCES} io/NamePattern.cpp io/WslPath.cpp)
    target_include_directories(GitClientTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(GitClientTests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME GitClientTests COMMAND GitClientTests)

    # The access policy and the protocol messages, without a process.
    add_executable(AgentTests tests/AgentTests.cpp ${AGENT_SOURCES})
    target_include_directories(AgentTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(AgentTests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME AgentTests COMMAND AgentTests)

    # Drives tests/fake_acp_agent.py, so it needs python on PATH.
    add_executable(AgentSessionTests tests/AgentSessionTests.cpp app/AgentSession.cpp app/AgentTerminals.cpp
        app/JsonLineProcess.cpp app/ProcessLaunch.cpp app/ProcessOrchestrator.cpp ${AGENT_SOURCES}
        io/CommandVariables.cpp io/WslPath.cpp)
    target_include_directories(AgentSessionTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(AgentSessionTests PRIVATE FAKE_AGENT="${CMAKE_CURRENT_SOURCE_DIR}/tests/fake_acp_agent.py")
    target_link_libraries(AgentSessionTests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME AgentSessionTests COMMAND AgentSessionTests)

    # The .crlf files: merging, discovery, saving, the command variables, and
    # the copy into a workspace. No widgets; Gui is for QKeySequence.
    add_executable(ProjectTests tests/ProjectTests.cpp io/ProjectCommands.cpp io/ProjectMerge.cpp
        io/WorkspaceSeed.cpp io/CommandVariables.cpp io/WslPath.cpp agent/AgentPolicy.cpp)
    target_include_directories(ProjectTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(ProjectTests PRIVATE
        SHIPPED_APPLICATION_FILE="${CMAKE_CURRENT_SOURCE_DIR}/resources/default.crlf")
    target_link_libraries(ProjectTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME ProjectTests COMMAND ProjectTests)

    # The explorer's tree against a real folder: what a re-read keeps, and
    # that a change made on disk reaches the tree.
    add_executable(ExplorerModelTests tests/ExplorerModelTests.cpp app/ExplorerModel.cpp app/FolderWatch.cpp
        app/FileIconTheme.cpp app/ProcessOrchestrator.cpp io/NamePattern.cpp io/WslPath.cpp
        syntax/LanguageRegistry.cpp syntax/LanguageDefinition.cpp)
    target_include_directories(ExplorerModelTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ExplorerModelTests PRIVATE Qt6::Widgets Qt6::Svg Qt6::Test)
    add_test(NAME ExplorerModelTests COMMAND ExplorerModelTests)

    # The reference agent against scripted providers. Needs python on PATH.
    add_test(NAME ReferenceAgentTests
        COMMAND python "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_agent_tests.py")
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg vterm)
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
# Languages, themes, file icons, and fonts are files beside the program, not
# data inside the executable. A dev build reads them next to the exe; an
# installed copy reads them from the install folder, one level above bin.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/languages"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/styles"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/themes"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/fonts"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/icons"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/default.crlf"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/application.crlf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/blank.crlf"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/blank.crlf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/prices.json"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/prices.json"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/scripts/syntax.sh"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts/syntax.sh"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/scripts/autocomplete.sh"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts/autocomplete.sh"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/scripts/autocomplete.py"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts/autocomplete.py"
    COMMAND ${CMAKE_COMMAND}
        "-DSRC=${CMAKE_SOURCE_DIR}/scripts/agent"
        "-DDST=$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts/agent"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_python_tree.cmake"
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/languages/"
    DESTINATION styles
    FILES_MATCHING PATTERN "*.json")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/themes/"
    DESTINATION themes
    FILES_MATCHING PATTERN "*.json")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/fonts/"
    DESTINATION fonts
    FILES_MATCHING PATTERN "*.ttf" PATTERN "*.txt")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/icons/"
    DESTINATION icons
    FILES_MATCHING PATTERN "*.json" PATTERN "*.ttf" PATTERN "*.txt" PATTERN "*.svg" PATTERN "*.png")
# Beside styles and themes, one level above bin. These copies are only the
# seed: a run copies each one the application folder lacks into it, and that
# folder is the one the user edits, so each install may replace them.
# application.crlf sets up the shipped scripts and agents; blank.crlf sets
# nothing.
install(FILES "${CMAKE_SOURCE_DIR}/resources/default.crlf"
    DESTINATION .
    RENAME application.crlf)
install(FILES "${CMAKE_SOURCE_DIR}/resources/blank.crlf"
    "${CMAKE_SOURCE_DIR}/resources/prices.json"
    DESTINATION .)
install(FILES "${CMAKE_SOURCE_DIR}/scripts/syntax.sh"
    "${CMAKE_SOURCE_DIR}/scripts/autocomplete.sh"
    "${CMAKE_SOURCE_DIR}/scripts/autocomplete.py"
    DESTINATION scripts)
# The reference agent: its modules, the tools package, and models.json.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/scripts/agent/"
    DESTINATION scripts/agent
    FILES_MATCHING PATTERN "*.py" PATTERN "*.json"
    PATTERN "__pycache__" EXCLUDE)

qt_generate_deploy_app_script(
    TARGET ${PROJECT_NAME}
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

file(TO_NATIVE_PATH "${CMAKE_INSTALL_PREFIX}" INSTALLER_STAGING_DIR)
file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/resources/images/CRLF_v2.ico" INSTALLER_ICON)
file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/installer" INSTALLER_OUTPUT_DIR)
configure_file(packaging/${PROJECT_NAME}.iss.in ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.iss @ONLY)

qt_finalize_executable(${PROJECT_NAME})
