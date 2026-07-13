// #include "opengl_manager.h"
//
// #include <ctime>
// #include <cmath>
// #include <chrono>
// #include <unistd.h>
//
// // 全局帧可用回调函数
// static void OnFrameAvailable(void* context) {
//     auto* manager = static_cast<OpenGLManager*>(context);
//     if (manager) {
//         manager->OnFrameAvailableCallback(); // 调用成员函数处理
//     }
// }
// void OpenGLManager::OnFrameAvailableCallback() {
//     frameAvailable_.store(true);
//     frameCond_.notify_one();
// }
// /**
//  * 构建用户变换矩阵（以图像中心为轴，旋转 + 翻转）
//  * @param angleDeg  旋转角度（0, 90, 180, 270）
//  * @param flipH     是否水平翻转（左右镜像）
//  * @param flipV     是否垂直翻转（上下颠倒）
//  * @param outMat    输出 4x4 列主序矩阵，可直接用于 glUniformMatrix4fv
//  */
// static void BuildUserTransformMatrix(float angleDeg, bool flipH, bool flipV, float outMat[16]) {
//     // 默认单位矩阵
//     float mat[16] = {
//         1,0,0,0,
//         0,1,0,0,
//         0,0,1,0,
//         0,0,0,1
//     };
//
//     // 如果无变换，直接返回单位矩阵
//     if (angleDeg == 0.0f && !flipH && !flipV) {
//         memcpy(outMat, mat, sizeof(mat));
//         return;
//     }
//
//     float rad = angleDeg * 3.141592653589793f / 180.0f;
//     float c = cosf(rad), s = sinf(rad);
//     float sx = flipH ? -1.0f : 1.0f;   // 水平翻转
//     float sy = flipV ? -1.0f : 1.0f;   // 垂直翻转
//
//     // 直接计算变换后的坐标公式：
//     // 原始点 (x,y) ∈ [0,1]
//     // 先平移到中心 (x-0.5, y-0.5)
//     // 再缩放 (sx, sy) 并旋转角度 θ
//     // 再平移回中心 (+0.5, +0.5)
//     // 最终坐标：
//     //   x' = (sx * c) * x + (-sy * s) * y + (0.5 - 0.5*sx*c + 0.5*sy*s)
//     //   y' = (sx * s) * x + (sy * c) * y + (0.5 - 0.5*sx*s - 0.5*sy*c)
//     // 矩阵（列主序）：
//     //   m00 = sx*c,  m01 = -sy*s,  m03 = 0.5 - 0.5*sx*c + 0.5*sy*s
//     //   m10 = sx*s,  m11 = sy*c,   m13 = 0.5 - 0.5*sx*s - 0.5*sy*c
//     //   m22 = 1.0,   m33 = 1.0
//     // 其余为 0
//
//     outMat[0]  = sx * c;
//     outMat[4]  = -sy * s;
//     outMat[12] = 0.5f - 0.5f * sx * c + 0.5f * sy * s;
//
//     outMat[1]  = sx * s;
//     outMat[5]  = sy * c;
//     outMat[13] = 0.5f - 0.5f * sx * s - 0.5f * sy * c;
//
//     outMat[10] = 1.0f;
//     outMat[15] = 1.0f;
//
//     // 其他元素置零
//     outMat[2]  = outMat[3]  = 0.0f;
//     outMat[6]  = outMat[7]  = 0.0f;
//     outMat[8]  = outMat[9]  = 0.0f;
//     outMat[11] = outMat[14] = 0.0f;
// }
// // =====================================================================
// // 着色器源码（GLSL ES 3.0）
// // =====================================================================
//
// // 顶点着色器：传递位置和纹理坐标，应用纹理矩阵校正摄像头方向
// static const char* VERTEX_SHADER_SRC = R"(#version 300 es
// layout(location = 0) in vec2 aPosition;
// layout(location = 1) in vec2 aTexCoord;
//
// uniform mat4 uTexMatrix;
//
// out vec2 vTexCoord;
//
// void main() {
//     gl_Position = vec4(aPosition, 0.0, 1.0);
//     vTexCoord = (uTexMatrix * vec4(aTexCoord, 0.0, 1.0)).xy;
// }
// )";
//
// // 片段着色器：直出，不添加任何滤镜效果
// static const char* FRAGMENT_SHADER_SRC = R"(#version 300 es
// #extension GL_OES_EGL_image_external_essl3 : require
//
// precision mediump float;
//
// uniform samplerExternalOES uTexture;
//
// in vec2 vTexCoord;
// out vec4 fragColor;
//
// void main() {
//     fragColor = texture(uTexture, vTexCoord);
// }
// )";
// // 片段着色器：OES 纹理采样 + 自定义滤镜（灰度 + 暗角 + 冷色调）
// // 可替换为任意图像处理算法（美颜、边缘检测、色彩映射等）
// // static const char* FRAGMENT_SHADER_SRC = R"(#version 300 es
// // #extension GL_OES_EGL_image_external_essl3 : require
// //
// // precision mediump float;
// //
// // uniform samplerExternalOES uTexture;   // OES 外部纹理（摄像头数据）
// // uniform float uTime;                   // 当前时间（秒），可用于动态效果
// // uniform vec2 uResolution;              // 渲染分辨率
// //
// // in vec2 vTexCoord;
// // out vec4 fragColor;
// //
// // void main() {
// //     // 1. 采样摄像头纹理
// //     vec4 color = texture(uTexture, vTexCoord);
// //
// //     // 2. 灰度转换（加权平均，符合人眼感知）
// //     float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
// //
// //     // 3. 暗角效果（距离中心越远越暗）
// //     vec2 center = vTexCoord - 0.5;
// //     float dist = length(center);
// //     float vignette = smoothstep(0.7, 0.3, dist);
// //
// //     // 4. 混合原始色和灰度，应用暗角
// //     vec3 processed = mix(color.rgb, vec3(gray), 0.6) * vignette;
// //
// //     // 5. 轻微冷色调
// //     processed.b *= 1.1;
// //
// //     fragColor = vec4(processed, color.a);
// // }
// // )";
//
// // 全屏四边形顶点：位置(x,y) + 纹理坐标(s,t)
// static const float QUAD_VERTICES[] = {
//     -1.0f, -1.0f,   0.0f, 1.0f,   // 左下
//      1.0f, -1.0f,   1.0f, 1.0f,   // 右下
//      1.0f,  1.0f,   1.0f, 0.0f,   // 右上
//     -1.0f,  1.0f,   0.0f, 0.0f,   // 左上
// };
//
// // 索引：两个三角形组成矩形
// static const uint16_t QUAD_INDICES[] = { 0, 1, 2, 0, 2, 3 };
//
// // =====================================================================
// // 单例实现
// // =====================================================================
//
// OpenGLManager& OpenGLManager::GetInstance() {
//     static OpenGLManager instance;
//     return instance;
// }
//
// OpenGLManager::OpenGLManager() {
//     LOGI("OpenGLManager 构造");
// }
//
// OpenGLManager::~OpenGLManager() {
//     LOGI("OpenGLManager 析构");
//     Stop();
// }
//
// // =====================================================================
// // 公开接口
// // =====================================================================
//
// bool OpenGLManager::Start(const std::string& xComponentSurfaceId, int32_t width, int32_t height) {
//     std::lock_guard<std::mutex> lock(mutex_);
//
//     if (running_.load()) {
//         LOGW("OpenGLManager 已在运行，忽略重复启动");
//         return false;
//     }
//
//     LOGI("Start - surfaceId=%s, %dx%d", xComponentSurfaceId.c_str(), width, height);
//
//     surfaceWidth_  = width;
//     surfaceHeight_ = height;
//
//     uint64_t surfId = std::stoull(xComponentSurfaceId);
//     int ret = OH_NativeWindow_CreateNativeWindowFromSurfaceId(surfId, &xComponentWindow_);
//     if (ret != 0 || xComponentWindow_ == nullptr) {
//         LOGE("CreateNativeWindowFromSurfaceId 失败, ret=%d", ret);
//         return false;
//     }
//     OH_NativeWindow_NativeWindowHandleOpt(xComponentWindow_, SET_BUFFER_GEOMETRY, width, height);
//
//     if (!InitEGL(reinterpret_cast<EGLNativeWindowType>(xComponentWindow_))) {
//         LOGE("初始化 EGL 失败");
//         DestroyEGL();
//         return false;
//     }
//
//     if (!CreateNativeImage()) {
//         LOGE("创建 NativeImage 失败");
//         DestroyNativeImage();
//         DestroyEGL();
//         return false;
//     }
//
//     if (!InitGLResources()) {
//         LOGE("初始化 GL 资源失败");
//         DestroyGLResources();
//         DestroyEGL();
//         DestroyNativeImage();
//         return false;
//     }
//
//     // ---- 释放 EGL 上下文，让渲染线程接管 ----
//     if (!eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
//         LOGE("释放 EGL 上下文失败: 0x%x", eglGetError());
//         // 继续，不一定致命
//     }
//
//     // SetRotation(180);
//     SetFlipVertical(true);
//     // SetFlipHorizontal(true);
//     running_.store(true);
//     startTime_ = 0.0f;
//     frameCount_.store(0);
//     renderThread_ = std::thread(&OpenGLManager::RenderLoop, this);
//
//     LOGI("启动成功, 摄像头输入 Surface ID=%s", inputSurfaceId_.c_str());
//     return true;
// }
//
// void OpenGLManager::Stop() {
//     // 通知渲染线程退出
//     if (running_.load()) {
//         running_.store(false);
//         // 唤醒可能阻塞在条件变量上的渲染线程
//         frameCond_.notify_all();
//         if (renderThread_.joinable()) {
//             renderThread_.join();
//         }
//     }
//
//     std::lock_guard<std::mutex> lock(mutex_);
//
//     // 按逆序释放资源
//     DestroyGLResources();
//     DestroyEGL();
//     DestroyNativeImage();
//
//     LOGI("已停止");
// }
//
// std::string OpenGLManager::GetInputSurfaceId() const {
//     std::lock_guard<std::mutex> lock(mutex_);
//     return inputSurfaceId_;
// }
//
// void OpenGLManager::UpdateSize(int32_t width, int32_t height) {
//     surfaceWidth_  = width;
//     surfaceHeight_ = height;
//     sizeChanged_.store(true);
//     LOGI("UpdateSize - %dx%d", width, height);
// }
//
// bool OpenGLManager::IsRunning() const {
//     return running_.load();
// }
//
// // =====================================================================
// // NativeImage 管理（摄像头数据零拷贝输入）
// // =====================================================================
// //
// // 开源鸿蒙 NativeImage 工作流程：
// //   1. 先创建 OES 纹理
// //   2. OH_NativeImage_Create(textureId, GL_TEXTURE_EXTERNAL_OES) 绑定纹理
// //   3. OH_NativeImage_AttachContext() 附加到 GL 上下文
// //   4. OH_NativeImage_AcquireNativeWindow() 获取生产者端 NativeWindow
// //   5. OH_NativeImage_GetSurfaceId(image, &surfaceId) 获取 Surface ID 给摄像头
// //   6. 渲染循环中 OH_NativeImage_UpdateSurfaceImage() 更新纹理（零拷贝）
// //
//
// bool OpenGLManager::CreateNativeImage() {
//     // ---- 1. 创建 OES 纹理 ----
//     oesTextureId_ = CreateOESTexture();
//     if (oesTextureId_ == 0) {
//         LOGE("创建 OES 纹理失败");
//         return false;
//     }
//
//     // ---- 2. 创建 NativeImage，绑定 OES 纹理 ----
//     nativeImage_ = OH_NativeImage_Create(oesTextureId_, GL_TEXTURE_EXTERNAL_OES);
//     if (nativeImage_ == nullptr) {
//         LOGE("OH_NativeImage_Create 失败");
//         return false;
//     }
//
//     // ---- 3. 附加到当前 GL 上下文 ----
//     int ret = OH_NativeImage_AttachContext(nativeImage_, oesTextureId_);
//     if (ret != 0) {
//         LOGE("OH_NativeImage_AttachContext 失败, ret=%d", ret);
//         OH_NativeImage_Destroy(&nativeImage_);
//         nativeImage_ = nullptr;
//         return false;
//     }
//
//     // ---- 4. 获取生产者端 NativeWindow ----
//     cameraInputWindow_ = OH_NativeImage_AcquireNativeWindow(nativeImage_);
//     if (cameraInputWindow_ == nullptr) {
//         LOGE("OH_NativeImage_AcquireNativeWindow 失败");
//         OH_NativeImage_DetachContext(nativeImage_);
//         OH_NativeImage_Destroy(&nativeImage_);
//         nativeImage_ = nullptr;
//         return false;
//     }
//
//     // ---- 5. 获取 Surface ID ----
//     uint64_t surfaceId = 0;
//     ret = OH_NativeImage_GetSurfaceId(nativeImage_, &surfaceId);
//     if (ret != 0) {
//         LOGE("OH_NativeImage_GetSurfaceId 失败, ret=%d", ret);
//         OH_NativeImage_DetachContext(nativeImage_);
//         OH_NativeImage_Destroy(&nativeImage_);
//         nativeImage_ = nullptr;
//         return false;
//     }
//     inputSurfaceId_ = std::to_string(surfaceId);
//
//     // ---- 6. 注册帧可用监听器 ----
//     OH_OnFrameAvailableListener listener;
//     listener.context = this;
//     listener.onFrameAvailable = OnFrameAvailable;
//     ret = OH_NativeImage_SetOnFrameAvailableListener(nativeImage_, listener);
//     if (ret != 0) {
//         LOGE("OH_NativeImage_SetOnFrameAvailableListener 失败, ret=%d", ret);
//         // 注册失败则释放资源并返回 false，因为后续循环依赖回调唤醒
//         OH_NativeImage_DetachContext(nativeImage_);
//         OH_NativeImage_Destroy(&nativeImage_);
//         nativeImage_ = nullptr;
//         return false;
//     }
//
//     LOGI("NativeImage 创建成功, 摄像头输入 Surface ID=%s, OES 纹理=%u",
//          inputSurfaceId_.c_str(), oesTextureId_);
//     return true;
// }
//
// void OpenGLManager::DestroyNativeImage() {
//     if (nativeImage_ != nullptr) {
//         // 先从 GL 上下文分离
//         OH_NativeImage_DetachContext(nativeImage_);
//         // 销毁 NativeImage（双指针传参，会自动释放关联的 NativeWindow）
//         OH_NativeImage_Destroy(&nativeImage_);
//         nativeImage_ = nullptr;
//         cameraInputWindow_ = nullptr;  // 已随 NativeImage 一起释放
//         inputSurfaceId_.clear();
//         LOGI("NativeImage 已销毁");
//     }
//
//     if (oesTextureId_ != 0) {
//         glDeleteTextures(1, &oesTextureId_);
//         oesTextureId_ = 0;
//     }
// }
//
// // =====================================================================
// // EGL 环境管理
// // =====================================================================
//
// bool OpenGLManager::InitEGL(EGLNativeWindowType window) {
//     // ---- 1. 获取 EGL Display ----
//     eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
//     if (eglDisplay_ == EGL_NO_DISPLAY) {
//         LOGE("eglGetDisplay 失败: 0x%x", eglGetError());
//         return false;
//     }
//
//     // ---- 2. 初始化 EGL ----
//     EGLint major, minor;
//     if (!eglInitialize(eglDisplay_, &major, &minor)) {
//         LOGE("eglInitialize 失败: 0x%x", eglGetError());
//         return false;
//     }
//     LOGI("EGL 版本: %d.%d", major, minor);
//
//     // ---- 3. 选择 EGL 配置（RGBA8888 + OpenGL ES 3.0） ----
//     EGLint configAttribs[] = {
//         EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
//         EGL_RED_SIZE,        8,
//         EGL_GREEN_SIZE,      8,
//         EGL_BLUE_SIZE,       8,
//         EGL_ALPHA_SIZE,      8,
//         EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
//         EGL_NONE
//     };
//
//     EGLint numConfigs;
//     if (!eglChooseConfig(eglDisplay_, configAttribs, &eglConfig_, 1, &numConfigs)
//         || numConfigs == 0) {
//         LOGE("eglChooseConfig 失败: 0x%x", eglGetError());
//         return false;
//     }
//
//     // ---- 4. 创建 EGL Context（OpenGL ES 3.0） ----
//     EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
//     eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, contextAttribs);
//     if (eglContext_ == EGL_NO_CONTEXT) {
//         LOGE("eglCreateContext 失败: 0x%x", eglGetError());
//         return false;
//     }
//
//     // ---- 5. 创建 EGL Surface（绑定到 XComponent 的 NativeWindow） ----
//     eglSurface_ = eglCreateWindowSurface(eglDisplay_, eglConfig_, window, nullptr);
//     if (eglSurface_ == EGL_NO_SURFACE) {
//         LOGE("eglCreateWindowSurface 失败: 0x%x", eglGetError());
//         return false;
//     }
//
//     // ---- 6. 绑定当前上下文 ----
//     if (!eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
//         LOGE("eglMakeCurrent 失败: 0x%x", eglGetError());
//         return false;
//     }
//
//     LOGI("EGL 环境初始化成功");
//     return true;
// }
//
// void OpenGLManager::DestroyEGL() {
//     if (eglDisplay_ != EGL_NO_DISPLAY) {
//         eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
//
//         if (eglSurface_ != EGL_NO_SURFACE) {
//             eglDestroySurface(eglDisplay_, eglSurface_);
//             eglSurface_ = EGL_NO_SURFACE;
//         }
//         if (eglContext_ != EGL_NO_CONTEXT) {
//             eglDestroyContext(eglDisplay_, eglContext_);
//             eglContext_ = EGL_NO_CONTEXT;
//         }
//         eglTerminate(eglDisplay_);
//         eglDisplay_ = EGL_NO_DISPLAY;
//     }
//
//     if (xComponentWindow_ != nullptr) {
//         OH_NativeWindow_DestroyNativeWindow(xComponentWindow_);
//         xComponentWindow_ = nullptr;
//     }
//
//     LOGI("EGL 环境已销毁");
// }
//
// // =====================================================================
// // GL 资源管理
// // =====================================================================
//
// /**
//  * 创建 OES 纹理（用于接收 NativeImage 的摄像头帧数据）
//  */
// GLuint OpenGLManager::CreateOESTexture() {
//     GLuint tex;
//     glGenTextures(1, &tex);
//     glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
//     glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//     glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//     glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//     glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//     glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
//     LOGI("OES 纹理创建成功, ID=%u", tex);
//     return tex;
// }
//
// /**
//  * 编译并链接渲染 Shader 程序
//  */
// GLuint OpenGLManager::CreateShaderProgram() {
//     // 编译着色器的 lambda
//     auto compileShader = [](GLenum type, const char* src) -> GLuint {
//         GLuint shader = glCreateShader(type);
//         glShaderSource(shader, 1, &src, nullptr);
//         glCompileShader(shader);
//         GLint status;
//         glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
//         if (status != GL_TRUE) {
//             GLchar log[512];
//             glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
//             LOGE("Shader 编译失败: %s", log);
//             glDeleteShader(shader);
//             return 0;
//         }
//         return shader;
//     };
//
//     GLuint vs = compileShader(GL_VERTEX_SHADER, VERTEX_SHADER_SRC);
//     GLuint fs = compileShader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER_SRC);
//     if (vs == 0 || fs == 0) {
//         if (vs) glDeleteShader(vs);
//         if (fs) glDeleteShader(fs);
//         return 0;
//     }
//
//     // 链接程序
//     GLuint program = glCreateProgram();
//     glAttachShader(program, vs);
//     glAttachShader(program, fs);
//     glLinkProgram(program);
//
//     GLint status;
//     glGetProgramiv(program, GL_LINK_STATUS, &status);
//     if (status != GL_TRUE) {
//         GLchar log[512];
//         glGetProgramInfoLog(program, sizeof(log), nullptr, log);
//         LOGE("Shader 链接失败: %s", log);
//         glDeleteProgram(program);
//         glDeleteShader(vs);
//         glDeleteShader(fs);
//         return 0;
//     }
//
//     glDeleteShader(vs);
//     glDeleteShader(fs);
//
//     // 获取 uniform 位置
//     uTextureLoc_    = glGetUniformLocation(program, "uTexture");
//     uTexMatrixLoc_  = glGetUniformLocation(program, "uTexMatrix");
//     uTimeLoc_       = glGetUniformLocation(program, "uTime");
//     uResolutionLoc_ = glGetUniformLocation(program, "uResolution");
//
//     LOGI("Shader 程序创建成功, ID=%u", program);
//     return program;
// }
//
// /**
//  * 创建 FBO 及颜色附件纹理
//  */
// bool OpenGLManager::CreateFBO() {
//     glGenFramebuffers(1, &fbo_);
//     glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
//
//     glGenTextures(1, &fboTexture_);
//     glBindTexture(GL_TEXTURE_2D, fboTexture_);
//     glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, surfaceWidth_, surfaceHeight_,
//                  0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//
//     glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTexture_, 0);
//
//     if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
//         LOGE("FBO 创建不完整");
//         glBindFramebuffer(GL_FRAMEBUFFER, 0);
//         return false;
//     }
//
//     glBindFramebuffer(GL_FRAMEBUFFER, 0);
//     LOGI("FBO 创建成功, ID=%u", fbo_);
//     return true;
// }
//
// /**
//  * 初始化所有 GL 资源
//  */
// bool OpenGLManager::InitGLResources() {
//     // 创建 Shader 程序
//     shaderProgram_ = CreateShaderProgram();
//     if (shaderProgram_ == 0) return false;
//
//     // 创建 FBO
//     if (!CreateFBO()) return false;
//
//     // 创建 VAO / VBO / EBO（全屏四边形）
//     glGenVertexArrays(1, &vao_);
//     glGenBuffers(1, &vbo_);
//     glGenBuffers(1, &ebo_);
//
//     glBindVertexArray(vao_);
//
//     glBindBuffer(GL_ARRAY_BUFFER, vbo_);
//     glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTICES), QUAD_VERTICES, GL_STATIC_DRAW);
//
//     glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
//     glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(QUAD_INDICES), QUAD_INDICES, GL_STATIC_DRAW);
//
//     // 位置属性 (location = 0)
//     glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
//     glEnableVertexAttribArray(0);
//
//     // 纹理坐标属性 (location = 1)
//     glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
//                           (void*)(2 * sizeof(float)));
//     glEnableVertexAttribArray(1);
//
//     glBindVertexArray(0);
//
//     LOGI("所有 GL 资源初始化完成");
//     return true;
// }
//
// /**
//  * 释放所有 GL 资源
//  */
// void OpenGLManager::DestroyGLResources() {
//     if (vao_)          { glDeleteVertexArrays(1, &vao_);        vao_ = 0; }
//     if (vbo_)          { glDeleteBuffers(1, &vbo_);             vbo_ = 0; }
//     if (ebo_)          { glDeleteBuffers(1, &ebo_);             ebo_ = 0; }
//     if (fboTexture_)   { glDeleteTextures(1, &fboTexture_);     fboTexture_ = 0; }
//     if (fbo_)          { glDeleteFramebuffers(1, &fbo_);        fbo_ = 0; }
//     if (shaderProgram_){ glDeleteProgram(shaderProgram_);       shaderProgram_ = 0; }
//     LOGI("GL 资源已释放");
// }
//
// // =====================================================================
// // 渲染逻辑
// // =====================================================================
//
// /**
//  * 渲染线程主函数
//  * 循环：获取最新帧 → 更新纹理 → Shader 处理 → 渲染到 XComponent
//  */
// // void OpenGLManager::RenderLoop() {
// //     LOGI("渲染线程启动");
// //
// //     // ---- 绑定 EGL 上下文到当前线程 ----
// //     if (!eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
// //         LOGE("渲染线程绑定 EGL 上下文失败: 0x%x", eglGetError());
// //         return;
// //     }
// //
// //     auto startTp = std::chrono::steady_clock::now();
// //
// //     while (running_.load()) {
// //         startTime_ = std::chrono::duration<float>(
// //             std::chrono::steady_clock::now() - startTp).count();
// //
// //         if (sizeChanged_.load()) {
// //             sizeChanged_.store(false);
// //             if (xComponentWindow_ != nullptr) {
// //                 OH_NativeWindow_NativeWindowHandleOpt(
// //                     xComponentWindow_, SET_BUFFER_GEOMETRY, surfaceWidth_, surfaceHeight_);
// //             }
// //             if (fbo_)        { glDeleteFramebuffers(1, &fbo_);        fbo_ = 0; }
// //             if (fboTexture_) { glDeleteTextures(1, &fboTexture_);     fboTexture_ = 0; }
// //             CreateFBO();
// //         }
// //
// //         if (!RenderFrame()) {
// //             // 没有新帧，适当休眠避免忙等（可保留或增加延迟）
// //             std::this_thread::sleep_for(std::chrono::milliseconds(5));
// //             // 如需调试可打印（建议仅打印一次，避免刷屏）
// //     // LOGI("没有新帧, 适当休眠避免忙等 %d 帧", frameCount_.load());
// //            
// //         }
// //         // 若需统计成功帧数，可在 RenderFrame 内部打印或此处加计数器
// //     }
// //
// //     // 释放上下文（可选）
// //     eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
// //
// //     LOGI("渲染线程退出, 共渲染 %d 帧", frameCount_.load());
// // }
//
// void OpenGLManager::RenderLoop() {
//     LOGI("渲染线程启动");
//
//     // ---- 绑定 EGL 上下文到当前线程 ----
//     if (!eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
//         LOGE("渲染线程绑定 EGL 上下文失败: 0x%x", eglGetError());
//         return;
//     }
//
//     auto startTp = std::chrono::steady_clock::now();
//
//     while (running_.load()) {
//         // 1. 等待新帧信号或线程退出指令
//         {
//             std::unique_lock<std::mutex> lock(frameMutex_);
//             frameCond_.wait(lock, [this] {
//                 return frameAvailable_.load() || !running_.load();
//             });
//         }
//
//         // 若线程被要求停止，则退出循环
//         if (!running_.load()) {
//             break;
//         }
//
//         // 2. 重置帧可用标志
//         frameAvailable_.store(false);
//
//         // 3. 处理窗口尺寸变化
//         if (sizeChanged_.load()) {
//             sizeChanged_.store(false);
//             if (xComponentWindow_ != nullptr) {
//                 OH_NativeWindow_NativeWindowHandleOpt(
//                     xComponentWindow_, SET_BUFFER_GEOMETRY, surfaceWidth_, surfaceHeight_);
//             }
//             if (fbo_)        { glDeleteFramebuffers(1, &fbo_);        fbo_ = 0; }
//             if (fboTexture_) { glDeleteTextures(1, &fboTexture_);     fboTexture_ = 0; }
//             CreateFBO();
//         }
//
//         // 4. 渲染一帧
//         if (!RenderFrame()) {
//             // 极少数情况渲染失败（如资源异常），短暂休眠避免死循环
//             std::this_thread::sleep_for(std::chrono::milliseconds(1));
//         }
//     }
//
//     // 释放上下文（可选）
//     eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
//     LOGI("渲染线程退出, 共渲染 %d 帧", frameCount_.load());
// }
//
// /**
//  * 执行一帧渲染
//  * @return true=成功渲染了一帧, false=没有新帧
//  */
// // bool OpenGLManager::RenderFrame() {
// //     if (nativeImage_ == nullptr || eglDisplay_ == EGL_NO_DISPLAY) {
// //         return false;
// //     }
// //
// //     // ---- 1. 从 NativeImage 更新 OES 纹理（摄像头最新帧，GPU 零拷贝） ----
// //     int ret = OH_NativeImage_UpdateSurfaceImage(nativeImage_);
// //     if (ret != 0) {
// //         // 没有新帧（摄像头未开始输出或帧率较低）
// //         return false;
// //     }
// //
// //     // ---- 2. 获取纹理变换矩阵（校正摄像头帧方向） ----
// //     float texMatrix[16];
// //     ret = OH_NativeImage_GetTransformMatrixV2(nativeImage_, texMatrix);
// //     if (ret != 0) {
// //         // 获取失败时使用单位矩阵
// //         for (int i = 0; i < 16; i++) texMatrix[i] = 0.0f;
// //         texMatrix[0] = texMatrix[5] = texMatrix[10] = texMatrix[15] = 1.0f;
// //     }
// //
// //     // ---- 3. 渲染到 XComponent（默认帧缓冲） ----
// //     glBindFramebuffer(GL_FRAMEBUFFER, 0);
// //     glViewport(0, 0, surfaceWidth_, surfaceHeight_);
// //     glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
// //     glClear(GL_COLOR_BUFFER_BIT);
// //
// //     glUseProgram(shaderProgram_);
// //
// //     // 绑定 OES 纹理到纹理单元 0
// //     glActiveTexture(GL_TEXTURE0);
// //     glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTextureId_);
// //     glUniform1i(uTextureLoc_, 0);
// //
// //     // 设置 uniform 变量
// //     glUniformMatrix4fv(uTexMatrixLoc_, 1, GL_FALSE, texMatrix);
// //     glUniform1f(uTimeLoc_, startTime_);
// //     glUniform2f(uResolutionLoc_, (float)surfaceWidth_, (float)surfaceHeight_);
// //
// //     // 绘制全屏四边形
// //     glBindVertexArray(vao_);
// //     glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
// //     glBindVertexArray(0);
// //
// //     // ---- 4. 交换缓冲区，显示到屏幕 ----
// //     eglSwapBuffers(eglDisplay_, eglSurface_);
// //
// //     frameCount_.fetch_add(1);
// //     return true;
// // }
//
// bool OpenGLManager::RenderFrame() {
//     if (nativeImage_ == nullptr || eglDisplay_ == EGL_NO_DISPLAY) {
//         return false;
//     }
//
//     // ---- 1. 更新 OES 纹理 ----
//     int ret = OH_NativeImage_UpdateSurfaceImage(nativeImage_);
//     if (ret != 0) {
//         return false; // 无新帧
//     }
//
//     // ---- 2. 获取摄像头校正矩阵 ----
//     float texMatrix[16];
//     ret = OH_NativeImage_GetTransformMatrixV2(nativeImage_, texMatrix);
//     if (ret != 0) {
//         // 若获取失败，使用单位矩阵
//         memset(texMatrix, 0, sizeof(texMatrix));
//         texMatrix[0] = texMatrix[5] = texMatrix[10] = texMatrix[15] = 1.0f;
//     }
//
//     // ---- 3. 构建用户变换矩阵 ----
//     float userMat[16];
//     float angle = rotationDeg_.load();
//     bool flipH = flipH_.load();
//     bool flipV = flipV_.load();
//     BuildUserTransformMatrix(angle, flipH, flipV, userMat);
//
//     // ---- 4. 组合矩阵：先用户变换，再摄像头校正 ----
//     float finalMat[16];
//     for (int i = 0; i < 4; ++i) {
//         for (int j = 0; j < 4; ++j) {
//             finalMat[i*4 + j] = 0.0f;
//             for (int k = 0; k < 4; ++k) {
//                 finalMat[i*4 + j] += texMatrix[i*4 + k] * userMat[k*4 + j];
//             }
//         }
//     }
//
//     // ---- 5. 渲染到屏幕 ----
//     glBindFramebuffer(GL_FRAMEBUFFER, 0);
//     glViewport(0, 0, surfaceWidth_, surfaceHeight_);
//     glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
//     glClear(GL_COLOR_BUFFER_BIT);
//
//     glUseProgram(shaderProgram_);
//     glActiveTexture(GL_TEXTURE0);
//     glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTextureId_);
//     glUniform1i(uTextureLoc_, 0);
//     glUniformMatrix4fv(uTexMatrixLoc_, 1, GL_FALSE, finalMat);
//
//     glBindVertexArray(vao_);
//     glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
//     glBindVertexArray(0);
//
//     eglSwapBuffers(eglDisplay_, eglSurface_);
//
//     frameCount_.fetch_add(1);
//     return true;
// }
// // =====================================================================
// // 用户变换控制接口
// // =====================================================================
//
// void OpenGLManager::SetRotation(int degrees) {
//     // 规范化到 [0, 360) 并就近取到 0, 90, 180, 270
//     int d = degrees % 360;
//     if (d < 0) d += 360;
//     // 四舍五入到最近的 90° 倍数
//     int rounded = ((d + 45) / 90) * 90;
//     if (rounded == 360) rounded = 0;
//     rotationDeg_.store(static_cast<float>(rounded));
//     LOGI("SetRotation: %d°", rounded);
// }
//
// void OpenGLManager::SetFlipHorizontal(bool enable) {
//     flipH_.store(enable);
//     LOGI("SetFlipHorizontal: %s", enable ? "true" : "false");
// }
//
// void OpenGLManager::SetFlipVertical(bool enable) {
//     flipV_.store(enable);
//     LOGI("SetFlipVertical: %s", enable ? "true" : "false");
// }