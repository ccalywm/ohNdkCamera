//
// Created on 2026/7/13.
//
// Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// please include "napi/native_api.h".

#include "OpenGLManager.h"

#include <chrono>
#include <unistd.h>

static void OnFrameAvailable(void* context) {
    auto* mgr = static_cast<OpenGLManager*>(context);
    if (mgr) mgr->OnFrameAvailableCallback();
}

OpenGLManager& OpenGLManager::GetInstance() {
    static OpenGLManager instance;
    return instance;
}

OpenGLManager::OpenGLManager() {
    LOGI("OpenGLManager constructed");
    // 初始化矩阵为单位矩阵
    memset(texMatrix_, 0, sizeof(texMatrix_));
    texMatrix_[0] = texMatrix_[5] = texMatrix_[10] = texMatrix_[15] = 1.0f;
}

OpenGLManager::~OpenGLManager() {
    Stop();
}

bool OpenGLManager::Initialize(int32_t width, int32_t height) {
    if (initialized_.load()) return true;

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

    // 3. 创建 OES 纹理（现在有上下文了）
    if (!CreateOESTexture()) {
        LOGE("CreateOESTexture failed");
        eglHelper_->release();
        eglHelper_.reset();
        return false;
    }

    // 4. 创建 NativeImage（输入源）
    if (!CreateNativeImage()) {
        LOGE("CreateNativeImage failed");
        DestroyNativeImage();
        eglHelper_->release();
        eglHelper_.reset();
        return false;
    }

    // 5. 创建 GLRenderer
    renderer_ = std::make_unique<GLRenderer>();
    if (!renderer_->prepare(oesTextureId_)) {
        LOGE("GLRenderer prepare failed");
        DestroyNativeImage();
        eglHelper_->release();
        eglHelper_.reset();
        return false;
    }

    // 6. 设置初始变换（默认无变换）
    // renderer_->setTransform(180, true, false);
    SetTransform(180, true, false);  // 同时更新原子变量和渲染器，并设置 userSetTransform_=t
    // SetTransform(0, false, false);  // 同时更新原子变量和渲染器，并设置 userSetTransform_=t
    // userSetTransform_.store(true);  // 默认用户未设置
    // 资源创建完成后，解绑当前线程的上下文，以便渲染线程可以重新绑定
    eglHelper_->unbind();
    
    
    initialized_.store(true);
    LOGI("OpenGLManager initialized");
    

    
    return true;
}

bool OpenGLManager::Start() {
    if (running_.load()) return true;
    if (!initialized_.load()) {
        LOGE("Not initialized, call Initialize first");
        return false;
    }

    running_.store(true);
    renderThread_ = std::thread(&OpenGLManager::RenderLoop, this);
    LOGI("OpenGLManager started");
    return true;
}

void OpenGLManager::Stop() {
    if (!running_.load()) return;

    running_.store(false);
    frameCond_.notify_all(); // 唤醒渲染线程

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
            // 注意：不释放 OHNativeWindow，由调用者管理
        }
        outputs_.clear();
    }

    // 清理 NativeImage
    DestroyNativeImage();

    // 清理渲染器
    if (renderer_) renderer_->release();
    renderer_.reset();

    // 释放 EGL
    if (eglHelper_) eglHelper_->release();
    eglHelper_.reset();

    initialized_.store(false);
    LOGI("OpenGLManager stopped");
}

std::string OpenGLManager::GetInputSurfaceId() const {
    return inputSurfaceId_;
}

void OpenGLManager::AddOutput(OHNativeWindow* window, int32_t width, int32_t height) {
    if (!window) {
        LOGE("AddOutput: window is null");
        return;
    }
    LOGI("AddOutput called: window=%p, size=%dx%d", window, width, height);
    // 放入任务队列，在渲染线程执行创建 EGLSurface
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
            } else {
                // 创建 EGLSurface
                EGLSurface surf = eglHelper_->createWindowSurface(window);
                if (surf == EGL_NO_SURFACE) {
                    LOGE("AddOutput: failed to create EGLSurface for window");
                    return;
                }
                outputs_[window] = {surf, width, height};
                LOGI("AddOutput: added window %p, size %dx%d", window, width, height);
            }
        });
    }
    // 唤醒渲染线程处理任务
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
    userSetTransform_.store(true);  // 标记用户已主动设置
    LOGI("OpenGLManager::SetTransform: rotation=%d, flipH=%d, flipV=%d, userSet=true", rounded, flipH, flipV);
}

bool OpenGLManager::IsRunning() const {
    return running_.load();
}

