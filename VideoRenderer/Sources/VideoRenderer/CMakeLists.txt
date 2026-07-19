set(VR_COMMON_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../Common/VideoRenderer/VideoRendererTypes.h
    ${CMAKE_CURRENT_LIST_DIR}/../Common/VideoRenderer/VideoRendererTypes.cpp)

if(APPLE)
    add_library(VideoRendererEngine STATIC
        ${VR_COMMON_SOURCES}
        ../macOS/VideoRenderer/VideoManifest.h
        ../macOS/VideoRenderer/VideoManifest.mm
        ../macOS/VideoRenderer/VRMemoryAssetLoader.h
        ../macOS/VideoRenderer/VRMemoryAssetLoader.mm
        ../macOS/VideoRenderer/VideoRendererEngine.h
        ../macOS/VideoRenderer/VideoRendererEngine.mm)

    target_compile_features(VideoRendererEngine PUBLIC cxx_std_20)
    set_target_properties(VideoRendererEngine PROPERTIES
        OBJCXX_STANDARD 20
        OBJCXX_STANDARD_REQUIRED ON)
    target_include_directories(VideoRendererEngine PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/../Common/VideoRenderer
        ${CMAKE_CURRENT_LIST_DIR}/../macOS/VideoRenderer)
    target_link_libraries(VideoRendererEngine PUBLIC ${VR_FRAMEWORKS})
    target_compile_options(VideoRendererEngine PRIVATE
        -Wno-deprecated-declarations
        -Wno-unused-function)
elseif(UNIX AND NOT APPLE)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets)
    find_package(X11 REQUIRED COMPONENTS Xrandr Xfixes Xext)

    add_library(VideoRendererEngine STATIC
        ${VR_COMMON_SOURCES}
        ../Linux/VideoRenderer/VideoManifest.h
        ../Linux/VideoRenderer/VideoManifest.cpp
        ../Linux/VideoRenderer/VideoRendererEngine.h
        ../Linux/VideoRenderer/VideoRendererEngine.cpp
        ../Linux/VideoRenderer/ControlChannel.h
        ../Linux/VideoRenderer/ControlChannel.cpp
        ../Linux/VideoRenderer/X11DesktopWindow.h
        ../Linux/VideoRenderer/X11DesktopWindow.cpp)

    target_compile_features(VideoRendererEngine PUBLIC cxx_std_20)
    set_target_properties(VideoRendererEngine PROPERTIES AUTOMOC ON)
    target_include_directories(VideoRendererEngine PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/../Common/VideoRenderer
        ${CMAKE_CURRENT_LIST_DIR}/../Linux/VideoRenderer)
    target_link_libraries(VideoRendererEngine PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::MultimediaWidgets
        X11::X11
        X11::Xrandr
        X11::Xfixes
        X11::Xext)
else()
    message(FATAL_ERROR "VideoRenderer has no platform backend for this system")
endif()
