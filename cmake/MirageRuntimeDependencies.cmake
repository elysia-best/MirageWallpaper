# Shared Linux install-time dependency scanning for Mirage executables.
#
# The package must carry application libraries, while EGL/GBM/DRM/Vulkan,
# Mesa driver libraries, and the desktop audio stack must remain on the target
# system. Those libraries discover hardware-specific drivers, SPA modules, and
# ALSA configuration at runtime; copying a CI host stack embeds paths such as a
# Homebrew Cellar location and breaks headless video rendering elsewhere.

function(mirage_install_runtime_dependencies target_name install_destination)
    if(NOT UNIX OR APPLE)
        return()
    endif()

    install(CODE "
        set(_mirage_runtime_executable \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${install_destination}/${target_name}\")
        if(EXISTS \"\${_mirage_runtime_executable}\")
            file(GET_RUNTIME_DEPENDENCIES
                EXECUTABLES \"\${_mirage_runtime_executable}\"
                RESOLVED_DEPENDENCIES_VAR _mirage_resolved_dependencies
                UNRESOLVED_DEPENDENCIES_VAR _mirage_unresolved_dependencies
                DIRECTORIES
                    \"/usr/local/lib\"
                    \"/usr/lib\"
                    \"/usr/lib64\"
                    \"/lib\"
                    \"/lib64\"
                    \"/home/linuxbrew/.linuxbrew/lib\"
                PRE_EXCLUDE_REGEXES
                    \"^libc\\\\.so.*\"
                    \"^libm\\\\.so.*\"
                    \"^libpthread\\\\.so.*\"
                    \"^ld-linux.*\"
                    \"^libdl\\\\.so.*\"
                    \"^librt\\\\.so.*\"
                    \"^libvulkan_.*\\\\.so.*\"
                    \"^libdri.*\\\\.so.*\"
                    \"^libgbm.*\\\\.so.*\"
                    \"^libEGL.*\\\\.so.*\"
                    \"^libGLES.*\\\\.so.*\"
                    \"^libGLX.*\\\\.so.*\"
                    \"^libGL.*\\\\.so.*\"
                    \"^libdrm\\\\.so.*\"
                    \"^libva.*\\\\.so.*\"
                    \"^libvdpau.*\\\\.so.*\"
                    \"^libasound.*\"
                    \"^libpipewire.*\"
                    \"^libspa.*\"
                    \"^libpulse.*\"
                    \"^libjack.*\"
                POST_EXCLUDE_REGEXES
                    \"^/lib/.*\"
                    \"^/lib64/.*\"
                    \"^/usr/lib/.*\"
                    \"^/usr/lib64/.*\"
                    \"^/usr/lib/x86_64-linux-gnu/.*\"
                    \"^/usr/lib/aarch64-linux-gnu/.*\"
                    \"^/home/linuxbrew/.linuxbrew/lib/.*\"
                    \".*libLLVM.*\"
                    \".*libgallium.*\"
                    \".*libSPIRV.*\"
                    \".*libz3.*\"
            )

            foreach(_mirage_dependency IN LISTS _mirage_resolved_dependencies)
                file(INSTALL
                    DESTINATION \"\${CMAKE_INSTALL_PREFIX}/lib\"
                    TYPE SHARED_LIBRARY
                    FILES \"\${_mirage_dependency}\"
                )
            endforeach()

            if(_mirage_unresolved_dependencies)
                message(WARNING
                    \"Unresolved runtime dependencies for ${target_name}: \${_mirage_unresolved_dependencies}\")
            endif()
        endif()
    ")
endfunction()
