//
// Created by Lixiaoyao on 2026/1/6.
//
#include "EGLHelper.h"
// ======================= EGLHelper 实现 =======================
// (为节省篇幅，EGL初始化代码保持标准流程，核心是 createPbufferSurface 和 createWindowSurface)
bool EGLHelper::init() {
    mDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (mDisplay == EGL_NO_DISPLAY) return false;
    EGLint version[2];
    if (!eglInitialize(mDisplay, &version[0], &version[1])) return false;

    const EGLint attribs[] = {
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            0x3142, 1, // EGL_RECORDABLE_ANDROID
            EGL_NONE
    };
    EGLConfig configs[1];
    EGLint numConfigs;
    eglChooseConfig(mDisplay, attribs, configs, 1, &numConfigs);
    mConfig = configs[0];

    const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    mContext = eglCreateContext(mDisplay, mConfig, EGL_NO_CONTEXT, ctxAttribs);

    // 创建 1x1 离屏 Surface 保活
    mDefaultSurface = createPbufferSurface(1, 1);
    return true;
}

EGLSurface EGLHelper::createPbufferSurface(int width, int height) {
    const EGLint attribs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    return eglCreatePbufferSurface(mDisplay, mConfig, attribs);
}

EGLSurface EGLHelper::createWindowSurface(OHNativeWindow* window) {
    if (!window) return EGL_NO_SURFACE;
    const EGLint attribs[] = {EGL_NONE};
    return eglCreateWindowSurface(mDisplay, mConfig, reinterpret_cast<EGLNativeWindowType>(window), attribs);
}

bool EGLHelper::makeCurrent(EGLSurface surface) {
    return eglMakeCurrent(mDisplay, surface, surface, mContext);
}
bool EGLHelper::swapBuffers(EGLSurface surface) { return eglSwapBuffers(mDisplay, surface); }
void EGLHelper::destroySurface(EGLSurface surface) { eglDestroySurface(mDisplay, surface); }
void EGLHelper::release() {
    if (mDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(mDisplay, mContext);
        eglTerminate(mDisplay);
    }
}

void EGLHelper::unbind() {
    if (mDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
}