#ifndef CAMERA_NDK_CAMERA_H
#define CAMERA_NDK_CAMERA_H

#include <mutex>
#include "hilog/log.h"
#include "napi/native_api.h"
#include "ohcamera/camera.h"
#include "ohcamera/camera_input.h"
#include "ohcamera/capture_session.h"
#include "ohcamera/photo_output.h"
#include "ohcamera/preview_output.h"
#include "ohcamera/video_output.h"
#include "ohcamera/camera_manager.h"

namespace OHOS_CAMERA_SAMPLE {

/**
 * @brief 鸿蒙相机管理类，封装了相机设备操作、会话控制、输出流管理等核心功能。
 *        采用单例模式（通过静态成员 ndkCamera_ 实现），提供统一的相机访问接口。
 */
class NDKCamera {
public:
    ~NDKCamera();

    /**
     * @brief 构造函数，初始化相机管理器、会话、预览输出、相机输入等。
     * @param str              预览输出所需的 Surface ID（字符串）
     * @param focusMode        初始对焦模式（如 FOCUS_MODE_AUTO）
     * @param cameraDeviceIndex 要使用的相机设备索引（0 表示第一个设备，通常为后置）
     */
    NDKCamera(const std::string& str, uint32_t focusMode, uint32_t cameraDeviceIndex);

    /**
     * @brief 销毁单例实例，释放资源。
     */
    static void Destroy();

