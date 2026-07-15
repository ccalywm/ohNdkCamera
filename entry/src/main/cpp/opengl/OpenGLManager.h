//
// Created on 2026/7/13.
//
// Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// please include "napi/native_api.h".

#pragma once

#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>
#include <map>
#include <condition_variable>
#include <queue>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <native_image/native_image.h>
#include <native_window/external_window.h>
#include <native_buffer/native_buffer.h>

#include "util/DebugLog.h"
#include "EGLHelper.h"
#include "GLRenderer.h"

/**
 * 渲染状态机
 */
enum class RenderState {
    PAUSED,     // 暂停：保留EGL上下文和纹理，销毁所有输出Surface
    RUNNING,    // 运行中：正常渲染
    EXITING     // 退出：释放所有资源，结束线程
};

/**
 * OpenGLManager - 多输出渲染引擎
 * 
 * 职责：
 *   1. 接收来自摄像头（NativeImage）的 OES 纹理输入。
 *   2. 管理多个输出目标（OHNativeWindow），每个输出独立渲染。
 *   3. 提供旋转/翻转变换控制。
 *   4. 线程安全，支持动态添加/移除输出。
 */
class OpenGLManager {
public:
    static OpenGLManager& GetInstance();

    /**
     * 初始化：创建 EGL 上下文、OES 纹理、NativeImage 等
     * @param width  默认输出宽度（用于初始化 FBO 等，但实际每个输出有自己的尺寸）
     * @param height 默认输出高度
     * @return true 成功
     */
    bool Initialize(int32_t width, int32_t height);

    /**
     * 启动渲染线程（设置为 RUNNING 状态）
     * 若线程已创建，则唤醒并继续渲染
     */
    bool Start();

    /**
     * 暂停渲染（设置为 PAUSED 状态）
     * 销毁所有输出 Surface，但保留 EGL 上下文和纹理
     */
    void Stop();

    /**
     * 完全释放所有资源，结束渲染线程
     * 调用后对象不可再使用，需重新初始化
     */
    void Release();

    /**
     * 获取摄像头输入 Surface ID（给相机使用）
     */
    std::string GetInputSurfaceId() const;

    /**
     * 添加输出目标
     * @param window  OHNativeWindow*（由调用者管理，不要重复释放）
     * @param width   输出宽度
     * @param height  输出高度
     */
    void AddOutput(OHNativeWindow* window, int32_t width, int32_t height);

    /**
     * 移除输出目标
     */
    void RemoveOutput(OHNativeWindow* window);

    /**
     * 更新输出尺寸（宽高变化时调用）
     */
    void UpdateOutputSize(OHNativeWindow* window, int32_t width, int32_t height);

    /**
     * 设置变换参数（对所有输出统一生效）
     * @param rotationDeg 旋转角度（0/90/180/270，自动四舍五入到最近90度）
     * @param flipH       水平翻转
     * @param flipV       垂直翻转
     */
    void SetTransform(int rotationDeg, bool flipH, bool flipV);

    /**
     * 是否处于运行状态（RUNNING）
     */
    bool IsRunning() const;
    
    // 帧可用回调处理（由 NativeImage 回调触发）
    void OnFrameAvailableCallback();

private:
    OpenGLManager();
    ~OpenGLManager();
    OpenGLManager(const OpenGLManager&) = delete;
    OpenGLManager& operator=(const OpenGLManager&) = delete;

    // 渲染线程主循环
    void RenderLoop();

    // 处理任务队列中的任务（添加/移除输出等）
    void ProcessTasks();

    // 创建 OES 纹理
    bool CreateOESTexture();

    // 创建 NativeImage
    bool CreateNativeImage();

    // 销毁 NativeImage
    void DestroyNativeImage();

    // EGL 时间戳扩展函数指针（用于音视频同步）
    typedef void (*PFNEGLPRESENTATIONTIMEANDROIDPROC)(EGLDisplay, EGLSurface, khronos_stime_nanoseconds_t);

    // EGL 工具类
    std::unique_ptr<EGLHelper> eglHelper_;
    std::unique_ptr<GLRenderer> renderer_;

    // NativeImage 相关
    OH_NativeImage* nativeImage_ = nullptr;
    GLuint oesTextureId_ = 0;
    std::string inputSurfaceId_;
    OHNativeWindow* cameraInputWindow_ = nullptr; // 暂未使用

    // 输出映射：OHNativeWindow* → { EGLSurface, width, height }
    struct OutputInfo {
        EGLSurface surface = EGL_NO_SURFACE;
        int32_t width = 0;
        int32_t height = 0;
    };
    std::map<OHNativeWindow*, OutputInfo> outputs_;
    std::mutex outputsMutex_;

    // 任务队列 (用于线程安全添加/移除输出)
    std::queue<std::function<void()>> taskQueue_;
    std::mutex taskMutex_;

    // 线程控制
    std::thread renderThread_;
    std::atomic<RenderState> state_{RenderState::PAUSED};
    bool initialized_ = false;          // 资源是否已初始化

    // 帧同步
    std::mutex frameMutex_;
    std::condition_variable frameCond_;
    std::atomic<int> frameCount_{0};    // 帧可用计数

    // 默认尺寸（仅用于初始化）
    int32_t defaultWidth_ = 1280;
    int32_t defaultHeight_ = 720;

    // 变换参数
    std::atomic<int> rotationDeg_{0};
    std::atomic<bool> flipH_{false};
    std::atomic<bool> flipV_{false};

