//
// #define LOG_TAG "NDKCamera"
// #include "camera_manager.h"
// #include "util/DebugLog.h"
//
// namespace OHOS_CAMERA_SAMPLE {
//
// // ===================== 构造 / 析构 =====================
//
// NDKCamera::NDKCamera(const std::string &str, uint32_t cameraDeviceIndex)
//     : cameraDeviceIndex_(cameraDeviceIndex), cameraManager_(nullptr), captureSession_(nullptr), cameras_(nullptr),
//       size_(0), cameraOutputCapability_(nullptr), profile_(nullptr), previewOutput_(nullptr), cameraInput_(nullptr),
//       previewSurfaceId_(str), ret_(CAMERA_OK), valid_(false) {
//     // 1. 获取相机管理器
//     ret_ = OH_Camera_GetCameraManager(&cameraManager_);
//     if (cameraManager_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE("获取相机管理器失败。");
//         return;
//     }
//     CameraManagerRegisterCallback();
//
//     // 2. 获取支持的相机列表
//     ret_ = OH_CameraManager_GetSupportedCameras(cameraManager_, &cameras_, &size_);
//     if (cameras_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE("获取支持的相机列表失败。");
//         return;
//     }
//
//     // 2.5 检查相机索引是否合法
//     if (size_ == 0 || cameraDeviceIndex_ >= size_) {
//         LOGE("相机索引越界：index=%u, size=%u", cameraDeviceIndex_, size_);
//         return;
//     }
//    
//     // 3. 获取当前相机的输出能力
//     ret_ = OH_CameraManager_GetSupportedCameraOutputCapability(cameraManager_, &cameras_[cameraDeviceIndex_],
//                                                                &cameraOutputCapability_);
//     if (cameraOutputCapability_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE("获取相机输出能力失败。");
//         return;
//     }
//
//     // 4. 创建相机输入
//     ret_ = OH_CameraManager_CreateCameraInput(cameraManager_, &cameras_[cameraDeviceIndex_], &cameraInput_);
//     if (cameraInput_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE("创建相机输入失败。");
//         return;
//     }
//     CameraInputRegisterCallback();
//
//     // 5. 打开相机
//     ret_ = OH_CameraInput_Open(cameraInput_);
//     if (ret_ != CAMERA_OK) {
//         LOGE("打开相机输入失败。");
//         return;
//     }
//
//     // 6. 创建预览输出
//     profile_ = cameraOutputCapability_->previewProfiles[0];
//     if (profile_ == nullptr) {
//         LOGE("获取预览Profile失败。");
//         return;
//     }
//     ret_ = OH_CameraManager_CreatePreviewOutput(cameraManager_, profile_, previewSurfaceId_.c_str(), &previewOutput_);
//     if (previewOutput_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE("创建预览输出失败。");
//         return;
//     }
//     PreviewOutputRegisterCallback();
//
//     // 7. 创建捕获会话
//     ret_ = OH_CameraManager_CreateCaptureSession(cameraManager_, &captureSession_);
//     if (captureSession_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE("创建捕获会话失败。");
//         return;
//     }
//     CaptureSessionRegisterCallback();
//
//     // 8. 配置并启动会话
//     ret_ = SessionFlowFn();
//     if (ret_ != CAMERA_OK) {
//         LOGE("配置并启动会话失败。");
//         return;
//     }
//
//     valid_ = true;
//     LOGE("相机预览启动成功。");
// }
//
// NDKCamera::~NDKCamera() { ReleaseCamera(); }
//
// // ===================== 公共接口 =====================
//
// Camera_ErrorCode NDKCamera::ReleaseCamera(void) {
//     if (!valid_) {
//         return CAMERA_OK;
//     }
//     valid_ = false;
//
//     // 停止并移除预览输出
//     if (previewOutput_) {
//         OH_PreviewOutput_Stop(previewOutput_);
//         OH_PreviewOutput_Release(previewOutput_);
//         if (captureSession_) {
//             OH_CaptureSession_RemovePreviewOutput(captureSession_, previewOutput_);
//         }
//         previewOutput_ = nullptr;
//     }
//
//     // 释放会话
//     if (captureSession_) {
//         OH_CaptureSession_Stop(captureSession_);
//         OH_CaptureSession_Release(captureSession_);
//         captureSession_ = nullptr;
//     }
//
//     // 关闭并释放相机输入
//     if (cameraInput_) {
//         OH_CameraInput_Close(cameraInput_);
//         OH_CameraInput_Release(cameraInput_);
//         cameraInput_ = nullptr;
//     }
//
//     // 释放相机管理器及能力缓存
//     if (cameraManager_) {
//         if (cameraOutputCapability_) {
//             OH_CameraManager_DeleteSupportedCameraOutputCapability(cameraManager_, cameraOutputCapability_);
//             cameraOutputCapability_ = nullptr;
//         }
//         if (cameras_) {
//             OH_CameraManager_DeleteSupportedCameras(cameraManager_, cameras_, size_);
//             cameras_ = nullptr;
//             size_ = 0;
//         }
//         OH_Camera_DeleteCameraManager(cameraManager_);
//         cameraManager_ = nullptr;
//     }
//
//     LOGE("释放相机资源完成。");
//     return CAMERA_OK;
// }
//
// // ===================== 内部初始化函数 =====================
//
// Camera_ErrorCode NDKCamera::SessionFlowFn(void) {
//     LOGE("开始配置会话流程。");
//
//     // 开始配置会话
//     ret_ = OH_CaptureSession_BeginConfig(captureSession_);
//     if (ret_ != CAMERA_OK) {
//         LOGE("开始配置会话失败。");
//         return ret_;
//     }
//
//     // 添加相机输入
//     ret_ = OH_CaptureSession_AddInput(captureSession_, cameraInput_);
//     if (ret_ != CAMERA_OK) {
//         LOGE("添加相机输入到会话失败。");
//         return ret_;
//     }
//
//     // 添加预览输出
//     ret_ = OH_CaptureSession_AddPreviewOutput(captureSession_, previewOutput_);
//     if (ret_ != CAMERA_OK) {
//         LOGE("添加预览输出到会话失败。");
//         return ret_;
//     }
//
//     // 提交配置
//     ret_ = OH_CaptureSession_CommitConfig(captureSession_);
//     if (ret_ != CAMERA_OK) {
//         LOGE("提交会话配置失败。");
//         return ret_;
//     }
//
//     // 启动会话
//     ret_ = OH_CaptureSession_Start(captureSession_);
//     if (ret_ != CAMERA_OK) {
//         LOGE("启动会话失败。");
//         return ret_;
//     }
//
//     LOGE("会话配置和启动成功。");
//     return ret_;
// }
//
// // ===================== 内部释放函数 =====================
//
// // ===================== 回调注册实现 =====================
//
// void NDKCamera::CameraManagerStatusCallback(Camera_Manager *cameraManager, Camera_StatusInfo *status) {
//     LOGE("相机管理器状态回调。");
// }
// CameraManager_Callbacks *NDKCamera::GetCameraManagerListener(void) {
//     static CameraManager_Callbacks listener = {.onCameraStatus = CameraManagerStatusCallback};
//     return &listener;
// }
// Camera_ErrorCode NDKCamera::CameraManagerRegisterCallback(void) {
//     ret_ = OH_CameraManager_RegisterCallback(cameraManager_, GetCameraManagerListener());
//     if (ret_ != CAMERA_OK) {
//         LOGE("注册相机管理器回调失败。");
//     }
//     return ret_;
// }
//
// void NDKCamera::OnCameraInputError(const Camera_Input *cameraInput, Camera_ErrorCode errorCode) {
//     LOGE("相机输入错误，错误码：%d", errorCode);
// }
// CameraInput_Callbacks *NDKCamera::GetCameraInputListener(void) {
//     static CameraInput_Callbacks listener = {.onError = OnCameraInputError};
//     return &listener;
// }
// Camera_ErrorCode NDKCamera::CameraInputRegisterCallback(void) {
//     ret_ = OH_CameraInput_RegisterCallback(cameraInput_, GetCameraInputListener());
//     if (ret_ != CAMERA_OK) {
//         LOGE("注册相机输入回调失败。");
//     }
//     return ret_;
// }
//
// void NDKCamera::PreviewOutputOnFrameStart(Camera_PreviewOutput *previewOutput) { LOGE("预览帧开始。"); }
// void NDKCamera::PreviewOutputOnFrameEnd(Camera_PreviewOutput *previewOutput, int32_t frameCount) {
//     LOGE("预览帧结束，帧计数：%d", frameCount);
// }
// void NDKCamera::PreviewOutputOnError(Camera_PreviewOutput *previewOutput, Camera_ErrorCode errorCode) {
//     LOGE("预览输出错误，错误码：%d", errorCode);
// }
// PreviewOutput_Callbacks *NDKCamera::GetPreviewOutputListener(void) {
//     static PreviewOutput_Callbacks listener = {.onFrameStart = PreviewOutputOnFrameStart,
//                                                .onFrameEnd = PreviewOutputOnFrameEnd,
//                                                .onError = PreviewOutputOnError};
//     return &listener;
// }
// Camera_ErrorCode NDKCamera::PreviewOutputRegisterCallback(void) {
//     ret_ = OH_PreviewOutput_RegisterCallback(previewOutput_, GetPreviewOutputListener());
//     if (ret_ != CAMERA_OK) {
//         LOGE("注册预览输出回调失败。");
//     }
//     return ret_;
// }
//
// void NDKCamera::CaptureSessionOnError(Camera_CaptureSession *session, Camera_ErrorCode errorCode) {
//     LOGE("捕获会话错误，错误码：%d", errorCode);
// }
// CaptureSession_Callbacks *NDKCamera::GetCaptureSessionRegister(void) {
//     static CaptureSession_Callbacks listener = {.onFocusStateChange = nullptr, .onError = CaptureSessionOnError};
//     return &listener;
// }
// Camera_ErrorCode NDKCamera::CaptureSessionRegisterCallback(void) {
//     ret_ = OH_CaptureSession_RegisterCallback(captureSession_, GetCaptureSessionRegister());
//     if (ret_ != CAMERA_OK) {
//         LOGE("注册捕获会话回调失败。");
//     }
//     return ret_;
// }
//
// }