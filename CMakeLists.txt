cmake_minimum_required(VERSION 3.24)

set(IRAUTOX_VERSION_OVERRIDE "" CACHE STRING "Override launcher version, e.g. from a release tag")
if(IRAUTOX_VERSION_OVERRIDE)
    set(_irautox_project_version "${IRAUTOX_VERSION_OVERRIDE}")
else()
    set(_irautox_project_version "2.0.0")
endif()

project(IrAutoXLauncher
    VERSION ${_irautox_project_version}
    DESCRIPTION "Native IrAutoX game launcher"
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
    src/core/ProtocolClient.cpp
    src/core/ProtocolClient.h
    src/core/SecureStore.cpp
    src/core/SecureStore.h
    src/core/ShortcutSync.cpp
    src/core/ShortcutSync.h
    src/core/SingleInstance.cpp
    src/core/SingleInstance.h
    src/ui/LauncherChrome.cpp
    src/ui/LauncherChrome.h
    src/ui/LoginDialog.cpp
    src/ui/LoginDialog.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    resources/resources.qrc
)

if(WIN32)
    list(APPEND IRAUTOX_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/windows.rc")
endif()

qt_add_executable(IrAutoXLauncher WIN32 ${IRAUTOX_SOURCES})
qt_add_executable(IrAutoXUpdater WIN32
    src/updater/main.cpp
    resources/resources.qrc
)

foreach(target IrAutoXLauncher IrAutoXUpdater)
    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        IRAUTOX_VERSION="${PROJECT_VERSION}"
    )
endforeach()

target_link_libraries(IrAutoXLauncher PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    Qt6::Concurrent
)

target_link_libraries(IrAutoXUpdater PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
)

if(MSVC)
    foreach(target IrAutoXLauncher IrAutoXUpdater)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endforeach()
    target_link_options(IrAutoXLauncher PRIVATE /MANIFEST:NO)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target IrAutoXLauncher IrAutoXUpdater)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

if(WIN32)
    target_link_libraries(IrAutoXLauncher PRIVATE crypt32 shell32 ole32)
endif()

install(TARGETS IrAutoXLauncher IrAutoXUpdater RUNTIME DESTINATION .)

if(IRAUTOX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
