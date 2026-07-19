add_executable(VideoRendererTypesTests
    VideoRendererTypesTests.cpp
    ../Sources/Common/VideoRenderer/VideoRendererTypes.cpp)
target_include_directories(VideoRendererTypesTests PRIVATE
    ../Sources/Common/VideoRenderer)
target_compile_features(VideoRendererTypesTests PRIVATE cxx_std_20)
add_test(NAME VideoRendererTypes COMMAND VideoRendererTypesTests)

if(UNIX AND NOT APPLE)
    add_executable(VideoManifestTests VideoManifestTests.cpp)
    target_link_libraries(VideoManifestTests PRIVATE VideoRendererEngine)
    target_compile_features(VideoManifestTests PRIVATE cxx_std_20)
    add_test(NAME VideoManifest COMMAND VideoManifestTests)
endif()
