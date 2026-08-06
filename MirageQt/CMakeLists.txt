cmake_minimum_required(VERSION 3.24)

project(MirageQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Quick
    QuickControls2
    Network
    Concurrent
    Qml
    WebEngineCore
    WebEngineWidgets
    DBus)
find_package(X11 REQUIRED)

set(FLUENTUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FLUENTUI_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
add_subdirectory(Vendor/FluentUI/src)

# MirageLinuxDisplay is vendored in-tree (see Vendors/MirageLinuxDisplay/README.md).
set(MIRAGE_DISPLAY_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../Vendors/MirageLinuxDisplay")
set(MIRAGE_DISPLAY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MIRAGE_DISPLAY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MIRAGE_DISPLAY_PLUGIN_QML OFF CACHE BOOL "" FORCE)
set(MIRAGE_DISPLAY_WITH_VULKAN OFF CACHE BOOL "" FORCE)
set(MIRAGE_DISPLAY_WITH_EGL OFF CACHE BOOL "" FORCE)
add_subdirectory("${MIRAGE_DISPLAY_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/MirageLinuxDisplay")

qt_standard_project_setup()
# Embed Resources/resources.qrc (app icon) into the binary.
set(CMAKE_AUTORCC ON)

set(MIRAGEQT_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(MIRAGE_STEAM_WEB_API_KEY "$ENV{MIRAGE_STEAM_WEB_API_KEY}" CACHE STRING
    "Optional built-in Steam Web API key (32 hexadecimal characters)")
if(MIRAGE_STEAM_WEB_API_KEY AND NOT MIRAGE_STEAM_WEB_API_KEY MATCHES "^[A-Fa-f0-9]{32}$")
    message(FATAL_ERROR "MIRAGE_STEAM_WEB_API_KEY must contain exactly 32 hexadecimal characters")
endif()

add_library(MirageQtCore STATIC
    Sources/Services/FavoritesManager.cpp
    Sources/Services/FavoritesManager.h
    Sources/Services/DisplayBrokerService.cpp
    Sources/Services/DisplayBrokerService.h
    Sources/Services/GlobalSettingsService.cpp
    Sources/Services/GlobalSettingsService.h
    Sources/Services/LinuxSystemIntegration.cpp
    Sources/Services/LinuxSystemIntegration.h
    Sources/Services/MirageController.cpp
    Sources/Services/MirageController.h
    Sources/Services/Paths.cpp
    Sources/Services/Paths.h
    Sources/Services/PlaylistModels.cpp
    Sources/Services/PlaylistModels.h
    Sources/Services/PlaylistManager.cpp
    Sources/Services/PlaylistManager.h
    Sources/Services/PlaylistRotator.cpp
    Sources/Services/PlaylistRotator.h
    Sources/Services/PlaylistTransitionOverlay.cpp
    Sources/Services/PlaylistTransitionOverlay.h
    Sources/Services/RendererController.cpp
    Sources/Services/RendererController.h
    Sources/Services/SteamCMDManager.cpp
    Sources/Services/SteamCMDManager.h
    Sources/Services/SteamWebAPI.cpp
    Sources/Services/SteamWebAPI.h
    Sources/Services/ThumbnailGenerator.cpp
    Sources/Services/ThumbnailGenerator.h
    Sources/Services/WallpaperLibrary.cpp
    Sources/Services/WallpaperLibrary.h
    Sources/Services/WallpaperRuntimeStore.cpp
    Sources/Services/WallpaperRuntimeStore.h
    Sources/Services/WEConditionEvaluator.cpp
    Sources/Services/WEConditionEvaluator.h
    Sources/Services/WEProject.cpp
    Sources/Services/WEProject.h
    Sources/Services/WorkshopModels.cpp
    Sources/Services/WorkshopModels.h
    Sources/Services/WorkshopViewModel.cpp
    Sources/Services/WorkshopViewModel.h)

target_include_directories(MirageQtCore PUBLIC Sources)
target_include_directories(MirageQtCore PRIVATE "${MIRAGE_DISPLAY_SOURCE_DIR}/include")
target_compile_definitions(MirageQtCore PUBLIC
    MIRAGEQT_REPO_ROOT="${MIRAGEQT_REPO_ROOT}"
    MIRAGE_STEAM_WEB_API_KEY="${MIRAGE_STEAM_WEB_API_KEY}")
target_link_libraries(MirageQtCore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::Qml
    Qt6::DBus
    mirage_display_static
    X11::X11)

qt_add_executable(MirageQt
    Sources/App/main.cpp
    Resources/resources.qrc)

set_source_files_properties(Sources/Qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(Sources/Qml/PropertyLocalization.js PROPERTIES QT_RESOURCE_ALIAS PropertyLocalization.js)
file(GLOB_RECURSE MIRAGE_QML_FILES CONFIGURE_DEPENDS
    Sources/Qml/*.qml
    Sources/Qml/*.js)
foreach(qml_file IN LISTS MIRAGE_QML_FILES)
    file(RELATIVE_PATH qml_alias "${CMAKE_CURRENT_LIST_DIR}/Sources/Qml" "${qml_file}")
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_alias}")
endforeach()
qt_add_qml_module(MirageQt
    URI MirageQt
    VERSION 1.0
    QML_FILES
        ${MIRAGE_QML_FILES})

target_link_libraries(MirageQt PRIVATE
    MirageQtCore
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::WebEngineWidgets)

target_include_directories(MirageQt PRIVATE Vendor/FluentUI/src)
target_link_libraries(MirageQt PRIVATE fluentuiplugin fluentuipluginplugin)

set_target_properties(MirageQt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

install(TARGETS MirageQt
    RUNTIME DESTINATION bin)

# --- Desktop integration ---
# Configures the .desktop entry with the absolute install path of the binary.
set(MIRAGEQT_EXECUTABLE "${CMAKE_INSTALL_PREFIX}/bin/MirageQt")
configure_file(Resources/mirageqt.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/mirageqt.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mirageqt.desktop
        DESTINATION share/applications)
install(FILES Resources/mirage.png
        DESTINATION share/icons/hicolor/512x512/apps
        RENAME mirageqt.png)
