// //
// // Created by Lixiaoyao on 2026/1/6.
// //
//
// #pragma once
//
//
// #include <functional>
// #include <queue>
// #include <mutex>
// #include <map>
// #include <thread>
// #include <string>
//
// #include "GLRenderer.h"
// #include "EGLHelper.h"
//
// #include <dlfcn.h>
//
//
// // 渲染状态机
// enum class RenderState {
//     PAUSED,     // 暂停：保留上下文，不渲染，销毁 Window Surface
//     RUNNING,    // 运行：正常渲染循环
//     EXITING     // 退出：清理所有资源并结束线程
// };
//
// // -----------------------------------------------------------
// // 管理类：多线程控制
// // -----------------------------------------------------------
// class GLManager {
// public:
//     // 构造函数：传入 JNI 环境、实例名称、是否启用 Native ST 模式
//     GLManager(JNIEnv *env, std::string name, bool nativeSurfaceTexture);
//
//     ~GLManager();
//
//     // --- 生命周期控制 ---
//     void start();
//
//     void stop();
//
//     void release();
//
//     // --- 输入源设置 ---
//     jobject getInputSurfaceTexture(JNIEnv *env, jobject cls);
//
//     jint getInputSurfaceTextureId();
//
//     void onFrameAvailable(); // Java 回调：新帧到达
//
//
//     // --- 输出目标管理 ---
//     void addOutput(ANativeWindow *window, int width, int height);
//
//     void removeOutput(ANativeWindow *window);
//
//     void updateOutputSize(ANativeWindow *window, int width, int height);
//
//
//     // --- 参数设置 ---
//     void setTransform(int rotation, bool mirrorH, bool mirrorV);
//
//     void setVideoSize(int w, int h);
//
//     void setFilterParams(float *params);
//
// private:
//     // 渲染线程主循环
//     void renderLoop();
//
//     void initNativeFunctions();
//
//     bool initEGL();
//
//     void createOES();
//
//     void initOther();
//
//
//     void processTasks(); // 处理队列中的任务
//
//     // 输出窗口信息结构
//     struct OutputInfo {
//         EGLSurface eglSurface = EGL_NO_SURFACE;
//         int width = 0;
//         int height = 0;
//     };
//
//     // 基础属性
//     std::string mName;
//     JavaVM *mJvm = nullptr;
//     bool mNativeMode = false; // 是否使用 Native SurfaceTexture
//
//     // OpenGL 辅助类
//     EglHelper *mEglHelper = nullptr;
//     GLRenderer *mRenderer = nullptr;
//
//     // 线程与同步
//     std::thread *mThread = nullptr;
//     std::mutex mLock;               // 状态锁
//     std::condition_variable mCond;  // 条件变量
//     // 核心状态机
//     std::atomic<RenderState> mState{RenderState::PAUSED};
//     // 帧同步计数器 (解决多线程信号丢失的核心)
//     int mFrameCount = 0;
//
//     // 任务队列 (解决线程安全的关键)
//     std::queue<std::function<void()>> mTaskQueue;
//     std::mutex mTaskLock;
//
//     // 输出 Surface 映射表
//     std::map<ANativeWindow *, OutputInfo> mOutputMap;
//
//     // --- 纹理相关 ---
//     GLuint mOesTextureId = 0;
//     float mTextureMatrix[16]{};
//     bool mFirstFrameReceived = false;
//
//
//
//     // --- JNI 与 Native Handle ---
//     ASurfaceTextureFunctions mNativeFuncs{};
//     ASurfaceTexture *mNativeST = nullptr; // API 28+ 使用
//
//     jobject mJavaStObj = nullptr;         // Global Ref: SurfaceTexture
//     jobject mJavaManagerObj = nullptr;    // Global Ref: Java 管理类实例
//     jmethodID mUpdateTexId = nullptr;     // Java 方法: updateTexImageAndGetMatrix
//     jfloatArray mJavaMatrixArr = nullptr; // Global Ref: 用于接收 Matrix
//
// };
//
//
