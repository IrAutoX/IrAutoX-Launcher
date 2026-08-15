cmake_minimum_required(VERSION 3.24)

project(IrAutoXLauncher
    VERSION 2.0.3
    DESCRIPTION "IrAutoX game launcher"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(IRAUTOX_BUILD_TESTS "Build launcher unit tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Svg Concurrent)
qt_standard_project_setup()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/windows.rc"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/updater.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/updater.rc"
    @ONLY
)

set(IRAUTOX_UI_COMMON
    src/ui/WindowChrome.cpp
    src/ui/WindowChrome.h
    src/ui/StyledMessageBox.cpp
    src/ui/StyledMessageBox.h
)

set(IRAUTOX_SOURCES
    src/main.cpp
    src/core/AppSettings.cpp
    src/core/AppSettings.h
    src/core/ArchiveUtil.cpp
    src/core/ArchiveUtil.h
    src/core/DownloadManager.cpp
    src/core/DownloadManager.h
    src/core/GameLibrary.cpp
    src/core/GameLibrary.h
    src/core/MessageCodec.cpp
    src/core/MessageCodec.h
    src/core/PresenceMonitor.cpp
    src/core/PresenceMonitor.h
    src/core/ProtocolClient.cpp
    src/core/ProtocolClient.h
    src/core/SdkBridge.cpp
    src/core/SdkBridge.h
    src/core/SecureStore.cpp
    src/core/SecureStore.h
    src/core/ShortcutManager.cpp
    src/core/ShortcutManager.h
    src/ui/LoginDialog.cpp
    src/ui/LoginDialog.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    ${IRAUTOX_UI_COMMON}
    resources/resources.qrc
)

if(WIN32)
    list(APPEND IRAUTOX_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/windows.rc")
endif()

qt_add_executable(IrAutoXLauncher WIN32 ${IRAUTOX_SOURCES})
qt_add_executable(IrAutoXUpdater WIN32
    src/updater/main_v203.cpp
    ${IRAUTOX_UI_COMMON}
    resources/resources.qrc
)
qt_add_executable(irxcmd WIN32 src/irxcmd/main.cpp)

if(WIN32)
    target_sources(IrAutoXUpdater PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/updater.rc")
endif()

target_include_directories(IrAutoXLauncher PRIVATE src)
target_include_directories(IrAutoXUpdater PRIVATE src)
target_compile_definitions(IrAutoXLauncher PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII IRAUTOX_VERSION="${PROJECT_VERSION}")
target_compile_definitions(IrAutoXUpdater PRIVATE IRAUTOX_VERSION="${PROJECT_VERSION}")
target_compile_definitions(irxcmd PRIVATE IRAUTOX_VERSION="${PROJECT_VERSION}")

target_link_libraries(IrAutoXLauncher PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Svg Qt6::Concurrent)
target_link_libraries(IrAutoXUpdater PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Svg)
target_link_libraries(irxcmd PRIVATE Qt6::Core Qt6::Network)

if(WIN32)
    add_library(IrAutoXSDK SHARED sdk/src/IrAutoXSDK.cpp)
    add_library(IrAutoXSDKStatic STATIC sdk/src/IrAutoXSDK.cpp)
    target_include_directories(IrAutoXSDK PUBLIC sdk/include)
    target_include_directories(IrAutoXSDKStatic PUBLIC sdk/include)
    target_compile_definitions(IrAutoXSDK PRIVATE IRAUTOX_SDK_BUILD NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(IrAutoXSDKStatic PUBLIC IRAUTOX_SDK_STATIC PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(IrAutoXSDK PRIVATE ws2_32 advapi32 shell32)
    target_link_libraries(IrAutoXSDKStatic PRIVATE ws2_32 advapi32 shell32)
    set_target_properties(IrAutoXSDK PROPERTIES OUTPUT_NAME "IrAutoXSDK")
    set_target_properties(IrAutoXSDKStatic PROPERTIES OUTPUT_NAME "IrAutoXSDK_static")
endif()

if(MSVC)
    target_compile_options(IrAutoXLauncher PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(IrAutoXUpdater PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(irxcmd PRIVATE /W4 /permissive- /utf-8 /EHsc)
    if(TARGET IrAutoXSDK)
        target_compile_options(IrAutoXSDK PRIVATE /W4 /permissive- /utf-8 /EHsc)
        target_compile_options(IrAutoXSDKStatic PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    target_link_options(IrAutoXLauncher PRIVATE /SUBSYSTEM:WINDOWS /MANIFEST:NO)
    target_link_options(IrAutoXUpdater PRIVATE /SUBSYSTEM:WINDOWS /MANIFEST:NO)
    target_link_options(irxcmd PRIVATE /SUBSYSTEM:WINDOWS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(IrAutoXLauncher PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(IrAutoXUpdater PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(irxcmd PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(IrAutoXLauncher PRIVATE crypt32 dwmapi shell32 ole32)
    target_link_libraries(IrAutoXUpdater PRIVATE crypt32 shell32)
endif()

install(TARGETS IrAutoXLauncher IrAutoXUpdater irxcmd RUNTIME DESTINATION .)

if(IRAUTOX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    if(WIN32)
        add_executable(IrAutoXSDKTests sdk/tests/TestSdk.cpp)
        target_include_directories(IrAutoXSDKTests PRIVATE sdk/include)
        target_compile_definitions(IrAutoXSDKTests PRIVATE IRAUTOX_SDK_STATIC NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(IrAutoXSDKTests PRIVATE IrAutoXSDKStatic ws2_32)
        add_test(NAME IrAutoXSDKTests COMMAND IrAutoXSDKTests)
    endif()
endif()
