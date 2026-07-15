//
// Created by Lixiaoyao on 2026/1/6.
//
#ifndef EGL_HELPER_H
#define EGL_HELPER_H

#include <EGL/egl.h>
#include <multimedia/player_framework/native_avcodec_base.h>

// -----------------------------------------------------------
// 辅助类：EGL 环境管理
// -----------------------------------------------------------
class EGLHelper {
public:
    // 初始化
    bool init();

    // 创建 Window Surface
    EGLSurface createWindowSurface(OHNativeWindow *window);

    // 创建 Pbuffer Surface
    EGLSurface createPbufferSurface(int width, int height);

    // 绑定 Context
    bool makeCurrent(EGLSurface surface);

    // 交换缓冲区
    bool swapBuffers(EGLSurface surface);

    // 销毁 Surface
    void destroySurface(EGLSurface surface);

    // 释放
    void release();
    void unbind();

    // 获取 Context
    EGLSurface getDefaultSurface() { return mDefaultSurface; }

    // 获取 Context
    EGLContext getContext() { return mContext; } // 用于创建 SurfaceTexture

    EGLDisplay getDisplay() { return mDisplay; } // 新增访问器
private:
    EGLDisplay mDisplay = EGL_NO_DISPLAY; // EGL Display
    EGLContext mContext = EGL_NO_CONTEXT; // 用于创建 SurfaceTexture
    EGLConfig mConfig = nullptr; // 配置
    EGLSurface mDefaultSurface = EGL_NO_SURFACE; // 默认 Surface
};
#endif // EGL_HELPER_H
