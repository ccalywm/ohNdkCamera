#ifndef OPENGL_MANAGER_H
#define OPENGL_MANAGER_H

#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <native_image/native_image.h>
#include <native_window/external_window.h>
#include <native_buffer/native_buffer.h>

#include "util/DebugLog.h"

/**
 * OpenGLManager - 单例类
 *
 * 职责：在摄像头预览管线中插入 OpenGL 图像处理环节
 *
 * 数据流：
 *   摄像头 → NativeImage 的 NativeWindow（生产者端，零拷贝输入）
 *         → OES 纹理采样（OH_NativeImage_UpdateSurfaceImage 更新）
 *         → 自定义 Fragment Shader 处理（滤镜）
 *         → EGL 渲染到 XComponent 的 Surface（屏幕显示）
 */
class OpenGLManager {
public:
    static OpenGLManager& GetInstance();

    /**
     * 初始化 OpenGL 管线并启动渲染线程
     * @param xComponentSurfaceId  ArkTS 层 XComponent 的 surfaceId（渲染输出目标）
     * @param width                渲染表面宽度（像素）
     * @param height               渲染表面高度（像素）
     * @return true=成功启动, false=失败
     */
    bool Start(const std::string& xComponentSurfaceId, int32_t width, int32_t height);

    /**
     * 停止渲染管线并释放所有资源
     */
    void Stop();

    /**
     * 获取摄像头预览输出应使用的 Surface ID
     * 该 Surface 由 NativeImage 的 NativeWindow 创建，摄像头写入后 OES 纹理自动更新
     * @return Surface ID 字符串，未初始化时返回空字符串
     */
    std::string GetInputSurfaceId() const;

    /**
     * 更新渲染目标尺寸（窗口大小变化时调用）
     */
    void UpdateSize(int32_t width, int32_t height);

    /**
     * 查询当前管线是否处于运行状态
     */
    bool IsRunning() const;

private:
    OpenGLManager();
    ~OpenGLManager();
    OpenGLManager(const OpenGLManager&) = delete;
    OpenGLManager& operator=(const OpenGLManager&) = delete;

    // EGL 环境管理
    bool InitEGL(EGLNativeWindowType window);
    void DestroyEGL();

    // GPU 资源管理
    GLuint CreateOESTexture();
    GLuint CreateShaderProgram();
    bool CreateFBO();
    bool InitGLResources();
    void DestroyGLResources();

    // NativeImage 管理
    bool CreateNativeImage();
    void DestroyNativeImage();

    // 渲染
    void RenderLoop();
    bool RenderFrame();

    // EGL 环境
    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    EGLSurface eglSurface_ = EGL_NO_SURFACE;
    EGLConfig  eglConfig_  = nullptr;

    // NativeWindow
    OHNativeWindow* xComponentWindow_  = nullptr;  // XComponent 的 NativeWindow（EGL 渲染目标）
    OHNativeWindow* cameraInputWindow_ = nullptr;   // NativeImage 的 NativeWindow（摄像头写入端）

    // NativeImage（摄像头数据零拷贝输入）
    OH_NativeImage* nativeImage_  = nullptr;
    uint32_t       oesTextureId_ = 0;
    std::string    inputSurfaceId_;

    // GL 资源
    GLuint shaderProgram_ = 0;
    GLuint fbo_           = 0;
    GLuint fboTexture_    = 0;
    GLuint vao_           = 0;
    GLuint vbo_           = 0;
    GLuint ebo_           = 0;

    // Shader Uniform 位置
    GLint uTextureLoc_    = -1;
    GLint uTexMatrixLoc_  = -1;
    GLint uTimeLoc_       = -1;
    GLint uResolutionLoc_ = -1;

    // 尺寸
    int32_t surfaceWidth_  = 0;
    int32_t surfaceHeight_ = 0;

    // 线程控制
    std::thread       renderThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> sizeChanged_{false};
    mutable std::mutex mutex_;

    // 帧计时
    float startTime_ = 0.0f;
    std::atomic<int32_t> frameCount_{0};
};

#endif // OPENGL_MANAGER_H