    // 纹理矩阵（从 NativeImage 获取）
    float texMatrix_[16];
    bool hasTexMatrix_ = false;
};
// //
// // Created on 2026/7/13.
// //
// // Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// // please include "napi/native_api.h".
//
// #pragma once
//
// #include <string>
// #include <mutex>
// #include <thread>
// #include <atomic>
// #include <cstdint>
// #include <map>
// #include <condition_variable>
// #include <queue>
//
// #include <EGL/egl.h>
// #include <EGL/eglext.h>
// #include <GLES3/gl3.h>
// #include <GLES2/gl2ext.h>
//
// #include <native_image/native_image.h>
// #include <native_window/external_window.h>
// #include <native_buffer/native_buffer.h>
//
// #include "util/DebugLog.h"
// // #include "EGLHelper.h"
// // #include "gl/EGLHelper.h"
// // #include "GLRenderer.h"
// #include "gl/EGLHelper.h"
// #include "gl/GLRenderer.h"
//
// /**
//  * OpenGLManager - 多输出渲染引擎
//  * 
//  * 职责：
//  *   1. 接收来自摄像头（NativeImage）的 OES 纹理输入。
//  *   2. 管理多个输出目标（OHNativeWindow），每个输出独立渲染。
//  *   3. 提供旋转/翻转变换控制。
//  *   4. 线程安全，支持动态添加/移除输出。
//  */
// class OpenGLManager {
// public:
//     static OpenGLManager& GetInstance();
//
//     /**
//      * 初始化：创建 EGL 上下文、OES 纹理、NativeImage 等
//      * @param width  默认输出宽度（用于初始化 FBO 等，但实际每个输出有自己的尺寸）
//      * @param height 默认输出高度
//      * @return true 成功
//      */
//     bool Initialize(int32_t width, int32_t height);
//
//     /**
//      * 启动渲染线程
//      */
//     bool Start();
//
//     /**
//      * 停止渲染线程，释放资源
//      */
//     void Stop();
//
//     /**
//      * 获取摄像头输入 Surface ID（给相机使用）
//      */
//     std::string GetInputSurfaceId() const;
//
//     /**
//      * 添加输出目标
//      * @param window  OHNativeWindow*（由调用者管理，不要重复释放）
//      * @param width   输出宽度
//      * @param height  输出高度
//      */
//     void AddOutput(OHNativeWindow* window, int32_t width, int32_t height);
//
//     /**
//      * 移除输出目标
//      */
//     void RemoveOutput(OHNativeWindow* window);
//
//     /**
//      * 更新输出尺寸（宽高变化时调用）
//      */
//     void UpdateOutputSize(OHNativeWindow* window, int32_t width, int32_t height);
//
//     /**
//      * 设置变换参数（对所有输出统一生效）
//      */
//     void SetTransform(int rotationDeg, bool flipH, bool flipV);
//
//     /**
//      * 是否运行中
//      */
//     bool IsRunning() const;
//    
//     // 帧可用回调处理
//     void OnFrameAvailableCallback();
// private:
//     OpenGLManager();
//     ~OpenGLManager();
//     OpenGLManager(const OpenGLManager&) = delete;
//     OpenGLManager& operator=(const OpenGLManager&) = delete;
//
//     // 渲染线程主循环
//     void RenderLoop();
//
//     // 处理任务队列中的任务（添加/移除输出等）
//     void ProcessTasks();
//
//     // 创建 OES 纹理
//     bool CreateOESTexture();
//
//     // 创建 NativeImage
//     bool CreateNativeImage();
//
//     // 销毁 NativeImage
//     void DestroyNativeImage();
//
//
//
//     // EGL 工具类
//     std::unique_ptr<EGLHelper> eglHelper_;
//     std::unique_ptr<GLRenderer> renderer_;
//
//     // NativeImage 相关
//     OH_NativeImage* nativeImage_ = nullptr;
//     GLuint oesTextureId_ = 0;
//     std::string inputSurfaceId_;
//     OHNativeWindow* cameraInputWindow_ = nullptr; // 暂时不用，但保留
//
//     // 输出映射：OHNativeWindow* → { EGLSurface, width, height }
//     struct OutputInfo {
//         EGLSurface surface = EGL_NO_SURFACE;
//         int32_t width = 0;
//         int32_t height = 0;
//     };
//     std::map<OHNativeWindow*, OutputInfo> outputs_;
//     std::mutex outputsMutex_;
//
//     // 任务队列 (用于线程安全添加/移除输出)
//     std::queue<std::function<void()>> taskQueue_;
//     std::mutex taskMutex_;
//
//     // 线程控制
//     std::thread renderThread_;
//     std::atomic<bool> running_{false};
//     std::atomic<bool> initialized_{false};
//
//     // 帧同步
//     std::mutex frameMutex_;
//     std::condition_variable frameCond_;
//     std::atomic<int> frameCount_{0};  // 帧可用计数
//
//     // 默认尺寸（仅用于初始化）
//     int32_t defaultWidth_ = 1280;
//     int32_t defaultHeight_ = 720;
//
//     // 变换参数
//     std::atomic<int> rotationDeg_{0};
//     std::atomic<bool> flipH_{false};
//     std::atomic<bool> flipV_{false};
//     std::atomic<bool> userSetTransform_{false};  // 记录用户是否主动设置过
//    
//     // 纹理矩阵（从 NativeImage 获取）
//     float texMatrix_[16];
//     bool hasTexMatrix_ = false;
// };
//
