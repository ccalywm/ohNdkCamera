//
// Created on 2026/7/13.
//
// Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// please include "napi/native_api.h".
#define LOG_TAG "OpenGLManager"

#include "OpenGLManager.h"

#include <chrono>
#include <unistd.h>
#include <ctime>

static void OnFrameAvailable(void* context) {
    auto* mgr = static_cast<OpenGLManager*>(context);
    if (mgr) mgr->OnFrameAvailableCallback();
}

// OpenGLManager& OpenGLManager::GetInstance() {
//     static OpenGLManager instance;
//     return instance;
// }

OpenGLManager::OpenGLManager() {
    LOGI("OpenGLManager constructed");
    // 初始化矩阵为单位矩阵
    memset(texMatrix_, 0, sizeof(texMatrix_));
    texMatrix_[0] = texMatrix_[5] = texMatrix_[10] = texMatrix_[15] = 1.0f;
}

OpenGLManager::~OpenGLManager() {
    Release();
}

bool OpenGLManager::Initialize(int32_t width, int32_t height) {
    if (initialized_) return true;

    defaultWidth_ = width;
    defaultHeight_ = height;

    // 1. 创建 EGLHelper
    eglHelper_ = std::make_unique<EGLHelper>();
    if (!eglHelper_->init()) {
        LOGE("EGLHelper init failed");
        return false;
    }

    // 2. 绑定 EGL 上下文（即使没有 surface，也需要激活上下文）
    if (!eglHelper_->makeCurrent(EGL_NO_SURFACE)) {
        LOGE("EGLHelper makeCurrent failed");
        eglHelper_->release();
        eglHelper_.reset();
        return false;
    }

    // 3. 创建 OES 纹理
    if (!CreateOESTexture()) {
        LOGE("CreateOESTexture failed");
        eglHelper_->release();
        eglHelper_.reset();
        return false;
    }

    // 4. 创建 GLRenderer
    renderer_ = std::make_unique<GLRenderer>();
    renderer_->prepare(oesTextureId_);

    // 设置视频尺寸（注意：鸿蒙摄像头方向可能需要宽高互换，这里保持与调用一致）
    // 安卓版本有 setVideoSize(w, h)，若需要可调用
    renderer_->setVideoSize(height, width); // 根据需要
    // renderer_->setVideoSize(width, height);

    // 5. 创建 NativeImage（输入源）
    if (!CreateNativeImage()) {
        LOGE("CreateNativeImage failed");
        DestroyNativeImage();
        eglHelper_->release();
        eglHelper_.reset();
        return false;
    }

    // 6. 设置初始变换：无旋转、无翻转（与安卓一致，由外部调用 SetTransform）
    // 不设置任何默认变换，保持单位矩阵
    SetTransform(180, true, false);  // 可以显式设为无变换，但默认已是0

    // 7. 解绑当前线程的上下文，等待渲染线程绑定
    eglHelper_->unbind();

    initialized_ = true;
    state_.store(RenderState::PAUSED);  // 初始化后处于暂停状态
    LOGI("OpenGLManager initialized (state=PAUSED)");
    return true;
}

bool OpenGLManager::Start() {
    if (state_.load() == RenderState::EXITING) {
        LOGE("Start called but already EXITING");
        return false;
    }
    if (!initialized_) {
        LOGE("Start called but not initialized");
        return false;
    }

    // 如果线程未启动，则创建渲染线程
    if (!renderThread_.joinable()) {
        renderThread_ = std::thread(&OpenGLManager::RenderLoop, this);
    }

    state_.store(RenderState::RUNNING);
    frameCond_.notify_all();
    LOGI("OpenGLManager started (RUNNING)");
    return true;
}

void OpenGLManager::Stop() {
    if (state_.load() == RenderState::EXITING) return;
    state_.store(RenderState::PAUSED);
    frameCond_.notify_all();
    LOGI("OpenGLManager paused (PAUSED)");
}