// ========== 私有方法 ==========
void OpenGLManager::RenderLoop() {
    LOGI("RenderLoop started");

    if (!eglHelper_->makeCurrent(EGL_NO_SURFACE)) {
        LOGE("RenderLoop: makeCurrent failed, cannot continue");
        return;
    }
    LOGI("RenderLoop: EGL context bound");

    frameCount_.store(0);

    while (running_.load()) {
        bool hasFrame = false;
        {
            std::unique_lock<std::mutex> lock(frameMutex_);
            frameCond_.wait(lock, [this] {
                return frameCount_.load() > 0 || !running_.load() || !taskQueue_.empty();
            });
            if (!running_.load()) break;
            if (frameCount_.load() > 0) {
                frameCount_.fetch_sub(1);
                hasFrame = true;
            }
        }

        if (!running_.load()) break;
        ProcessTasks();

        if (!hasFrame) {
            continue;
        }

        // 更新纹理
        int ret = OH_NativeImage_UpdateSurfaceImage(nativeImage_);
        if (ret != 0) {
            LOGE("UpdateSurfaceImage failed, ret=%d", ret);
            continue;
        }

        ret = OH_NativeImage_GetTransformMatrixV2(nativeImage_, texMatrix_);
        if (ret != 0) {
            memset(texMatrix_, 0, sizeof(texMatrix_));
            texMatrix_[0] = texMatrix_[5] = texMatrix_[10] = texMatrix_[15] = 1.0f;
            LOGE("GetTransformMatrixV2 failed, using identity");
        }
        hasTexMatrix_ = true;

        int rot = rotationDeg_.load();
        bool fh = flipH_.load();
        bool fv = flipV_.load();
        renderer_->setTransform(rot, fh, fv);

        std::lock_guard<std::mutex> lock(outputsMutex_);
        if (outputs_.empty()) {
            LOGI("RenderLoop: no outputs");
            continue;
        }

        for (auto& pair : outputs_) {
            OutputInfo& info = pair.second;
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
            LOGI("Drawing to surface %p, size %dx%d", info.surface, info.width, info.height);
            renderer_->draw(info.width, info.height, texMatrix_);
            if (!eglHelper_->swapBuffers(info.surface)) {
                LOGE("swapBuffers failed for surface %p", info.surface);
            }
        }
        eglHelper_->makeCurrent(EGL_NO_SURFACE);
    }

    // 清理...
    LOGI("RenderLoop exited");
}
// void OpenGLManager::RenderLoop() {
//     LOGI("RenderLoop started");
//
//     if (!eglHelper_->makeCurrent(EGL_NO_SURFACE)) {
//         LOGE("RenderLoop: makeCurrent failed, cannot continue");
//         return;
//     }
//     LOGI("RenderLoop: EGL context bound");
//
//     frameCount_.store(0);
//
//     while (running_.load()) {
//         bool hasFrame = false;
//         {
//             std::unique_lock<std::mutex> lock(frameMutex_);
//             frameCond_.wait(lock, [this] {
//                 return frameCount_.load() > 0 || !running_.load() || !taskQueue_.empty();
//             });
//             if (!running_.load()) break;
//             if (frameCount_.load() > 0) {
//                 frameCount_.fetch_sub(1);
//                 hasFrame = true;
//             }
//         }
//
//         if (!running_.load()) break;
//         ProcessTasks();
//
//         if (!hasFrame) {
//             continue;
//         }
//
//         // 更新纹理
//         int ret = OH_NativeImage_UpdateSurfaceImage(nativeImage_);
//         if (ret != 0) {
//             LOGE("UpdateSurfaceImage failed, ret=%d", ret);
//             continue;
//         }
//
//         ret = OH_NativeImage_GetTransformMatrixV2(nativeImage_, texMatrix_);
//         if (ret != 0) {
//             memset(texMatrix_, 0, sizeof(texMatrix_));
//             texMatrix_[0] = texMatrix_[5] = texMatrix_[10] = texMatrix_[15] = 1.0f;
//             LOGE("GetTransformMatrixV2 failed, using identity");
//         }
//         hasTexMatrix_ = true;
//
//         // ---- 读取变换参数 ----
//         int rot = rotationDeg_.load();
//         bool fh = flipH_.load();
//         bool fv = flipV_.load();
//         bool userSet = userSetTransform_.load();
//
//         // 如果用户没有主动设置，且旋转为0，默认应用180度修正画面颠倒
//         // 这样即使 setTransform 未调用，画面也是正的
//         if (!userSet && rot == 0) {
//             rot = 180;
//         //     LOGI("RenderLoop: applying default 180-degree rotation (user not set)");
//         // } else {
//         //     LOGI("RenderLoop: rotation=%d, flipH=%d, flipV=%d, userSet=%d", rot, fh, fv, userSet);
//         }
//
//         // 应用变换到渲染器
//         renderer_->setTransform(rot, fh, fv);
//
//         // 绘制输出
//         std::lock_guard<std::mutex> lock(outputsMutex_);
//         if (outputs_.empty()) continue;
//
//         for (auto& pair : outputs_) {
//             OutputInfo& info = pair.second;
//             if (info.surface == EGL_NO_SURFACE) {
//                 info.surface = eglHelper_->createWindowSurface(pair.first);
//                 if (info.surface == EGL_NO_SURFACE) {
//                     LOGE("Recreate surface failed");
//                     continue;
//                 }
//             }
//             if (!eglHelper_->makeCurrent(info.surface)) {
//                 LOGE("makeCurrent failed");
//                 continue;
//             }
//             renderer_->draw(info.width, info.height, texMatrix_);
//             if (!eglHelper_->swapBuffers(info.surface)) {
//                 LOGE("swapBuffers failed");
//             }
//         }
//         eglHelper_->makeCurrent(EGL_NO_SURFACE);
//     }
//
//     // 清理
//     {
//         std::lock_guard<std::mutex> lock(outputsMutex_);
//         for (auto& pair : outputs_) {
//             if (pair.second.surface != EGL_NO_SURFACE) {
//                 eglHelper_->destroySurface(pair.second.surface);
//             }
//         }
//         outputs_.clear();
//     }
//     LOGI("RenderLoop exited");
// }

void OpenGLManager::ProcessTasks() {
    std::queue<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        tasks.swap(taskQueue_);
    }
    // LOGI("ProcessTasks: processing %zu tasks", tasks.size());
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
    return true;
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
        // 可以不视为致命错误，退化为轮询？
        // 但为了可靠性，我们仍然继续，只是会退化为无回调，需要轮询，但我们使用条件变量，需要外部触发
        // 这里简单处理：如果注册失败，我们仍然认为创建成功，但帧可用信号需由其他方式触发
        // 不过标准实现中回调是必须的，因此我们返回 false 表示失败
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