    // --------------------- 相机输入管理 ---------------------
    /**
     * @brief 根据当前选中的相机设备创建 CameraInput 对象。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode CreateCameraInput(void);

    /**
     * @brief 打开 CameraInput，启动相机设备。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode CameraInputOpen(void);

    /**
     * @brief 关闭 CameraInput，停止相机设备。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode CameraInputClose(void);

    /**
     * @brief 释放 CameraInput 资源。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode CameraInputRelease(void);

    // --------------------- 相机查询 ---------------------
    /**
     * @brief 获取设备支持的所有相机列表，存储在成员 cameras_ 中。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode GetSupportedCameras(void);

    /**
     * @brief 获取指定相机的输出能力（预览、拍照、录像等 profile）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode GetSupportedOutputCapability(void);

    // --------------------- 输出流创建 ---------------------
    /**
     * @brief 创建预览输出对象，使用构造时传入的 Surface ID。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode CreatePreviewOutput(void);

    /**
     * @brief 创建拍照输出对象，用于照片捕获。
     * @param photoId 照片输出所需的 Surface ID（字符指针）
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode CreatePhotoOutput(char *photoId);

    /**
     * @brief 创建录像输出对象。
     * @param videoId 录像输出所需的 Surface ID（字符指针）
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode CreateVideoOutput(char *videoId);

    /**
     * @brief 创建元数据输出对象（如人脸检测、二维码等）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode CreateMetadataOutput(void);

    // --------------------- 状态查询 ---------------------
    /**
     * @brief 查询当前相机是否处于静音状态（如系统策略限制）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode IsCameraMuted(void);

    // --------------------- 预览输出控制 ---------------------
    /**
     * @brief 停止预览流。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode PreviewOutputStop(void);

    /**
     * @brief 释放预览输出资源。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode PreviewOutputRelease(void);

    // --------------------- 拍照输出控制 ---------------------
    /**
     * @brief 释放拍照输出资源。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode PhotoOutputRelease(void);

    // --------------------- 闪光灯控制 ---------------------
    /**
     * @brief 检测并设置闪光灯模式（如自动、开启、关闭）。
     * @param mode 闪光灯模式（Camera_FlashMode 枚举值）
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode HasFlashFn(uint32_t mode);

    // --------------------- 视频防抖控制 ---------------------
    /**
     * @brief 检测并设置视频防抖模式。
     * @param mode 防抖模式（Camera_VideoStabilizationMode 枚举值）
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode IsVideoStabilizationModeSupportedFn(uint32_t mode);

    // --------------------- 变焦控制 ---------------------
    /**
     * @brief 设置变焦倍数（内部转换为 float）。
     * @param zoomRatio 变焦倍数（整数，实际使用时转为浮点）
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode setZoomRatioFn(uint32_t zoomRatio);

    // --------------------- 会话流程控制 ---------------------
    /**
     * @brief 执行完整的会话配置流程：开始配置 → 添加输入/输出 → 提交配置 → 启动会话。
     *        在构造函数中调用，用于初始化正常预览会话。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode SessionFlowFn(void);

    /**
     * @brief 开始会话配置（BeginConfig）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode SessionBegin(void);

    /**
     * @brief 提交会话配置（CommitConfig）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode SessionCommitConfig(void);

    /**
     * @brief 启动会话（Start）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode SessionStart(void);

    /**
     * @brief 停止会话（Stop）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode SessionStop(void);

    // --------------------- 录像相关 ---------------------
    /**
     * @brief 切换至录像模式：停止会话，重新配置添加录像输出和拍照输出，再启动。
     * @param videoId 录像 Surface ID
     * @param photoId 拍照 Surface ID
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode StartVideo(char *videoId, char *photoId);

    /**
     * @brief 将录像输出添加到会话中（需在 BeginConfig 后调用）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode AddVideoOutput(void);

    /**
     * @brief 将拍照输出添加到会话中（需在 BeginConfig 后调用）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode AddPhotoOutput();

    /**
     * @brief 启动录像输出（开始录像）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode VideoOutputStart(void);

    /**
     * @brief 停止录像输出。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode VideoOutputStop(void);

    /**
     * @brief 释放录像输出资源。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode VideoOutputRelease(void);

    // --------------------- 拍照相关 ---------------------
    /**
     * @brief 切换至拍照模式（若首次调用，会重建会话并添加拍照输出）。
     * @param mSurfaceId 照片 Surface ID
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode StartPhoto(char *mSurfaceId);

    /**
     * @brief 执行单次拍照（Capture）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode TakePicture(void);

    /**
     * @brief 带拍照设置参数执行拍照。
     * @param photoSetting 拍照设置结构体（含质量、旋转、位置等）
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode TakePictureWithPhotoSettings(Camera_PhotoCaptureSetting photoSetting);

    // --------------------- 曝光控制 ---------------------
    /**
     * @brief 检测并设置曝光模式。
     * @param mode 曝光模式（Camera_ExposureMode 枚举值）
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode IsExposureModeSupportedFn(uint32_t mode);

    /**
     * @brief 设置测光点坐标（需先设置曝光模式为测光模式）。
     * @param x 测光点 x 坐标（范围 0~1 映射到画面）
     * @param y 测光点 y 坐标
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode IsMeteringPoint(int x, int y);

    /**
     * @brief 设置曝光补偿值，并获取当前范围。
     * @param exposureBias 曝光补偿值（整数，实际转为 float）
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode IsExposureBiasRange(int exposureBias);

    // --------------------- 对焦控制 ---------------------
    /**
     * @brief 检测指定对焦模式是否支持。
     * @param mode 对焦模式（Camera_FocusMode 枚举值）
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode IsFocusModeSupported(uint32_t mode);

    /**
     * @brief 检测并设置对焦模式（同时设置）。
     * @param mode 对焦模式
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode IsFocusMode(uint32_t mode);

    /**
     * @brief 设置对焦点坐标。
     * @param x 对焦点 x（0~1）
     * @param y 对焦点 y（0~1）
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode IsFocusPoint(float x, float y);

    // --------------------- 资源释放 ---------------------
    /**
     * @brief 释放所有相机资源（输入、输出、会话等）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode ReleaseCamera(void);

    /**
     * @brief 释放会话资源（Release）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode SessionRealese(void);

    /**
     * @brief 停止预览并释放会话资源（组合操作）。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode ReleaseSession(void);

    // --------------------- 录像参数查询 ---------------------
    /**
     * @brief 获取当前录像帧宽度。
     * @return int32_t 宽度（像素）
     */
    int32_t GetVideoFrameWidth(void);

    /**
     * @brief 获取当前录像帧高度。
     * @return int32_t 高度（像素）
     */
    int32_t GetVideoFrameHeight(void);

