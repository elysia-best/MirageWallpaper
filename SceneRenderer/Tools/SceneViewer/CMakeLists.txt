# Standalone GLFW viewer for Wallpaper Engine scene.pkgs.
#   - SceneViewer : SceneRenderer scene.pkg viewer
# Modules:
#   - viewer.common — argv/option parsing + GLFW/exe-dir helpers.

# viewer.common: shared helpers + arg parsing.
add_library(viewer-common STATIC)
target_sources(viewer-common
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        Common.cppm
        Arg.cppm)
target_sources(viewer-common PRIVATE Common.cpp)
target_include_directories(viewer-common PUBLIC .)
target_link_libraries(viewer-common PUBLIC PkgConfig::GLFW argparse::argparse rstd.cppstd)
target_compile_features(viewer-common PUBLIC cxx_std_20)
set_target_properties(viewer-common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_SCAN_FOR_MODULES ON)

if(SCENERENDERER_BUILD_ENGINE AND SCENERENDERER_BUILD_VIEWER)
    find_package(Vulkan REQUIRED)
    if(APPLE)
        enable_language(OBJCXX)
        find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
        find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
        find_library(METAL_FRAMEWORK Metal REQUIRED)
        find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    endif()
    add_executable(SceneViewer PreviewApp.cpp)
    target_link_libraries(SceneViewer
        PRIVATE
            viewer-common
            PkgConfig::GLFW
            Vulkan::Vulkan
            SceneRenderer::Engine
            argparse::argparse)
    if(UNIX AND NOT APPLE)
        find_package(OpenGL REQUIRED)
        target_link_libraries(SceneViewer
            PRIVATE
                OpenGL::GL)
        # This is required when using brew on linux
        include_directories(SYSTEM ${OPENGL_EGL_INCLUDE_DIRS})
    endif()
    if(APPLE)
        target_sources(SceneViewer PRIVATE
            ../../Sources/SceneRenderer/Host/macOS/MacMetalDisplay.mm)
        target_compile_definitions(SceneViewer PRIVATE
            GL_SILENCE_DEPRECATION
            SCENERENDERER_MACMETALDISPLAY_WITH_GLFW)
        target_link_libraries(SceneViewer
            PRIVATE
                ${OPENGL_FRAMEWORK}
                ${COCOA_FRAMEWORK}
                ${METAL_FRAMEWORK}
                ${QUARTZCORE_FRAMEWORK})
    endif()
    set_target_properties(SceneViewer PROPERTIES CXX_SCAN_FOR_MODULES ON)
endif()

include(GNUInstallDirs)
install(TARGETS SceneViewer
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(CODE "
    file(GET_RUNTIME_DEPENDENCIES
        EXECUTABLES \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/SceneViewer\"
        RESOLVED_DEPENDENCIES_VAR _r_deps
        UNRESOLVED_DEPENDENCIES_VAR _u_deps
        
        DIRECTORIES 
            \"/usr/local/lib\" 
            \"/usr/lib/x86_64-linux-gnu\"
            \"/home/linuxbrew/.linuxbrew/lib\"
            
        PRE_EXCLUDE_REGEXES 
            \"^libc\\\\.so.*\" 
            \"^libm\\\\.so.*\" 
            \"^libpthread\\\\.so.*\" 
            \"^ld-linux.*\"
            \"^libdl\\\\.so.*\"
            \"^librt\\\\.so.*\"
            
        POST_EXCLUDE_REGEXES 
            \"^/lib/.*\" 
            \"^/lib64/.*\" 
            \"^/usr/lib/.*\" 
            \"^/usr/lib64/.*\"
            \"^/usr/lib/x86_64-linux-gnu/.*\"
            \".*libLLVM.*\"
            \".*libgallium.*\"
            \".*libSPIRV.*\"
            \".*libz3.*\"
    )

    foreach(_file \${_r_deps})
        file(INSTALL
            DESTINATION \"\${CMAKE_INSTALL_PREFIX}/lib\"
            TYPE SHARED_LIBRARY
            FILES \"\${_file}\"
        )
    endforeach()

    # 【修复 1】：if 条件直接检查变量名，不要加 ${}
    if(_u_deps)
        message(WARNING \"Unresolved dependencies found: \${_u_deps}\")
    endif()
    
    # 【修复 2】：安全计算数量
    set(_r_count 0)
    if(_r_deps)
        # 【核心修复】：list(LENGTH) 的第一个参数必须是变量名 (_r_deps)，而不是值 (\${_r_deps})
        list(LENGTH _r_deps _r_count)
    endif()
    
    message(STATUS \"Automatically installed \${_r_count} runtime dependencies.\")
")