// //
// // Created on 2026/7/13.
// //
// // Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// // please include "napi/native_api.h".
//
// #ifndef EGL_HELPER_H
// #define EGL_HELPER_H
//
// #include <EGL/egl.h>
// #include <EGL/eglext.h>
// #include <native_window/external_window.h>
//
// class EGLHelper {
// public:
//     EGLHelper();
//     ~EGLHelper();
//
//     // 初始化 EGL 并选择配置，创建上下文（默认不创建 Surface）
//     bool init();
//
//     // 创建窗口 Surface（从 OHNativeWindow）
//     EGLSurface createWindowSurface(OHNativeWindow* window);
//
//     // 销毁 EGLSurface
//     void destroySurface(EGLSurface surface);
//
//     // 切换当前上下文到指定 Surface（如果 surface 为 EGL_NO_SURFACE，则解绑）
//     bool makeCurrent(EGLSurface surface);
//
//     // 交换缓冲区
//     bool swapBuffers(EGLSurface surface);
//
//     // 获取默认的“空”Surface（用于解绑），实际是 EGL_NO_SURFACE，但可以返回一个 dummy
//     EGLSurface getDefaultSurface() const { return dummySurface_; }
//
//     // 释放所有资源
//     void release();
//    
//      void unbind();   // 解绑当前线程的上下文
//
//     EGLDisplay getDisplay() const { return eglDisplay_; }
//     EGLContext getContext() const { return eglContext_; }
//     EGLConfig getConfig() const { return eglConfig_; }
//
// private:
//     EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
//     EGLContext eglContext_ = EGL_NO_CONTEXT;
//     EGLConfig eglConfig_ = nullptr;
//     EGLSurface dummySurface_ = EGL_NO_SURFACE; // 用于占位，实际未使用
//
//     bool initialized_;
// };
//
// #endif // EGL_HELPER_H