void OpenGLManager::Release() {
    RenderState expected = RenderState::EXITING;
    if (state_.exchange(expected) == RenderState::EXITING) {
        return; // 已经处于退出状态
    }

    frameCond_.notify_all();
    if (renderThread_.joinable()) {
        renderThread_.join();
    }

    // 释放所有输出 Surface
    {
        std::lock_guard<std::mutex> lock(outputsMutex_);
        for (auto& pair : outputs_) {
            if (pair.second.surface != EGL_NO_SURFACE) {
                eglHelper_->destroySurface(pair.second.surface);
            }
        }
        outputs_.clear();
    }

    // 清理 NativeImage
    DestroyNativeImage();

    // 清理渲染器
    if (renderer_) {
        renderer_->release();
        renderer_.reset();
    }

    // 释放 EGL
    if (eglHelper_) {
        eglHelper_->release();
        eglHelper_.reset();
    }

    initialized_ = false;
    LOGI("OpenGLManager released");
}

std::string OpenGLManager::GetInputSurfaceId() const {
    return inputSurfaceId_;
}

void OpenGLManager::AddOutput(OHNativeWindow* window, int32_t width, int32_t height) {
    if (!window) {
        LOGE("AddOutput: window is null");
        return;
    }
    LOGI("AddOutput: window=%p, size=%dx%d", window, width, height);

    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        taskQueue_.push([=]() {
            std::lock_guard<std::mutex> lockOut(outputsMutex_);
            auto it = outputs_.find(window);
            if (it != outputs_.end()) {
                // 已存在：更新尺寸
                it->second.width = width;
                it->second.height = height;
                LOGI("AddOutput: update existing output size %dx%d", width, height);
                return;
            }

            EGLSurface surf = EGL_NO_SURFACE;
            // 只有在 RUNNING 状态下才创建 EGLSurface
            if (state_.load() == RenderState::RUNNING) {
                surf = eglHelper_->createWindowSurface(window);
                if (surf == EGL_NO_SURFACE) {
                    LOGE("AddOutput: failed to create EGLSurface for window");
                    return;
                }
            }
            outputs_[window] = {surf, width, height};
            LOGI("AddOutput: added window %p, surface=%p, size=%dx%d", window, surf, width, height);
        });
    }
    frameCond_.notify_one();
}

void OpenGLManager::RemoveOutput(OHNativeWindow* window) {
    if (!window) return;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        taskQueue_.push([=]() {
            std::lock_guard<std::mutex> lockOut(outputsMutex_);
            auto it = outputs_.find(window);
            if (it != outputs_.end()) {
                if (it->second.surface != EGL_NO_SURFACE) {
                    eglHelper_->destroySurface(it->second.surface);
                }
                outputs_.erase(it);
                LOGI("RemoveOutput: removed window %p", window);
            }
        });
    }
    frameCond_.notify_one();
}

void OpenGLManager::UpdateOutputSize(OHNativeWindow* window, int32_t width, int32_t height) {
    if (!window) return;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        taskQueue_.push([=]() {
            std::lock_guard<std::mutex> lockOut(outputsMutex_);
            auto it = outputs_.find(window);
            if (it != outputs_.end()) {
                it->second.width = width;
                it->second.height = height;
                LOGI("UpdateOutputSize: %p -> %dx%d", window, width, height);
            }
        });
    }
    frameCond_.notify_one();
}

void OpenGLManager::SetTransform(int rotationDeg, bool flipH, bool flipV) {
    // 确保角度在 [0, 360) 并四舍五入到最近90度
    int deg = rotationDeg % 360;
    if (deg < 0) deg += 360;
    int rounded = ((deg + 45) / 90) * 90;
    if (rounded == 360) rounded = 0;

    rotationDeg_.store(rounded);
    flipH_.store(flipH);
    flipV_.store(flipV);
    if (renderer_) {
        renderer_->setTransform(rotationDeg, flipH, flipV);
    }
    LOGI("OpenGLManager::SetTransform: rotation=%d, flipH=%d, flipV=%d", rounded, flipH, flipV);
}

bool OpenGLManager::IsRunning() const {
    return state_.load() == RenderState::RUNNING;
}

// ========== 私有方法 ==========