    /**
     * @brief 获取当前录像帧率（最小值）。
     * @return int32_t 帧率
     */
    int32_t GetVideoFrameRate(void);

    // --------------------- 回调注册 ---------------------
    /**
     * @brief 注册 CameraManager 状态回调。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode CameraManagerRegisterCallback(void);

    /**
     * @brief 注册 CameraInput 错误回调。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode CameraInputRegisterCallback(void);

    /**
     * @brief 注册 PreviewOutput 帧开始/结束/错误回调。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode PreviewOutputRegisterCallback(void);

    /**
     * @brief 注册 PhotoOutput 帧开始/快门/结束/错误回调。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode PhotoOutputRegisterCallback(void);

    /**
     * @brief 注册 VideoOutput 帧开始/结束/错误回调。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode VideoOutputRegisterCallback(void);

    /**
     * @brief 注册 MetadataOutput 元数据可用/错误回调。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode MetadataOutputRegisterCallback(void);

    /**
     * @brief 注册 CaptureSession 对焦状态变化/错误回调。
     * @return Camera_ErrorCode 操作结果码。
     */
    Camera_ErrorCode CaptureSessionRegisterCallback(void);

    // --------------------- 获取回调结构体（供内部使用） ---------------------
    CameraManager_Callbacks *GetCameraManagerListener(void);
    CameraInput_Callbacks *GetCameraInputListener(void);
    PreviewOutput_Callbacks *GetPreviewOutputListener(void);
    PhotoOutput_Callbacks *GetPhotoOutputListener(void);
    VideoOutput_Callbacks *GetVideoOutputListener(void);
    MetadataOutput_Callbacks *GetMetadataOutputListener(void);
    CaptureSession_Callbacks *GetCaptureSessionRegister(void);

private:
    // 禁止拷贝和赋值
    NDKCamera(const NDKCamera &) = delete;
    NDKCamera &operator=(const NDKCamera &) = delete;

    // --------------------- 成员变量 ---------------------
    uint32_t cameraDeviceIndex_;                // 选中的相机设备索引
    Camera_Manager *cameraManager_;             // 相机管理器句柄
    Camera_CaptureSession *captureSession_;     // 捕获会话句柄
    Camera_Device *cameras_;                    // 支持的相机设备列表
    uint32_t size_;                             // cameras_ 数组大小
    Camera_OutputCapability *cameraOutputCapability_; // 当前相机的输出能力
    const Camera_Profile *profile_;             // 预览/拍照 profile
    const Camera_VideoProfile *videoProfile_;   // 录像 profile
    Camera_PreviewOutput *previewOutput_;       // 预览输出
    Camera_PhotoOutput *photoOutput_;           // 拍照输出
    Camera_VideoOutput *videoOutput_;           // 录像输出
    const Camera_MetadataObjectType *metaDataObjectType_; // 元数据类型
    Camera_MetadataOutput *metadataOutput_;     // 元数据输出
    Camera_Input *cameraInput_;                 // 相机输入
    bool *isCameraMuted_;                       // 静音状态指针
    Camera_Position position_;                  // 相机位置（前后置）
    Camera_Type type_;                          // 相机类型
    std::string previewSurfaceId_;              // 预览 Surface ID
    char *photoSurfaceId_;                      // 拍照 Surface ID
    Camera_ErrorCode ret_;                      // 最近一次操作的返回码
    uint32_t takePictureTimes = 0;              // 拍照计数（用于判断是否为首次）
    Camera_ExposureMode exposureMode_;          // 当前曝光模式
    bool isExposureModeSupported_;              // 当前曝光模式是否支持
    bool isFocusModeSupported_;                 // 当前对焦模式是否支持
    float minExposureBias_;                     // 最小曝光补偿值
    float maxExposureBias_;                     // 最大曝光补偿值
    float step_;                                // 曝光补偿步长
    uint32_t focusMode_;                        // 初始对焦模式

    static NDKCamera *ndkCamera_;               // 单例实例指针
    static std::mutex mtx_;                     // 单例互斥锁
    volatile bool valid_;                       // 对象有效性标志
};

}  // namespace OHOS_CAMERA_SAMPLE
#endif  // CAMERA_NDK_CAMERA_H