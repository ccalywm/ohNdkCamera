//
// Created on 2026/7/13.
//
// Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// please include "napi/native_api.h".

#include "EGLHelper.h"
#include "util/DebugLog.h"

EGLHelper::EGLHelper()
    : eglDisplay_(EGL_NO_DISPLAY),
      eglContext_(EGL_NO_CONTEXT),
      eglConfig_(nullptr),
      dummySurface_(EGL_NO_SURFACE),
      initialized_(false) {}

EGLHelper::~EGLHelper() {
    release();
}

bool EGLHelper::init() {
    if (initialized_) return true;

    eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay_ == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(eglDisplay_, &major, &minor)) {
        LOGE("eglInitialize failed");
        return false;
    }

    // 配置属性
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE
    };

    EGLint numConfigs;
    if (!eglChooseConfig(eglDisplay_, configAttribs, &eglConfig_, 1, &numConfigs) || numConfigs == 0) {
        LOGE("eglChooseConfig failed");
        return false;
    }

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, contextAttribs);
    if (eglContext_ == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed");
        return false;
    }

    // 创建一个 dummy Pbuffer Surface 用于解绑上下文时使用（可选）
    EGLint pbufferAttribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    dummySurface_ = eglCreatePbufferSurface(eglDisplay_, eglConfig_, pbufferAttribs);
    if (dummySurface_ == EGL_NO_SURFACE) {
        LOGE("eglCreatePbufferSurface failed");
        // 继续，后面可以用 EGL_NO_SURFACE 解绑
    }

    initialized_ = true;
    LOGI("EGLHelper initialized");
    return true;
}

EGLSurface EGLHelper::createWindowSurface(OHNativeWindow* window) {
    if (!initialized_ || window == nullptr) {
        LOGE("createWindowSurface: not initialized or window null");
        return EGL_NO_SURFACE;
    }

    EGLSurface surface = eglCreateWindowSurface(eglDisplay_, eglConfig_,
                                                reinterpret_cast<EGLNativeWindowType>(window),
                                                nullptr);
    if (surface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed, error=%x", eglGetError());
    }
    return surface;
}

void EGLHelper::destroySurface(EGLSurface surface) {
    if (surface != EGL_NO_SURFACE && eglDisplay_ != EGL_NO_DISPLAY) {
        eglDestroySurface(eglDisplay_, surface);
    }
}

bool EGLHelper::makeCurrent(EGLSurface surface) {
    if (!initialized_) return false;
    // 如果 surface 为 EGL_NO_SURFACE，使用 dummySurface_ 作为 read/draw，或者直接传 EGL_NO_SURFACE
    EGLSurface surf = (surface == EGL_NO_SURFACE) ? dummySurface_ : surface;
    if (surf == EGL_NO_SURFACE) {
        // 如果没有 dummy，直接用 EGL_NO_SURFACE，某些实现可能不支持
        return eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, eglContext_);
    }
    return eglMakeCurrent(eglDisplay_, surf, surf, eglContext_);
}

bool EGLHelper::swapBuffers(EGLSurface surface) {
    if (surface == EGL_NO_SURFACE) return false;
    return eglSwapBuffers(eglDisplay_, surface);
}

void EGLHelper::release() {
    if (!initialized_) return;

    if (dummySurface_ != EGL_NO_SURFACE) {
        eglDestroySurface(eglDisplay_, dummySurface_);
        dummySurface_ = EGL_NO_SURFACE;
    }

    if (eglContext_ != EGL_NO_CONTEXT) {
        eglDestroyContext(eglDisplay_, eglContext_);
        eglContext_ = EGL_NO_CONTEXT;
    }

    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglTerminate(eglDisplay_);
        eglDisplay_ = EGL_NO_DISPLAY;
    }

    initialized_ = false;
    LOGI("EGLHelper released");
}

void EGLHelper::unbind() {
    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
}