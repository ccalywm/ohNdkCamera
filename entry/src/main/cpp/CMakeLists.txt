# the minimum version of CMake.
cmake_minimum_required(VERSION 3.5.0)
project(NDKcamera)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH}
                    ${NATIVERENDER_ROOT_PATH}/include)

add_library(entry SHARED
main.cpp
camera_manager.cpp
video_codec.cpp
opengl/EGLHelper.cpp
opengl/GLRenderer.cpp
opengl/OpenGLManager.cpp
)
#target_link_libraries(entry PUBLIC libohcamera.so libace_napi.z.so libnative_buffer.so libhilog_ndk.z.so librawfile.z.so)


# 系统及 NDK 公共库（不变）
target_link_libraries(entry PUBLIC libohcamera.so libace_napi.z.so libnative_buffer.so librawfile.z.so
                                   libace_ndk.z.so native_window EGL GLESv3
                                   libhilog_ndk.z.so uv ohaudio ohcamera image_receiver native_image ohimage native_buffer hilog_ndk.z
                                   native_media_codecbase native_media_core native_media_vdec c++_shared native_media_venc)