void OpenGLManager::RenderLoop() {
    LOGI("RenderLoop started");

    if (!eglHelper_->makeCurrent(EGL_NO_SURFACE)) {
        LOGE("RenderLoop: makeCurrent failed, cannot continue");
        return;
    }
    LOGI("RenderLoop: EGL context bound");

    // 获取 EGL 时间戳扩展（可选，用于音视频同步）
    auto eglPresentationTimeANDROID = (PFNEGLPRESENTATIONTIMEANDROIDPROC)
        eglGetProcAddress("eglPresentationTimeANDROID");
    if (!eglPresentationTimeANDROID) {
        LOGI("eglPresentationTimeANDROID not available (ignored)");
    }

    while (true) {
        bool shouldRender = false;

        // 等待条件
        {
            std::unique_lock<std::mutex> lock(frameMutex_);
            frameCond_.wait(lock, [this] {
                if (state_.load() == RenderState::EXITING) return true;
                {
                    std::lock_guard<std::mutex> tLock(taskMutex_);
                    if (!taskQueue_.empty()) return true;
                }
                if (state_.load() == RenderState::RUNNING && frameCount_.load() > 0) return true;
                return false;
            });

            if (state_.load() == RenderState::EXITING) break;

            if (state_.load() == RenderState::RUNNING && frameCount_.load() > 0) {
                frameCount_.fetch_sub(1);
                shouldRender = true;
            }
        }

        // 处理任务（添加/移除输出）
        ProcessTasks();

        // 如果处于 PAUSED 状态，销毁所有输出 Surface
        if (state_.load() == RenderState::PAUSED) {
            std::lock_guard<std::mutex> lock(outputsMutex_);
            for (auto& pair : outputs_) {
                if (pair.second.surface != EGL_NO_SURFACE) {
                    eglHelper_->destroySurface(pair.second.surface);
                    pair.second.surface = EGL_NO_SURFACE;
                }
            }
            continue; // 跳过渲染
        }

        // 非 RUNNING 或无帧，继续等待
        if (state_.load() != RenderState::RUNNING || !shouldRender) {
            continue;
        }

        // --- 开始渲染 ---
        // 更新纹理
        int ret = OH_NativeImage_UpdateSurfaceImage(nativeImage_);
        if (ret != 0) {
            LOGE("OH_NativeImage_UpdateSurfaceImage failed, ret=%d", ret);
            continue;
        }

        ret = OH_NativeImage_GetTransformMatrixV2(nativeImage_, texMatrix_);
        if (ret != 0) {
            // 使用单位矩阵
            memset(texMatrix_, 0, sizeof(texMatrix_));
            texMatrix_[0] = texMatrix_[5] = texMatrix_[10] = texMatrix_[15] = 1.0f;
            LOGE("OH_NativeImage_GetTransformMatrixV2 failed, using identity");
        }
        hasTexMatrix_ = true;

        // 获取当前系统时间戳（用于录制同步）
        struct timespec now;
        clock_gettime(CLOCK_BOOTTIME, &now);
        long nowNs = now.tv_sec * 1000000000LL + now.tv_nsec;

        // 绘制到所有输出
        std::lock_guard<std::mutex> lock(outputsMutex_);
        if (outputs_.empty()) {
            LOGI("RenderLoop: no outputs");
            continue;
        }

        for (auto& pair : outputs_) {
            OutputInfo& info = pair.second;
            // 如果 Surface 为空（例如刚恢复时），重新创建
            if (info.surface == EGL_NO_SURFACE) {
                info.surface = eglHelper_->createWindowSurface(pair.first);
                if (info.surface == EGL_NO_SURFACE) {
                    LOGE("Recreate surface failed for window %p", pair.first);
                    continue;
                }
            }

            if (!eglHelper_->makeCurrent(info.surface)) {
                LOGE("makeCurrent failed for surface %p", info.surface);
                continue;
            }

            // 应用变换（用户主动设置）
            // renderer_->setTransform(rotationDeg_.load(), flipH_.load(), flipV_.load());

            // 绘制
            renderer_->draw(info.width, info.height, texMatrix_);

            // 设置时间戳（如果扩展可用）
            if (eglPresentationTimeANDROID) {
                eglPresentationTimeANDROID(eglHelper_->getDisplay(), info.surface, nowNs);
            }

            if (!eglHelper_->swapBuffers(info.surface)) {
                LOGE("swapBuffers failed for surface %p", info.surface);
            }
        }
        // 注意：不主动解绑上下文，与安卓行为一致
    }

    // ---- 线程退出清理 ----
    LOGI("RenderLoop exiting, cleaning up...");

    // 销毁所有输出 Surface
    {
        std::lock_guard<std::mutex> lock(outputsMutex_);
        for (auto& pair : outputs_) {
            if (pair.second.surface != EGL_NO_SURFACE) {
                eglHelper_->destroySurface(pair.second.surface);
                pair.second.surface = EGL_NO_SURFACE;
            }
        }
        outputs_.clear();
    }

    // 释放 NativeImage 和纹理
    DestroyNativeImage();

    // 释放渲染器
    if (renderer_) {
        renderer_->release();
        renderer_.reset();
    }

    // 释放 EGL
    if (eglHelper_) {
        eglHelper_->release();
        eglHelper_.reset();
    }

    initialized_ = false;
    LOGI("RenderLoop exited");
}

