// //
// // Created by Lixiaoyao on 2026/1/6.
// //
// #include <EGL/egl.h>
// #include <unistd.h>
// #include "GLManager.h"
// #include "util/DebugLog.h"
// #include <ctime>
//
// // EGL 扩展函数定义：用于设置时间戳，保证录制同步
// // typedef void (*PFNEGLPRESENTATIONTIMEANDROIDPROC)(EGLDisplay display, EGLSurface surface,
// //                                                   khronos_stime_nanoseconds_t time);
//
//
// // ======================= GLManager 实现 (核心) =======================
// GLManager::GLManager(JNIEnv *env, std::string name, bool nativeSurfaceTexture)
//         : mName(std::move(name)), mNativeMode(nativeSurfaceTexture) {
//     env->GetJavaVM(&mJvm);
//     initNativeFunctions(); // 加载 Android 库函数
//
//     // 初始化辅助类
//     mEglHelper = new EglHelper();
//     mRenderer = new GLRenderer();
//
//     // 初始化矩阵为单位矩阵
//     for (int i = 0; i < 16; i++) mTextureMatrix[i] = (i % 5 == 0) ? 1 : 0;
//
//     // 启动渲染线程
//     mState = RenderState::PAUSED;
//     mThread = new std::thread(&GLManager::renderLoop, this);
//
//     MYLOGD(mName.c_str(),"[%s] Constructed via C++", mName.c_str());
// }
//
// GLManager::~GLManager() {
//     if (mState != RenderState::EXITING) release();
//     if (mThread && mThread->joinable()) {
//         mThread->join(); // 等待 renderLoop 彻底结束
//         delete mThread;
//         mThread = nullptr;
//         if (mNativeFuncs.handle) {
//             dlclose(mNativeFuncs.handle);
//             mNativeFuncs = {};
//         }
//         MYLOGD(mName.c_str(),"GLManager: Thread joined");
//     }
// }
//
// void GLManager::initNativeFunctions() {
//     if (!mNativeMode) {
//         return;
//     }
//
//     // Android 9 才支持 需要判断，获取当前版本
//     int androidVersion = android_get_device_api_level();
//     MYLOGD(mName.c_str(),"GLManager: Android API:%d", androidVersion);
//     if (androidVersion < 28) {
//         return;
//     }
//     // 清零结构体
//     mNativeFuncs = {};
//     mNativeFuncs.handle = dlopen("libandroid.so", RTLD_NOW);
//     if (!mNativeFuncs.handle) {
//         MYLOGD(mName.c_str(),"Failed to dlopen libandroid.so");
//         mNativeMode = false;
//         return;
//     }
//
//     // 动态加载符号
//     mNativeFuncs.fromSurfaceTexture = (ASurfaceTextureFunctions::fromSurfaceTexture_t) dlsym(
//             mNativeFuncs.handle, "ASurfaceTexture_fromSurfaceTexture");
//     mNativeFuncs.updateTexImage = (ASurfaceTextureFunctions::updateTexImage_t) dlsym(
//             mNativeFuncs.handle, "ASurfaceTexture_updateTexImage");
//     mNativeFuncs.getTransformMatrix = (ASurfaceTextureFunctions::getTransformMatrix_t) dlsym(
//             mNativeFuncs.handle, "ASurfaceTexture_getTransformMatrix");
//     mNativeFuncs.release = (ASurfaceTextureFunctions::release_t) dlsym(mNativeFuncs.handle,
//                                                                        "ASurfaceTexture_release");
//
//     if (!mNativeFuncs.fromSurfaceTexture || !mNativeFuncs.updateTexImage) {
//         MYLOGD(mName.c_str(),"Failed to load ASurfaceTexture symbols");
//         dlclose(mNativeFuncs.handle);
//         mNativeFuncs = {};
//         mNativeMode = false;
//     } else {
//         mNativeFuncs.loaded = true;
//         MYLOGD(mName.c_str(),"ASurfaceTexture functions loaded successfully");
//     }
// }
//
// void GLManager::start() {
//     std::lock_guard<std::mutex> lock(mLock);
//     if (mState == RenderState::EXITING) return;
//
//     mState = RenderState::RUNNING;
//     mCond.notify_all(); // 唤醒线程开始干活
//     MYLOGD(mName.c_str(),"GLManager: State -> RUNNING");
// }
//
// void GLManager::stop() {
//     std::lock_guard<std::mutex> lock(mLock);
//     if (mState == RenderState::EXITING) return;
//
//     mState = RenderState::PAUSED;
//     mCond.notify_all(); // 唤醒线程去执行暂停逻辑（销毁 Surface）
//     MYLOGD(mName.c_str(),"GLManager: State -> PAUSED");
// }
//
//
// void GLManager::addOutput(ANativeWindow *window, int width, int height) {
//     MYLOGD(mName.c_str(),"AddOutput: window already exists, update size w:%d,h:%d", width, height);
//     std::lock_guard<std::mutex> lock(mTaskLock);
//     mTaskQueue.push([=]() {
//         auto it = mOutputMap.find(window);
//         if (it != mOutputMap.end()) {
//             // 已存在：只更新宽高，并释放多余的引用
//             it->second.width = width;
//             it->second.height = height;
//             ANativeWindow_release(window); // 抵消 JNI 的 acquire
//             MYLOGD(mName.c_str(),"AddOutput: window already exists, update size w:%d,h:%d", width, height);
//         } else {
//             // 新增：如果当前是 RUNNING，立即创建 EGLSurface
//             // 如果是 PAUSED，暂不创建，等切到 RUNNING 时统一创建
//             EGLSurface s = EGL_NO_SURFACE;
//             if (mState == RenderState::RUNNING) {
//                 s = mEglHelper->createWindowSurface(window);
//             }
//
//             if (mState == RenderState::RUNNING && s == EGL_NO_SURFACE) {
//                 MYLOGD(mName.c_str(),"AddOutput failed to create surface");
//                 ANativeWindow_release(window);
//             } else {
//                 // 存入 Map (PAUSED 状态下 s 为 NO_SURFACE，这是合法的)
//                 mOutputMap[window] = {s, width, height};
//             }
//         }
//     });
//     // 即使是 Pause 状态，也唤醒一下处理任务
//     mCond.notify_one();
// }
//
// // 任务封装：移除输出 (保持逻辑，但增加日志以便排查)
// void GLManager::removeOutput(ANativeWindow *window) {
//     std::lock_guard<std::mutex> lock(mTaskLock);
//     mTaskQueue.push([=]() {
//         auto it = mOutputMap.find(window);
//         if (it != mOutputMap.end()) {
//             if (it->second.eglSurface != EGL_NO_SURFACE) {
//                 mEglHelper->destroySurface(it->second.eglSurface);
//             }
//             ANativeWindow_release(window);
//             mOutputMap.erase(it);
//         } else {
//             // 如果没找到，说明可能已经移除了，释放传入的引用
//             // (注意：nativeRemoveOutput 没有 acquire，这里不需要 release，逻辑保持之前的)
//         }
//     });
//     mCond.notify_one();
// }
//
//
// void GLManager::updateOutputSize(ANativeWindow *window, int width, int height) {
//     MYLOGD(mName.c_str(),"updateOutputSize success, p:%p size: %dx%d", window, width, height);
//     {
//         // 作用域限定锁的范围，push 完立刻释放，稍微提高一点点效率
//         std::lock_guard<std::mutex> lock(mTaskLock);
//         mTaskQueue.push([=]() {
//             auto it = mOutputMap.find(window);
//             if (it != mOutputMap.end()) {
//                 it->second.width = width;
//                 it->second.height = height;
//                 MYLOGD(mName.c_str(),"updateOutputSize success, p:%p size: %dx%d", window, width, height);
//             }
//         });
//     }
//
//     // 【修改点】直接删除下面这行
//     // mIsRenderRequested = true;
//
//     // 通知 GL 线程醒来处理任务
//     mCond.notify_one();
// }
//
// // Java 侧 OnFrameAvailableListener 调用此方法
// void GLManager::onFrameAvailable() {
//     std::lock_guard<std::mutex> lock(mLock);
//     // MYLOGD(mName.c_str(),"Native: onFrameAvailable count=%d", mFrameCount);
//     if (mState != RenderState::RUNNING) return;
//     mFrameCount++;
//     // MYLOGD(mName.c_str(),"Native: onFrameAvailable Done count=%d", mFrameCount);
//     mCond.notify_one();
// }
//
// // jint GLManager::getInputSurfaceTextureId() {
// //     // 简单自旋等待 GL 线程初始化纹理 ID
// //     int waitCount = 0;
// //     while (mState != RenderState::EXITING && mOesTextureId == 0 && waitCount < 300) {
// //         usleep(10000);
// //         waitCount++;
// //     }
// //     return mOesTextureId;
// // }
// jint GLManager::getInputSurfaceTextureId() {
//     std::unique_lock<std::mutex> lock(mLock);
//
//     // wait_for 会阻塞当前线程，直到：
//     // 1. mCond.notify_all() 被调用
//     // 2. 或者 超时 (这里设为 1 秒)
//     // 3. 且 Lambda 表达式返回 true
//     bool success = mCond.wait_for(lock, std::chrono::seconds(1), [this] {
//         // 等待条件：纹理ID不为0，或者线程已经退出了(防止死等)
//         return mOesTextureId != 0 || mState == RenderState::EXITING;
//     });
//
//     if (!success || mOesTextureId == 0) {
//         MYLOGD(mName.c_str(),"Timeout waiting for Texture ID generation!");
//         return 0;
//     }
//
//     return (jint) mOesTextureId;
// }
//
// jobject GLManager::getInputSurfaceTexture(JNIEnv *env, jobject managerInstance) {
//     if (getInputSurfaceTextureId() == 0) return nullptr;
//
//     // 1. 创建 Java SurfaceTexture
//     jclass stCls = env->FindClass("android/graphics/SurfaceTexture");
//     jmethodID ctor = env->GetMethodID(stCls, "<init>", "(I)V");
//     jobject stObj = env->NewObject(stCls, ctor, (jint) mOesTextureId);
//
//     if (!stObj) return nullptr;
//
//     // 2. 初始化 JNI 优化路径需要的引用
//     if (!mNativeMode) {
//         // 保存 Manager 实例的全局引用
//         if (!mJavaManagerObj) {
//             mJavaManagerObj = env->NewGlobalRef(managerInstance);
//         }
//         // 获取 Java 优化方法 ID (updateTexImageAndGetMatrix)
//         if (!mUpdateTexId) {
//             jclass managerCls = env->GetObjectClass(managerInstance);
//             mUpdateTexId = env->GetMethodID(managerCls, "updateTexImageAndGetMatrix", "([F)V");
//         }
//         // 准备接收矩阵的数组
//         if (!mJavaMatrixArr) {
//             jfloatArray arr = env->NewFloatArray(16);
//             mJavaMatrixArr = (jfloatArray) env->NewGlobalRef(arr);
//             env->DeleteLocalRef(arr);
//         }
//     }
//
//     // 3. 保存 SurfaceTexture 全局引用
//     mJavaStObj = env->NewGlobalRef(stObj);
//
//     // 4. (Native 模式) 发送任务到 GL 线程创建 ASurfaceTexture
//     if (mNativeMode) {
//         std::lock_guard<std::mutex> lock(mTaskLock);
//         mTaskQueue.emplace([this]() {
//             // 使用 JniHelper 自动管理 Attach/Detach
//             auto state = JniHelper::getEnv();
//             if (state.env && mNativeFuncs.loaded) {
//                 mNativeST = mNativeFuncs.fromSurfaceTexture(state.env, mJavaStObj);
//                 MYLOGD(mName.c_str(),"ASurfaceTexture created: %p", mNativeST);
//             }
//             JniHelper::release(state);
//         });
//         mCond.notify_one(); // 唤醒去执行
//     }
//
//     return stObj;
// }
//
// bool GLManager::initEGL() {
//     // 1. EGL Init
//     if (!mEglHelper->init()) return false;
//     mEglHelper->makeCurrent(mEglHelper->getDefaultSurface());
//     return true;
// }
//
// void GLManager::createOES() {
//     // 2. 创建 OES 纹理 ID
//     glGenTextures(1, &mOesTextureId);
//     glBindTexture(GL_TEXTURE_EXTERNAL_OES, mOesTextureId);
//     glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//     glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//     glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//     glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//
//     mRenderer->prepare(mOesTextureId);
//     MYLOGD(mName.c_str(),"Render loop started, TexId: %d", mOesTextureId);
//
//
// }
//
// void GLManager::initOther() {
// // [新增] 获取 EGL 时间戳扩展函数
// //     auto eglPresentationTimeANDROID = (PFNEGLPRESENTATIONTIMEANDROIDPROC) eglGetProcAddress(
// //             "eglPresentationTimeANDROID");
// //     if (!eglPresentationTimeANDROID) {
// //         MYLOGD(mName.c_str(),"ERROR: eglPresentationTimeANDROID is not available!");
// //     }
// }
//
// void GLManager::renderLoop() {
//     // 1. 设置线程名称 (方便调试)
// #if defined(__ANDROID__) || defined(__linux__)
//     std::string tName = mName.length() > 15 ? mName.substr(0, 15) : mName;
//     pthread_setname_np(pthread_self(), tName.c_str());
// #endif
//
//     // 2. 初始化 EGL 和 纹理
//     if (!initEGL()) {
//         MYLOGD(mName.c_str(),"[%s] Fatal: EGL init failed", mName.c_str());
//         return;
//     }
//     createOES();
//
//     // >>>>>>>通知正在等待 textureId 的线程 <<<<<<<
//     {
//         std::lock_guard<std::mutex> lock(mLock);
//         // 这里的 notify 会唤醒 getInputSurfaceTextureId 中的 wait
//         mCond.notify_all();
//     }
//     // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
//
//     // 3. 获取时间戳设置函数 (API 18+)
//     // auto eglPresentationTimeANDROID = (PFNEGLPRESENTATIONTIMEANDROIDPROC)
//     //         eglGetProcAddress("eglPresentationTimeANDROID");
//
//     // 4. JNI 环境准备
//     auto state = JniHelper::getEnv();
//     if (!state.env) {
//         MYLOGD(mName.c_str(),"[%s] Fatal: Failed to attach thread", mName.c_str());
//         return;
//     }
//
//     // --- 循环开始 ---
//     while (true) {
//         bool shouldRender = false;
//
//         // Phase 1: 等待信号
//         {
//             std::unique_lock<std::mutex> lock(mLock);
//             mCond.wait(lock, [this] {
//                 // 退出条件
//                 if (mState == RenderState::EXITING) return true;
//                 // 任务检查
//                 {
//                     std::lock_guard<std::mutex> tLock(mTaskLock);
//                     if (!mTaskQueue.empty()) return true;
//                 }
//                 // 帧信号检查
//                 return mFrameCount > 0;
//             });
//
//             if (mState == RenderState::EXITING) break;
//
//             // 消耗一帧信号
//             if (mFrameCount > 0) {
//                 mFrameCount--;
//                 shouldRender = true;
//             }
//         }
//
//         // Phase 2: 处理任务 (Window 添加/移除/Resize)
//         processTasks();
//
//         // 暂停状态检查
//         if (mState == RenderState::PAUSED) {
//             // 在 PAUSED 状态下，销毁 Window Surface 释放资源
//             for (auto &pair: mOutputMap) {
//                 if (pair.second.eglSurface != EGL_NO_SURFACE) {
//                     mEglHelper->destroySurface(pair.second.eglSurface);
//                     pair.second.eglSurface = EGL_NO_SURFACE;
//                 }
//             }
//             continue; // 跳过渲染
//         }
//
//         if (!shouldRender) continue; // 仅被任务唤醒，无新帧，不渲染
//
//         // Phase 3: 渲染准备
//         // 创建局部引用帧，防止循环内 JNI 引用溢出
//         if (!mNativeMode) {
//             if (state.env->PushLocalFrame(16) < 0) continue;
//         }
//
//         // 重建 Window Surface (如果之前被销毁)
//         for (auto &pair: mOutputMap) {
//             if (pair.second.eglSurface == EGL_NO_SURFACE) {
//                 pair.second.eglSurface = mEglHelper->createWindowSurface(pair.first);
//             }
//         }
//
//         // 计算当前系统时间戳 (PTS)，用于录制同步
//         struct timespec now;
//         clock_gettime(CLOCK_BOOTTIME, &now);
//         long nowNs = now.tv_sec * 1000000000LL + now.tv_nsec;
//
//         // Phase 4: 更新纹理 (核心)
//         bool textureUpdated = false;
//
//         if (mNativeMode && mNativeST) {
//             // [路径 A] 纯 Native 调用 (API 28+, 听起来原生好像更高性能，实际上还不如下面的调用Java方法)
//             if (mNativeFuncs.updateTexImage(mNativeST) == 0) {
//                 mNativeFuncs.getTransformMatrix(mNativeST, mTextureMatrix);
//                 textureUpdated = true;
//             }
//         } else if (mJavaManagerObj && mUpdateTexId) {
//             // [路径 B] JNI 批处理优化调用
//             state.env->CallVoidMethod(mJavaManagerObj, mUpdateTexId, mJavaMatrixArr);
//
//             if (state.env->ExceptionCheck()) {
//                 state.env->ExceptionClear();
//                 MYLOGD(mName.c_str(),"[%s] Java updateTexImage failed", mName.c_str());
//             } else {
//                 state.env->GetFloatArrayRegion(mJavaMatrixArr, 0, 16, mTextureMatrix);
//                 textureUpdated = true;
//             }
//         }
//
//         if (textureUpdated) {
//             mFirstFrameReceived = true;
//         } else if (!mNativeMode) {
//             state.env->PopLocalFrame(nullptr);
//             continue; // 更新失败，跳过绘制
//         }
//
//         // Phase 5: 绘制到所有 Output
//         if (mFirstFrameReceived && !mOutputMap.empty()) {
//             for (auto &pair: mOutputMap) {
//                 OutputInfo &info = pair.second;
//                 // 切换 EGL 上下文
//                 if (!mEglHelper->makeCurrent(info.eglSurface)) continue;
//
//                 // 绘制
//                 mRenderer->draw(info.width, info.height, mTextureMatrix);
//
//                 // 设置时间戳
//                 if (eglPresentationTimeANDROID) {
//                     eglPresentationTimeANDROID(mEglHelper->getDisplay(), info.eglSurface, nowNs);
//                 }
//
//                 // 提交缓冲区
//                 mEglHelper->swapBuffers(info.eglSurface);
//             }
//         }
//
//         if (!mNativeMode) {
//             state.env->PopLocalFrame(nullptr);
//         }
//     } // while end
//
//     // =========================================================================
//     // 线程退出清理
//     // =========================================================================
//
//     // 释放 Global Refs
//     if (mJavaManagerObj) {
//         state.env->DeleteGlobalRef(mJavaManagerObj);
//         mJavaManagerObj = nullptr;
//     }
//     if (mJavaStObj) {
//         state.env->DeleteGlobalRef(mJavaStObj);
//         mJavaStObj = nullptr;
//     }
//     if (mJavaMatrixArr) {
//         state.env->DeleteGlobalRef(mJavaMatrixArr);
//         mJavaMatrixArr = nullptr;
//     }
//
//     // 释放 Native ASurfaceTexture
//     if (mNativeMode && mNativeST && mNativeFuncs.release) {
//         mNativeFuncs.release(mNativeST);
//         mNativeST = nullptr;
//     }
//
//     // 释放 EGL 资源
//     for (auto &pair: mOutputMap) {
//         mEglHelper->destroySurface(pair.second.eglSurface);
//         ANativeWindow_release(pair.first);
//     }
//     mOutputMap.clear();
//
//     // 释放 GL 纹理
//     if (mOesTextureId != 0) {
//         glDeleteTextures(1, &mOesTextureId);
//         mOesTextureId = 0;
//     }
//
//     mRenderer->release();
//     mEglHelper->release();
//
//     delete mEglHelper;
//     delete mRenderer;
//
//     JniHelper::release(state);
//
//     MYLOGD(mName.c_str(),"[%s] Render loop exited cleanly", mName.c_str());
// }
//
// void GLManager::processTasks() {
//     std::lock_guard<std::mutex> lock(mTaskLock);
//     while (!mTaskQueue.empty()) {
//         auto task = mTaskQueue.front();
//         mTaskQueue.pop();
//         task(); // 在 GL 线程执行
//     }
// }
//
// void GLManager::setTransform(int r, bool h, bool v) { mRenderer->setTransform(r, h, v); }
//
// void GLManager::setVideoSize(int w, int h) { mRenderer->setVideoSize(w, h); }
//
// void GLManager::setFilterParams(float *p) {
//     // 数组转参数：warm, contrast, sat, gamma, bright, r, g, b, sharp, hue
//     mRenderer->setFilterParams(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]);
// }
//
// void GLManager::release() {
//     std::lock_guard<std::mutex> lock(mLock);
//     if (mState == RenderState::EXITING) return;
//
//     mState = RenderState::EXITING;
//     mCond.notify_all(); // 唤醒线程去自杀
// }