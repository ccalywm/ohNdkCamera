// #pragma once
//
// #include <string>
// #include "ohcamera/camera.h"
// #include "ohcamera/camera_input.h"
// #include "ohcamera/capture_session.h"
// #include "ohcamera/preview_output.h"
// #include "ohcamera/camera_manager.h"
//
// namespace OHOS_CAMERA_SAMPLE {
//
// /**
//  * @brief 鸿蒙相机预览类（精简版），仅提供预览功能，不包含拍照、录像等。
//  *        支持通过 Surface ID 初始化，非单例，可构造多个实例。
//  */
// class NDKCamera {
// public:
//     /**
//      * @brief 构造函数，传入预览 Surface ID 和相机设备索引。
//      * @param str               预览 Surface ID（字符串）
//      * @param cameraDeviceIndex 相机设备索引，默认为 0（通常为后置）
//      */
//     NDKCamera(const std::string& str, uint32_t cameraDeviceIndex = 0);
//
//     ~NDKCamera();
//
//     // 禁止拷贝和赋值
//     NDKCamera(const NDKCamera &) = delete;
//     NDKCamera &operator=(const NDKCamera &) = delete;
//
//     /**
//      * @brief 释放所有相机资源（输入、输出、会话、管理器）。
//      * @return Camera_ErrorCode 操作结果码。
//      */
//     Camera_ErrorCode ReleaseCamera(void);
//
//     /**
//      * @brief 检查相机是否初始化成功且预览已启动。
//      * @return true 有效，false 无效。
//      */
//     bool IsValid() const { return valid_; }
//
// private:
//     // --------------------- 内部初始化函数 ---------------------
//   
//     Camera_ErrorCode SessionFlowFn(void);   // 配置并启动会话
//
//     // --------------------- 内部释放函数 ---------------------
//
//     // --------------------- 回调注册 ---------------------
//     Camera_ErrorCode CameraManagerRegisterCallback(void);
//     Camera_ErrorCode CameraInputRegisterCallback(void);
//     Camera_ErrorCode PreviewOutputRegisterCallback(void);
//     Camera_ErrorCode CaptureSessionRegisterCallback(void);
//
//     // 静态回调函数
//     static void CameraManagerStatusCallback(Camera_Manager *cameraManager, Camera_StatusInfo *status);
//     static void OnCameraInputError(const Camera_Input *cameraInput, Camera_ErrorCode errorCode);
//     static void PreviewOutputOnFrameStart(Camera_PreviewOutput *previewOutput);
//     static void PreviewOutputOnFrameEnd(Camera_PreviewOutput *previewOutput, int32_t frameCount);
//     static void PreviewOutputOnError(Camera_PreviewOutput *previewOutput, Camera_ErrorCode errorCode);
//     static void CaptureSessionOnError(Camera_CaptureSession *session, Camera_ErrorCode errorCode);
//
//     // 获取回调结构体
//     CameraManager_Callbacks *GetCameraManagerListener(void);
//     CameraInput_Callbacks *GetCameraInputListener(void);
//     PreviewOutput_Callbacks *GetPreviewOutputListener(void);
//     CaptureSession_Callbacks *GetCaptureSessionRegister(void);
//
//     // --------------------- 成员变量 ---------------------
//     uint32_t cameraDeviceIndex_;                // 相机设备索引
//     Camera_Manager *cameraManager_;             // 相机管理器
//     Camera_CaptureSession *captureSession_;     // 捕获会话
//     Camera_Device *cameras_;                    // 支持的相机设备列表
//     uint32_t size_;                             // 设备列表大小
//     Camera_OutputCapability *cameraOutputCapability_; // 当前相机输出能力
//     const Camera_Profile *profile_;             // 预览 profile
//     Camera_PreviewOutput *previewOutput_;       // 预览输出
//     Camera_Input *cameraInput_;                 // 相机输入
//     std::string previewSurfaceId_;              // 预览 Surface ID
//     Camera_ErrorCode ret_;                      // 最近操作的返回码
//     volatile bool valid_;                       // 对象有效性标志
// };
//
// } 