void OpenGLManager::ProcessTasks() {
    std::queue<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        tasks.swap(taskQueue_);
    }
    while (!tasks.empty()) {
        auto& task = tasks.front();
        task();
        tasks.pop();
    }
}

bool OpenGLManager::CreateOESTexture() {
    glGenTextures(1, &oesTextureId_);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTextureId_);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    LOGI("OES texture created: %u", oesTextureId_);
    return (oesTextureId_ != 0);
}

bool OpenGLManager::CreateNativeImage() {
    if (oesTextureId_ == 0) {
        LOGE("CreateNativeImage: OES texture not created");
        return false;
    }

    nativeImage_ = OH_NativeImage_Create(oesTextureId_, GL_TEXTURE_EXTERNAL_OES);
    if (!nativeImage_) {
        LOGE("OH_NativeImage_Create failed");
        return false;
    }

    int ret = OH_NativeImage_AttachContext(nativeImage_, oesTextureId_);
    if (ret != 0) {
        LOGE("OH_NativeImage_AttachContext failed, ret=%d", ret);
        OH_NativeImage_Destroy(&nativeImage_);
        nativeImage_ = nullptr;
        return false;
    }

    cameraInputWindow_ = OH_NativeImage_AcquireNativeWindow(nativeImage_);
    if (!cameraInputWindow_) {
        LOGE("OH_NativeImage_AcquireNativeWindow failed");
        OH_NativeImage_DetachContext(nativeImage_);
        OH_NativeImage_Destroy(&nativeImage_);
        nativeImage_ = nullptr;
        return false;
    }

    uint64_t surfaceId = 0;
    ret = OH_NativeImage_GetSurfaceId(nativeImage_, &surfaceId);
    if (ret != 0) {
        LOGE("OH_NativeImage_GetSurfaceId failed, ret=%d", ret);
        OH_NativeImage_DetachContext(nativeImage_);
        OH_NativeImage_Destroy(&nativeImage_);
        nativeImage_ = nullptr;
        return false;
    }
    inputSurfaceId_ = std::to_string(surfaceId);

    // 注册帧可用回调
    OH_OnFrameAvailableListener listener;
    listener.context = this;
    listener.onFrameAvailable = OnFrameAvailable;
    ret = OH_NativeImage_SetOnFrameAvailableListener(nativeImage_, listener);
    if (ret != 0) {
        LOGE("OH_NativeImage_SetOnFrameAvailableListener failed, ret=%d", ret);
        OH_NativeImage_DetachContext(nativeImage_);
        OH_NativeImage_Destroy(&nativeImage_);
        nativeImage_ = nullptr;
        return false;
    }

    LOGI("NativeImage created, inputSurfaceId=%s", inputSurfaceId_.c_str());
    return true;
}

void OpenGLManager::DestroyNativeImage() {
    if (nativeImage_) {
        OH_NativeImage_DetachContext(nativeImage_);
        OH_NativeImage_Destroy(&nativeImage_);
        nativeImage_ = nullptr;
        cameraInputWindow_ = nullptr;
        inputSurfaceId_.clear();
        LOGI("NativeImage destroyed");
    }
    if (oesTextureId_) {
        glDeleteTextures(1, &oesTextureId_);
        oesTextureId_ = 0;
    }
}

void OpenGLManager::OnFrameAvailableCallback() {
    // 增加帧计数，唤醒渲染线程
    frameCount_.fetch_add(1);
    frameCond_.notify_one();
}