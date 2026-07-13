#include "camera_manager.h"

// #include "opengl_manager.h"
// #include "opengl/OpenGLManager.h"

#include "util/DebugLog.h"

namespace OHOS_CAMERA_SAMPLE {
NDKCamera *NDKCamera::ndkCamera_ = nullptr;
std::mutex NDKCamera::mtx_;

NDKCamera::NDKCamera(const std::string& str, uint32_t focusMode, uint32_t cameraDeviceIndex)
    : previewSurfaceId_(str),
      cameras_(nullptr),
      focusMode_(focusMode),
      cameraDeviceIndex_(cameraDeviceIndex),
      cameraOutputCapability_(nullptr),
      cameraInput_(nullptr),
      captureSession_(nullptr),
      size_(0),
      isCameraMuted_(nullptr),
      profile_(nullptr),
      photoSurfaceId_(nullptr),
      previewOutput_(nullptr),
      photoOutput_(nullptr),
      metaDataObjectType_(nullptr),
      metadataOutput_(nullptr),
      isExposureModeSupported_(false),
      isFocusModeSupported_(false),
      exposureMode_(EXPOSURE_MODE_LOCKED),
      minExposureBias_(0),
      maxExposureBias_(0),
      step_(0),
      ret_(CAMERA_OK)
{
    // // 获取 OpenGLManager 提供的输入 Surface ID
    // std::string inputSurfaceId = OpenGLManager::GetInstance().GetInputSurfaceId();
    // if (inputSurfaceId.empty()) {
    //     LOGE( "OpenGLManager not ready, camera init failed");
    //     return;
    // }
    // // 将 previewSurfaceId_ 设为该 ID
    // previewSurfaceId_ = strdup(inputSurfaceId.c_str());
    // previewSurfaceId_ = strdup(inputSurfaceId.c_str());
    valid_ = false;
    ReleaseCamera();
    Camera_ErrorCode ret = OH_Camera_GetCameraManager(&cameraManager_);  // 创建manager对象
    if (cameraManager_ == nullptr || ret != CAMERA_OK) {
        LOGE( "Get CameraManager failed.");
    }

    ret = OH_CameraManager_CreateCaptureSession(cameraManager_, &captureSession_);
    if (captureSession_ == nullptr || ret != CAMERA_OK) {
        LOGE( "Create captureSession failed.");
    }
    CaptureSessionRegisterCallback();
    GetSupportedCameras();
    GetSupportedOutputCapability();
    CreatePreviewOutput();
    CreateCameraInput();
    CameraInputOpen();
    CameraManagerRegisterCallback();
    SessionFlowFn();
    valid_ = true;
}  // 设备输入设置

NDKCamera::~NDKCamera()
{
    valid_ = false;
    LOGE( "~NDKCamera");
    Camera_ErrorCode ret = CAMERA_OK;

    if (cameraManager_) {
        LOGE( "Release OH_CameraManager_DeleteSupportedCameraOutputCapability. enter");
        ret = OH_CameraManager_DeleteSupportedCameraOutputCapability(cameraManager_, cameraOutputCapability_);
        if (ret != CAMERA_OK) {
            LOGE( "Delete CameraOutputCapability failed.");
        } else {
            LOGE( "Release OH_CameraManager_DeleteSupportedCameraOutputCapability. ok");
        }

        LOGE( "Release OH_CameraManager_DeleteSupportedCameras. enter");
        ret = OH_CameraManager_DeleteSupportedCameras(cameraManager_, cameras_, size_);
        if (ret != CAMERA_OK) {
            LOGE( "Delete Cameras failed.");
        } else {
            LOGE( "Release OH_CameraManager_DeleteSupportedCameras. ok");
        }

        ret = OH_Camera_DeleteCameraManager(cameraManager_);
        if (ret != CAMERA_OK) {
            LOGE( "Delete CameraManager failed.");
        } else {
            LOGE( "Release OH_Camera_DeleteCameraMananger. ok");
        }
        cameraManager_ = nullptr;
    }
    LOGE( "~NDKCamera exit");
}

Camera_ErrorCode NDKCamera::ReleaseCamera(void)
{
    LOGE( " enter ReleaseCamera");
    if (previewOutput_) {
        PreviewOutputStop();
        PreviewOutputRelease();
        OH_CaptureSession_RemovePreviewOutput(captureSession_, previewOutput_);
    }
    if (photoOutput_) {
        PhotoOutputRelease();
    }
    if (captureSession_) {
        SessionRealese();
    }
    if (cameraInput_) {
        CameraInputClose();
    }
    LOGE( " exit ReleaseCamera");
    return ret_;
}

Camera_ErrorCode NDKCamera::ReleaseSession(void)
{
    LOGE( " enter ReleaseSession");
    PreviewOutputStop();
    PhotoOutputRelease();
    SessionRealese();
    LOGE( " exit ReleaseSession");
    return ret_;
}

Camera_ErrorCode NDKCamera::SessionRealese(void)
{
    LOGE( " enter SessionRealese");
    Camera_ErrorCode ret = OH_CaptureSession_Release(captureSession_);
    captureSession_ = nullptr;
    LOGE( " exit SessionRealese");
    return ret;
}

Camera_ErrorCode NDKCamera::HasFlashFn(uint32_t mode)
{
    Camera_FlashMode flashMode = static_cast<Camera_FlashMode>(mode);
    // Check for flashing lights
    bool hasFlash = false;
    Camera_ErrorCode ret = OH_CaptureSession_HasFlash(captureSession_, &hasFlash);
    if (captureSession_ == nullptr || ret != CAMERA_OK) {
        LOGE( "OH_CaptureSession_HasFlash failed.");
    }
    if (hasFlash) {
        LOGE( "hasFlash success-----");
    } else {
        LOGE( "hasFlash fail-----");
    }

    // Check if the flash mode is supported
    bool isSupported = false;
    ret = OH_CaptureSession_IsFlashModeSupported(captureSession_, flashMode, &isSupported);
    if (ret != CAMERA_OK) {
        LOGE( "OH_CaptureSession_IsFlashModeSupported failed.");
    }
    if (isSupported) {
        LOGE( "isFlashModeSupported success-----");
    } else {
        LOGE( "isFlashModeSupported fail-----");
    }

    // Set flash mode
    ret = OH_CaptureSession_SetFlashMode(captureSession_, flashMode);
    if (ret == CAMERA_OK) {
        LOGE( "OH_CaptureSession_SetFlashMode success.");
    } else {
        LOGE( "OH_CaptureSession_SetFlashMode failed. %{public}d ", ret);
    }

    // Obtain the flash mode of the current device
    ret = OH_CaptureSession_GetFlashMode(captureSession_, &flashMode);
    if (ret == CAMERA_OK) {
        LOGE( "OH_CaptureSession_GetFlashMode success. flashMode：%{public}d ", flashMode);
    } else {
        LOGE( "OH_CaptureSession_GetFlashMode failed. %d ", ret);
    }
    return ret;
}

Camera_ErrorCode NDKCamera::IsVideoStabilizationModeSupportedFn(uint32_t mode)
{
    Camera_VideoStabilizationMode videoMode = static_cast<Camera_VideoStabilizationMode>(mode);
    // Check if the specified video anti shake mode is supported
    bool isSupported = false;
    Camera_ErrorCode ret =
        OH_CaptureSession_IsVideoStabilizationModeSupported(captureSession_, videoMode, &isSupported);
    if (ret != CAMERA_OK) {
        LOGE( "OH_CaptureSession_IsVideoStabilizationModeSupported failed.");
    }
    if (isSupported) {
        LOGE( "OH_CaptureSession_IsVideoStabilizationModeSupported success-----");
    } else {
        LOGE( "OH_CaptureSession_IsVideoStabilizationModeSupported fail-----");
    }

    // Set video stabilization
    ret = OH_CaptureSession_SetVideoStabilizationMode(captureSession_, videoMode);
    if (ret == CAMERA_OK) {
        LOGE( "OH_CaptureSession_SetVideoStabilizationMode success.");
    } else {
        LOGE( "OH_CaptureSession_SetVideoStabilizationMode failed. %{public}d ", ret);
    }

    ret = OH_CaptureSession_GetVideoStabilizationMode(captureSession_, &videoMode);
    if (ret == CAMERA_OK) {
        LOGE( "OH_CaptureSession_GetVideoStabilizationMode success. videoMode：%u ", videoMode);
    } else {
        LOGE( "OH_CaptureSession_GetVideoStabilizationMode failed. %{public}d ", ret);
    }
    return ret;
}

Camera_ErrorCode NDKCamera::setZoomRatioFn(uint32_t zoomRatio)
{
    float zoom = float(zoomRatio);
    // Obtain supported zoom range
    float minZoom;
    float maxZoom;
    Camera_ErrorCode ret = OH_CaptureSession_GetZoomRatioRange(captureSession_, &minZoom, &maxZoom);
    if (captureSession_ == nullptr || ret != CAMERA_OK) {
        LOGE( "OH_CaptureSession_GetZoomRatioRange failed.");
    } else {
        LOGE( "OH_CaptureSession_GetZoomRatioRange success. minZoom: %{public}f, maxZoom:%{public}f",
                    minZoom, maxZoom);
    }

    // Set Zoom
    ret = OH_CaptureSession_SetZoomRatio(captureSession_, zoom);
    if (ret == CAMERA_OK) {
        LOGE( "OH_CaptureSession_SetZoomRatio success.");
    } else {
        LOGE( "OH_CaptureSession_SetZoomRatio failed. %{public}d ", ret);
    }

    // Obtain the zoom value of the current device
    ret = OH_CaptureSession_GetZoomRatio(captureSession_, &zoom);
    if (ret == CAMERA_OK) {
        LOGE( "OH_CaptureSession_GetZoomRatio success. zoom：%{public}f ", zoom);
    } else {
        LOGE( "OH_CaptureSession_GetZoomRatio failed. %{public}d ", ret);
    }
    return ret;
}

Camera_ErrorCode NDKCamera::SessionBegin(void)
{
    Camera_ErrorCode ret = OH_CaptureSession_BeginConfig(captureSession_);
    if (ret == CAMERA_OK) {
        LOGE( "OH_CaptureSession_BeginConfig success.");
    } else {
        LOGE( "OH_CaptureSession_BeginConfig failed. %d ", ret);
    }
    return ret;
}

Camera_ErrorCode NDKCamera::SessionCommitConfig(void)
{
    Camera_ErrorCode ret = OH_CaptureSession_CommitConfig(captureSession_);
    if (ret == CAMERA_OK) {
        LOGE( "OH_CaptureSession_CommitConfig success.");
    } else {
        LOGE( "OH_CaptureSession_CommitConfig failed. %d ", ret);
    }
    return ret;
}

Camera_ErrorCode NDKCamera::SessionStart(void)
{
    Camera_ErrorCode ret = OH_CaptureSession_Start(captureSession_);
    if (ret == CAMERA_OK) {
        LOGE( "OH_CaptureSession_Start success.");
    } else {
        LOGE( "OH_CaptureSession_Start failed. %d ", ret);
    }
    return ret;
}

Camera_ErrorCode NDKCamera::SessionStop(void)
{
    Camera_ErrorCode ret = OH_CaptureSession_Stop(captureSession_);
    if (ret == CAMERA_OK) {
        LOGE( "OH_CaptureSession_Stop success.");
    } else {
        LOGE( "OH_CaptureSession_Stop failed. %d ", ret);
    }
    return ret;
}

Camera_ErrorCode NDKCamera::SessionFlowFn(void)
{
    LOGE( "Start SessionFlowFn IN.");
    // Start configuring session
    LOGE( "session beginConfig.");
    Camera_ErrorCode ret = OH_CaptureSession_BeginConfig(captureSession_);

    // Add CameraInput to the session
    LOGE( "session addInput.");
    ret = OH_CaptureSession_AddInput(captureSession_, cameraInput_);

    // Add previewOutput to the session
    LOGE( "session add Preview Output.");
    ret = OH_CaptureSession_AddPreviewOutput(captureSession_, previewOutput_);

    // Adding PhotoOutput to the Session
    LOGE( "session add Photo Output.");

    // Submit configuration information
    LOGE( "session commitConfig");
    ret = OH_CaptureSession_CommitConfig(captureSession_);

    // Start Session Work
    LOGE( "session start");
    ret = OH_CaptureSession_Start(captureSession_);
    LOGE( "session success");

    // Start focusing
    LOGE( "IsFocusMode start");
    ret = IsFocusMode(focusMode_);
    LOGE( "IsFocusMode success");
    return ret;
}

Camera_ErrorCode NDKCamera::CreateCameraInput(void)
{
    LOGE( "enter CreateCameraInput.");
    ret_ = OH_CameraManager_CreateCameraInput(cameraManager_, &cameras_[cameraDeviceIndex_], &cameraInput_);
    if (cameraInput_ == nullptr || ret_ != CAMERA_OK) {
        LOGE( "CreateCameraInput failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    LOGE( "exit CreateCameraInput.");
    CameraInputRegisterCallback();
    return ret_;
}

Camera_ErrorCode NDKCamera::CameraInputOpen(void)
{
    LOGE( "enter CameraInputOpen.");
    ret_ = OH_CameraInput_Open(cameraInput_);
    if (ret_ != CAMERA_OK) {
        LOGE( "CameraInput_Open failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    LOGE( "exit CameraInputOpen.");
    return ret_;
}

Camera_ErrorCode NDKCamera::CameraInputClose(void)
{
    LOGE( "enter CameraInput_Close.");
    ret_ = OH_CameraInput_Close(cameraInput_);
    if (ret_ != CAMERA_OK) {
        LOGE( "CameraInput_Close failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    LOGE( "exit CameraInput_Close.");
    return ret_;
}

Camera_ErrorCode NDKCamera::CameraInputRelease(void)
{
    LOGE( "enter CameraInputRelease.");
    ret_ = OH_CameraInput_Release(cameraInput_);
    if (ret_ != CAMERA_OK) {
        LOGE( "CameraInput_Release failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    LOGE( "exit CameraInputRelease.");
    return ret_;
}

Camera_ErrorCode NDKCamera::GetSupportedCameras(void)
{
    ret_ = OH_CameraManager_GetSupportedCameras(cameraManager_, &cameras_, &size_);
    if (cameras_ == nullptr || &size_ == nullptr || ret_ != CAMERA_OK) {
        LOGE( "Get supported cameras failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return ret_;
}

Camera_ErrorCode NDKCamera::GetSupportedOutputCapability(void)
{
    ret_ = OH_CameraManager_GetSupportedCameraOutputCapability(cameraManager_, &cameras_[cameraDeviceIndex_],
                                                               &cameraOutputCapability_);
    if (cameraOutputCapability_ == nullptr || ret_ != CAMERA_OK) {
        LOGE( "GetSupportedCameraOutputCapability failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return ret_;
}

Camera_ErrorCode NDKCamera::CreatePreviewOutput(void)
{
    profile_ = cameraOutputCapability_->previewProfiles[0];
    if (profile_ == nullptr) {
        LOGE( "Get previewProfiles failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    ret_ = OH_CameraManager_CreatePreviewOutput(cameraManager_, profile_, previewSurfaceId_.c_str(), &previewOutput_);
    if (previewSurfaceId_.empty() || previewOutput_ == nullptr || ret_ != CAMERA_OK) {
        LOGE( "CreatePreviewOutput failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return ret_;
    PreviewOutputRegisterCallback();
}

Camera_ErrorCode NDKCamera::CreatePhotoOutput(char *photoSurfaceId)
{
    profile_ = cameraOutputCapability_->photoProfiles[0];
    if (profile_ == nullptr) {
        LOGE( "Get photoProfiles failed.");
        return CAMERA_INVALID_ARGUMENT;
    }

    if (photoSurfaceId == nullptr) {
        LOGE( "CreatePhotoOutput failed.");
        return CAMERA_INVALID_ARGUMENT;
    }

    ret_ = OH_CameraManager_CreatePhotoOutput(cameraManager_, profile_, photoSurfaceId, &photoOutput_);
    PhotoOutputRegisterCallback();
    return ret_;
}

Camera_ErrorCode NDKCamera::CreateVideoOutput(char *videoId)
{
    videoProfile_ = cameraOutputCapability_->videoProfiles[0];

    if (videoProfile_ == nullptr) {
        LOGE( "Get videoProfiles failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    ret_ = OH_CameraManager_CreateVideoOutput(cameraManager_, videoProfile_, videoId, &videoOutput_);
    if (videoId == nullptr || videoOutput_ == nullptr || ret_ != CAMERA_OK) {
        LOGE( "CreateVideoOutput failed.");
        return CAMERA_INVALID_ARGUMENT;
    }

    return ret_;
}

Camera_ErrorCode NDKCamera::AddVideoOutput(void)
{
    Camera_ErrorCode ret = OH_CaptureSession_AddVideoOutput(captureSession_, videoOutput_);
    if (ret == CAMERA_OK) {
        LOGE( "OH_CaptureSession_AddVideoOutput success.");
    } else {
        LOGE( "OH_CaptureSession_AddVideoOutput failed. %d ", ret);
    }
    return ret;
}

Camera_ErrorCode NDKCamera::AddPhotoOutput()
{
    Camera_ErrorCode ret = OH_CaptureSession_AddPhotoOutput(captureSession_, photoOutput_);
    if (ret == CAMERA_OK) {
        LOGE( "OH_CaptureSession_AddPhotoOutput success.");
    } else {
        LOGE( "OH_CaptureSession_AddPhotoOutput failed. %d ", ret);
    }
    return ret;
}

Camera_ErrorCode NDKCamera::CreateMetadataOutput(void)
{
    metaDataObjectType_ = cameraOutputCapability_->supportedMetadataObjectTypes[0];
    if (metaDataObjectType_ == nullptr) {
        LOGE( "Get metaDataObjectType failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    ret_ = OH_CameraManager_CreateMetadataOutput(cameraManager_, metaDataObjectType_, &metadataOutput_);
    if (metadataOutput_ == nullptr || ret_ != CAMERA_OK) {
        LOGE( "CreateMetadataOutput failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    MetadataOutputRegisterCallback();
    return ret_;
}

Camera_ErrorCode NDKCamera::IsCameraMuted(void)
{
    ret_ = OH_CameraManager_IsCameraMuted(cameraManager_, isCameraMuted_);
    if (isCameraMuted_ == nullptr || ret_ != CAMERA_OK) {
        LOGE( "IsCameraMuted failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return ret_;
}

Camera_ErrorCode NDKCamera::PreviewOutputStop(void)
{
    LOGE( "enter PreviewOutputStop.");
    ret_ = OH_PreviewOutput_Stop(previewOutput_);
    if (ret_ != CAMERA_OK) {
        LOGE( "PreviewOutputStop failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return ret_;
}

Camera_ErrorCode NDKCamera::PreviewOutputRelease(void)
{
    LOGE( "enter PreviewOutputRelease.");
    ret_ = OH_PreviewOutput_Release(previewOutput_);
    if (ret_ != CAMERA_OK) {
        LOGE( "PreviewOutputRelease failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return ret_;
}

Camera_ErrorCode NDKCamera::PhotoOutputRelease(void)
{
    LOGE( "enter PhotoOutputRelease.");
    ret_ = OH_PhotoOutput_Release(photoOutput_);
    if (ret_ != CAMERA_OK) {
        LOGE( "PhotoOutputRelease failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return ret_;
}

Camera_ErrorCode NDKCamera::StartVideo(char *videoId, char *photoId)
{
    LOGE( "StartVideo begin.");
    Camera_ErrorCode ret = SessionStop();
    if (ret == CAMERA_OK) {
        LOGE( "SessionStop success.");
    } else {
        LOGE( "SessionStop failed. %d ", ret);
    }
    ret = SessionBegin();
    if (ret == CAMERA_OK) {
        LOGE( "SessionBegin success.");
    } else {
        LOGE( "SessionBegin failed. %d ", ret);
    }
    OH_CaptureSession_RemovePhotoOutput(captureSession_, photoOutput_);
    CreatePhotoOutput(photoId);
    AddPhotoOutput();
    CreateVideoOutput(videoId);
    AddVideoOutput();
    SessionCommitConfig();
    SessionStart();
    VideoOutputRegisterCallback();
    return ret;
}

Camera_ErrorCode NDKCamera::VideoOutputStart(void)
{
    LOGE( "VideoOutputStart begin.");
    Camera_ErrorCode ret = OH_VideoOutput_Start(videoOutput_);
    if (ret == CAMERA_OK) {
        LOGE( "OH_VideoOutput_Start success.");
    } else {
        LOGE( "OH_VideoOutput_Start failed. %d ", ret);
    }
    return ret;
}

Camera_ErrorCode NDKCamera::StartPhoto(char *mSurfaceId)
{
    Camera_ErrorCode ret = CAMERA_OK;
    if (takePictureTimes == 0) {
        ret = SessionStop();
        if (ret == CAMERA_OK) {
            LOGE( "SessionStop success.");
        } else {
            LOGE( "SessionStop failed. %d ", ret);
        }
        ret = SessionBegin();
        if (ret == CAMERA_OK) {
            LOGE( "SessionBegin success.");
        } else {
            LOGE( "SessionBegin failed. %d ", ret);
        }
        LOGE( "startPhoto begin.");
        ret = CreatePhotoOutput(mSurfaceId);

        LOGE( "startPhoto CreatePhotoOutput ret = %{public}d.", ret);
        ret = OH_CaptureSession_AddPhotoOutput(captureSession_, photoOutput_);
        LOGE( "startPhoto AddPhotoOutput ret = %{public}d.", ret);
        ret = SessionCommitConfig();

        LOGE( "startPhoto SessionCommitConfig ret = %{public}d.", ret);
        ret = SessionStart();
        LOGE( "startPhoto SessionStart ret = %{public}d.", ret);
    }
    ret = TakePicture();
    LOGE( "startPhoto OH_PhotoOutput_Capture ret = %{public}d.", ret);
    if (ret_ != CAMERA_OK) {
        LOGE( "startPhoto failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    takePictureTimes++;
    return ret_;
}

// exposure mode
Camera_ErrorCode NDKCamera::IsExposureModeSupportedFn(uint32_t mode)
{
    LOGE( "IsExposureModeSupportedFn start.");
    exposureMode_ = static_cast<Camera_ExposureMode>(mode);
    ret_ = OH_CaptureSession_IsExposureModeSupported(captureSession_, exposureMode_, &isExposureModeSupported_);
    if (&isExposureModeSupported_ == nullptr || ret_ != CAMERA_OK) {
        LOGE( "IsExposureModeSupported failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    ret_ = OH_CaptureSession_SetExposureMode(captureSession_, exposureMode_);
    if (ret_ != CAMERA_OK) {
        LOGE( "SetExposureMode failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    ret_ = OH_CaptureSession_GetExposureMode(captureSession_, &exposureMode_);
    if (&exposureMode_ == nullptr || ret_ != CAMERA_OK) {
        LOGE( "GetExposureMode failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    LOGE( "IsExposureModeSupportedFn end.");
    return ret_;
}

Camera_ErrorCode NDKCamera::IsMeteringPoint(int x, int y)
{
    LOGE( "IsMeteringPoint start.");
    ret_ = OH_CaptureSession_GetExposureMode(captureSession_, &exposureMode_);
    if (&exposureMode_ == nullptr || ret_ != CAMERA_OK) {
        LOGE( "GetExposureMode failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    Camera_Point exposurePoint;
    exposurePoint.x = x;
    exposurePoint.y = y;
    ret_ = OH_CaptureSession_SetMeteringPoint(captureSession_, exposurePoint);
    if (ret_ != CAMERA_OK) {
        LOGE( "SetMeteringPoint failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    ret_ = OH_CaptureSession_GetMeteringPoint(captureSession_, &exposurePoint);
    if (ret_ != CAMERA_OK) {
        LOGE( "GetMeteringPoint failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    LOGE( "IsMeteringPoint end.");
    return ret_;
}

Camera_ErrorCode NDKCamera::IsExposureBiasRange(int exposureBias)
{
    LOGE( "IsExposureBiasRange end.");
    float exposureBiasValue = static_cast<float>(exposureBias);
    ret_ = OH_CaptureSession_GetExposureBiasRange(captureSession_, &minExposureBias_, &maxExposureBias_, &step_);
    if (&minExposureBias_ == nullptr || &maxExposureBias_ == nullptr || &step_ == nullptr || ret_ != CAMERA_OK) {
        LOGE( "GetExposureBiasRange failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    ret_ = OH_CaptureSession_SetExposureBias(captureSession_, exposureBiasValue);
    LOGE( "OH_CaptureSession_SetExposureBias end.");
    if (ret_ != CAMERA_OK) {
        LOGE( "SetExposureBias failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    ret_ = OH_CaptureSession_GetExposureBias(captureSession_, &exposureBiasValue);
    if (&exposureBiasValue == nullptr || ret_ != CAMERA_OK) {
        LOGE( "GetExposureBias failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    LOGE( "IsExposureBiasRange end.");
    return ret_;
}

// focus mode
Camera_ErrorCode NDKCamera::IsFocusModeSupported(uint32_t mode)
{
    Camera_FocusMode focusMode = static_cast<Camera_FocusMode>(mode);
    ret_ = OH_CaptureSession_IsFocusModeSupported(captureSession_, focusMode, &isFocusModeSupported_);
    if (&isFocusModeSupported_ == nullptr || ret_ != CAMERA_OK) {
        LOGE( "IsFocusModeSupported failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return ret_;
}

Camera_ErrorCode NDKCamera::IsFocusMode(uint32_t mode)
{
    LOGE( "IsFocusMode start.");
    Camera_FocusMode focusMode = static_cast<Camera_FocusMode>(mode);
    ret_ = OH_CaptureSession_IsFocusModeSupported(captureSession_, focusMode, &isFocusModeSupported_);
    if (&isFocusModeSupported_ == nullptr || ret_ != CAMERA_OK) {
        LOGE( "IsFocusModeSupported failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    ret_ = OH_CaptureSession_SetFocusMode(captureSession_, focusMode);
    if (ret_ != CAMERA_OK) {
        LOGE( "SetFocusMode failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    ret_ = OH_CaptureSession_GetFocusMode(captureSession_, &focusMode);
    if (&focusMode == nullptr || ret_ != CAMERA_OK) {
        LOGE( "GetFocusMode failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    LOGE( "IsFocusMode end.");
    return ret_;
}

Camera_ErrorCode NDKCamera::IsFocusPoint(float x, float y)
{
    LOGE( "IsFocusPoint start.");
    Camera_Point focusPoint;
    focusPoint.x = x;
    focusPoint.y = y;
    ret_ = OH_CaptureSession_SetFocusPoint(captureSession_, focusPoint);
    if (ret_ != CAMERA_OK) {
        LOGE( "SetFocusPoint failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    ret_ = OH_CaptureSession_GetFocusPoint(captureSession_, &focusPoint);
    if (&focusPoint == nullptr || ret_ != CAMERA_OK) {
        LOGE( "GetFocusPoint failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    LOGE( "IsFocusPoint end.");
    return ret_;
}

int32_t NDKCamera::GetVideoFrameWidth(void)
{
    videoProfile_ = cameraOutputCapability_->videoProfiles[0];
    if (videoProfile_ == nullptr) {
        LOGE( "Get videoProfiles failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return videoProfile_->size.width;
}

int32_t NDKCamera::GetVideoFrameHeight(void)
{
    videoProfile_ = cameraOutputCapability_->videoProfiles[0];
    if (videoProfile_ == nullptr) {
        LOGE( "Get videoProfiles failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return videoProfile_->size.height;
}

int32_t NDKCamera::GetVideoFrameRate(void)
{
    videoProfile_ = cameraOutputCapability_->videoProfiles[0];
    if (videoProfile_ == nullptr) {
        LOGE( "Get videoProfiles failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return videoProfile_->range.min;
}

Camera_ErrorCode NDKCamera::VideoOutputStop(void)
{
    LOGE( "enter VideoOutputStop.");
    ret_ = OH_VideoOutput_Stop(videoOutput_);
    if (ret_ != CAMERA_OK) {
        LOGE( "VideoOutputStop failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return ret_;
}

Camera_ErrorCode NDKCamera::VideoOutputRelease(void)
{
    LOGE( "enter VideoOutputRelease.");
    ret_ = OH_VideoOutput_Release(videoOutput_);
    if (ret_ != CAMERA_OK) {
        LOGE( "VideoOutputRelease failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return ret_;
}

Camera_ErrorCode NDKCamera::TakePicture(void)
{
    Camera_ErrorCode ret = CAMERA_OK;
    ret = OH_PhotoOutput_Capture(photoOutput_);
    LOGE( "takePicture OH_PhotoOutput_Capture ret = %{public}d.", ret);
    if (ret != CAMERA_OK) {
        LOGE( "startPhoto failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return ret;
}

Camera_ErrorCode NDKCamera::TakePictureWithPhotoSettings(Camera_PhotoCaptureSetting photoSetting)
{
    Camera_ErrorCode ret = CAMERA_OK;
    ret = OH_PhotoOutput_Capture_WithCaptureSetting(photoOutput_, photoSetting);

    OH_LOG_INFO(LOG_APP,
                "TakePictureWithPhotoSettings get quality %{public}d, rotation %{public}d, mirror %{public}d, "
                "latitude, %{public}d, longitude %{public}d, altitude %{public}d",
                photoSetting.quality, photoSetting.rotation, photoSetting.mirror, photoSetting.location->latitude,
                photoSetting.location->longitude, photoSetting.location->altitude);

    LOGE( "takePicture TakePictureWithPhotoSettings ret = %{public}d.", ret);
    if (ret != CAMERA_OK) {
        LOGE( "startPhoto failed.");
        return CAMERA_INVALID_ARGUMENT;
    }
    return ret;
}

// CameraManager Callback
void CameraManagerStatusCallback(Camera_Manager *cameraManager, Camera_StatusInfo *status)
{
    LOGE( "CameraManagerStatusCallback");
}

CameraManager_Callbacks *NDKCamera::GetCameraManagerListener(void)
{
    static CameraManager_Callbacks cameraManagerListener = {.onCameraStatus = CameraManagerStatusCallback};
    return &cameraManagerListener;
}

Camera_ErrorCode NDKCamera::CameraManagerRegisterCallback(void)
{
    ret_ = OH_CameraManager_RegisterCallback(cameraManager_, GetCameraManagerListener());
    if (ret_ != CAMERA_OK) {
        LOGE( "OH_CameraManager_RegisterCallback failed.");
    }
    return ret_;
}

// CameraInput Callback
void OnCameraInputError(const Camera_Input *cameraInput, Camera_ErrorCode errorCode)
{
    LOGE( "OnCameraInput errorCode = %{public}d", errorCode);
}

CameraInput_Callbacks *NDKCamera::GetCameraInputListener(void)
{
    static CameraInput_Callbacks cameraInputCallbacks = {.onError = OnCameraInputError};
    return &cameraInputCallbacks;
}

Camera_ErrorCode NDKCamera::CameraInputRegisterCallback(void)
{
    ret_ = OH_CameraInput_RegisterCallback(cameraInput_, GetCameraInputListener());
    if (ret_ != CAMERA_OK) {
        LOGE( "OH_CameraInput_RegisterCallback failed.");
    }
    return ret_;
}

// PreviewOutput Callback
void PreviewOutputOnFrameStart(Camera_PreviewOutput *previewOutput)
{
    LOGE( "PreviewOutputOnFrameStart");
}

void PreviewOutputOnFrameEnd(Camera_PreviewOutput *previewOutput, int32_t frameCount)
{
    LOGE( "PreviewOutput frameCount = %{public}d", frameCount);
}

void PreviewOutputOnError(Camera_PreviewOutput *previewOutput, Camera_ErrorCode errorCode)
{
    LOGE( "PreviewOutput errorCode = %{public}d", errorCode);
}

PreviewOutput_Callbacks *NDKCamera::GetPreviewOutputListener(void)
{
    static PreviewOutput_Callbacks previewOutputListener = {.onFrameStart = PreviewOutputOnFrameStart,
                                                            .onFrameEnd = PreviewOutputOnFrameEnd,
                                                            .onError = PreviewOutputOnError};
    return &previewOutputListener;
}

Camera_ErrorCode NDKCamera::PreviewOutputRegisterCallback(void)
{
    ret_ = OH_PreviewOutput_RegisterCallback(previewOutput_, GetPreviewOutputListener());
    if (ret_ != CAMERA_OK) {
        LOGE( "OH_PreviewOutput_RegisterCallback failed.");
    }
    return ret_;
}

// PhotoOutput Callback
void PhotoOutputOnFrameStart(Camera_PhotoOutput *photoOutput)
{
    LOGE( "PhotoOutputOnFrameStart");
}

void PhotoOutputOnFrameShutter(Camera_PhotoOutput *photoOutput, Camera_FrameShutterInfo *info)
{
    LOGE( "PhotoOutputOnFrameShutter");
}

void PhotoOutputOnFrameEnd(Camera_PhotoOutput *photoOutput, int32_t frameCount)
{
    LOGE( "PhotoOutput frameCount = %{public}d", frameCount);
}

void PhotoOutputOnError(Camera_PhotoOutput *photoOutput, Camera_ErrorCode errorCode)
{
    LOGE( "PhotoOutput errorCode = %{public}d", errorCode);
}

PhotoOutput_Callbacks *NDKCamera::GetPhotoOutputListener(void)
{
    static PhotoOutput_Callbacks photoOutputListener = {.onFrameStart = PhotoOutputOnFrameStart,
                                                        .onFrameShutter = PhotoOutputOnFrameShutter,
                                                        .onFrameEnd = PhotoOutputOnFrameEnd,
                                                        .onError = PhotoOutputOnError};
    return &photoOutputListener;
}

Camera_ErrorCode NDKCamera::PhotoOutputRegisterCallback(void)
{
    ret_ = OH_PhotoOutput_RegisterCallback(photoOutput_, GetPhotoOutputListener());
    if (ret_ != CAMERA_OK) {
        LOGE( "OH_PhotoOutput_RegisterCallback failed.");
    }
    return ret_;
}

// VideoOutput Callback
void VideoOutputOnFrameStart(Camera_VideoOutput *videoOutput)
{
    LOGE( "VideoOutputOnFrameStart");
}

void VideoOutputOnFrameEnd(Camera_VideoOutput *videoOutput, int32_t frameCount)
{
    LOGE( "VideoOutput frameCount = %{public}d", frameCount);
}

void VideoOutputOnError(Camera_VideoOutput *videoOutput, Camera_ErrorCode errorCode)
{
    LOGE( "VideoOutput errorCode = %{public}d", errorCode);
}

VideoOutput_Callbacks *NDKCamera::GetVideoOutputListener(void)
{
    static VideoOutput_Callbacks videoOutputListener = {
        .onFrameStart = VideoOutputOnFrameStart, .onFrameEnd = VideoOutputOnFrameEnd, .onError = VideoOutputOnError};
    return &videoOutputListener;
}

Camera_ErrorCode NDKCamera::VideoOutputRegisterCallback(void)
{
    ret_ = OH_VideoOutput_RegisterCallback(videoOutput_, GetVideoOutputListener());
    if (ret_ != CAMERA_OK) {
        LOGE( "OH_VideoOutput_RegisterCallback failed.");
    }
    return ret_;
}

// Metadata Callback
void OnMetadataObjectAvailable(Camera_MetadataOutput *metadataOutput, Camera_MetadataObject *metadataObject,
                               uint32_t size)
{
    LOGE( "size = %{public}d", size);
}

void OnMetadataOutputError(Camera_MetadataOutput *metadataOutput, Camera_ErrorCode errorCode)
{
    LOGE( "OnMetadataOutput errorCode = %{public}d", errorCode);
}

MetadataOutput_Callbacks *NDKCamera::GetMetadataOutputListener(void)
{
    static MetadataOutput_Callbacks metadataOutputListener = {.onMetadataObjectAvailable = OnMetadataObjectAvailable,
                                                              .onError = OnMetadataOutputError};
    return &metadataOutputListener;
}

Camera_ErrorCode NDKCamera::MetadataOutputRegisterCallback(void)
{
    ret_ = OH_MetadataOutput_RegisterCallback(metadataOutput_, GetMetadataOutputListener());
    if (ret_ != CAMERA_OK) {
        LOGE( "OH_MetadataOutput_RegisterCallback failed.");
    }
    return ret_;
}

// Session Callback
void CaptureSessionOnFocusStateChange(Camera_CaptureSession *session, Camera_FocusState focusState)
{
    LOGE( "CaptureSessionOnFocusStateChange");
}

void CaptureSessionOnError(Camera_CaptureSession *session, Camera_ErrorCode errorCode)
{
    LOGE( "CaptureSession errorCode = %{public}d", errorCode);
}

CaptureSession_Callbacks *NDKCamera::GetCaptureSessionRegister(void)
{
    static CaptureSession_Callbacks captureSessionCallbacks = {.onFocusStateChange = CaptureSessionOnFocusStateChange,
                                                               .onError = CaptureSessionOnError};
    return &captureSessionCallbacks;
}

Camera_ErrorCode NDKCamera::CaptureSessionRegisterCallback(void)
{
    ret_ = OH_CaptureSession_RegisterCallback(captureSession_, GetCaptureSessionRegister());
    if (ret_ != CAMERA_OK) {
        LOGE( "OH_CaptureSession_RegisterCallback failed.");
    }
    return ret_;
}
}  // namespace OHOS_CAMERA_SAMPLE
// #include "camera_manager.h"
//
// #include "opengl_manager.h"
// #include "util/DebugLog.h"
//
// namespace OHOS_CAMERA_SAMPLE {
// NDKCamera *NDKCamera::ndkCamera_ = nullptr;
// std::mutex NDKCamera::mtx_;
//
// NDKCamera::NDKCamera(char *str, uint32_t focusMode, uint32_t cameraDeviceIndex)
//     : previewSurfaceId_(str),
//       cameras_(nullptr),
//       focusMode_(focusMode),
//       cameraDeviceIndex_(cameraDeviceIndex),
//       cameraOutputCapability_(nullptr),
//       cameraInput_(nullptr),
//       captureSession_(nullptr),
//       size_(0),
//       isCameraMuted_(nullptr),
//       profile_(nullptr),
//       photoSurfaceId_(nullptr),
//       previewOutput_(nullptr),
//       photoOutput_(nullptr),
//       metaDataObjectType_(nullptr),
//       metadataOutput_(nullptr),
//       isExposureModeSupported_(false),
//       isFocusModeSupported_(false),
//       exposureMode_(EXPOSURE_MODE_LOCKED),
//       minExposureBias_(0),
//       maxExposureBias_(0),
//       step_(0),
//       ret_(CAMERA_OK)
// {
//     // 获取 OpenGLManager 提供的输入 Surface ID
//     std::string inputSurfaceId = OpenGLManager::GetInstance().GetInputSurfaceId();
//     if (inputSurfaceId.empty()) {
//         LOGE( "OpenGLManager not ready, camera init failed");
//         return;
//     }
//     // 将 previewSurfaceId_ 设为该 ID
//     previewSurfaceId_ = strdup(inputSurfaceId.c_str());
//     valid_ = false;
//     ReleaseCamera();
//     Camera_ErrorCode ret = OH_Camera_GetCameraManager(&cameraManager_);  // 创建manager对象
//     if (cameraManager_ == nullptr || ret != CAMERA_OK) {
//         LOGE( "Get CameraManager failed.");
//     }
//
//     ret = OH_CameraManager_CreateCaptureSession(cameraManager_, &captureSession_);
//     if (captureSession_ == nullptr || ret != CAMERA_OK) {
//         LOGE( "Create captureSession failed.");
//     }
//     CaptureSessionRegisterCallback();
//     GetSupportedCameras();
//     GetSupportedOutputCapability();
//     CreatePreviewOutput();
//     CreateCameraInput();
//     CameraInputOpen();
//     CameraManagerRegisterCallback();
//     SessionFlowFn();
//     valid_ = true;
// }  // 设备输入设置
//
// NDKCamera::~NDKCamera()
// {
//     valid_ = false;
//     LOGE( "~NDKCamera");
//     Camera_ErrorCode ret = CAMERA_OK;
//
//     if (cameraManager_) {
//         LOGE( "Release OH_CameraManager_DeleteSupportedCameraOutputCapability. enter");
//         ret = OH_CameraManager_DeleteSupportedCameraOutputCapability(cameraManager_, cameraOutputCapability_);
//         if (ret != CAMERA_OK) {
//             LOGE( "Delete CameraOutputCapability failed.");
//         } else {
//             LOGE( "Release OH_CameraManager_DeleteSupportedCameraOutputCapability. ok");
//         }
//
//         LOGE( "Release OH_CameraManager_DeleteSupportedCameras. enter");
//         ret = OH_CameraManager_DeleteSupportedCameras(cameraManager_, cameras_, size_);
//         if (ret != CAMERA_OK) {
//             LOGE( "Delete Cameras failed.");
//         } else {
//             LOGE( "Release OH_CameraManager_DeleteSupportedCameras. ok");
//         }
//
//         ret = OH_Camera_DeleteCameraManager(cameraManager_);
//         if (ret != CAMERA_OK) {
//             LOGE( "Delete CameraManager failed.");
//         } else {
//             LOGE( "Release OH_Camera_DeleteCameraMananger. ok");
//         }
//         cameraManager_ = nullptr;
//     }
//     LOGE( "~NDKCamera exit");
// }
//
// Camera_ErrorCode NDKCamera::ReleaseCamera(void)
// {
//     LOGE( " enter ReleaseCamera");
//     if (previewOutput_) {
//         PreviewOutputStop();
//         PreviewOutputRelease();
//         OH_CaptureSession_RemovePreviewOutput(captureSession_, previewOutput_);
//     }
//     if (photoOutput_) {
//         PhotoOutputRelease();
//     }
//     if (captureSession_) {
//         SessionRealese();
//     }
//     if (cameraInput_) {
//         CameraInputClose();
//     }
//     LOGE( " exit ReleaseCamera");
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::ReleaseSession(void)
// {
//     LOGE( " enter ReleaseSession");
//     PreviewOutputStop();
//     PhotoOutputRelease();
//     SessionRealese();
//     LOGE( " exit ReleaseSession");
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::SessionRealese(void)
// {
//     LOGE( " enter SessionRealese");
//     Camera_ErrorCode ret = OH_CaptureSession_Release(captureSession_);
//     captureSession_ = nullptr;
//     LOGE( " exit SessionRealese");
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::HasFlashFn(uint32_t mode)
// {
//     Camera_FlashMode flashMode = static_cast<Camera_FlashMode>(mode);
//     // Check for flashing lights
//     bool hasFlash = false;
//     Camera_ErrorCode ret = OH_CaptureSession_HasFlash(captureSession_, &hasFlash);
//     if (captureSession_ == nullptr || ret != CAMERA_OK) {
//         LOGE( "OH_CaptureSession_HasFlash failed.");
//     }
//     if (hasFlash) {
//         LOGE( "hasFlash success-----");
//     } else {
//         LOGE( "hasFlash fail-----");
//     }
//
//     // Check if the flash mode is supported
//     bool isSupported = false;
//     ret = OH_CaptureSession_IsFlashModeSupported(captureSession_, flashMode, &isSupported);
//     if (ret != CAMERA_OK) {
//         LOGE( "OH_CaptureSession_IsFlashModeSupported failed.");
//     }
//     if (isSupported) {
//         LOGE( "isFlashModeSupported success-----");
//     } else {
//         LOGE( "isFlashModeSupported fail-----");
//     }
//
//     // Set flash mode
//     ret = OH_CaptureSession_SetFlashMode(captureSession_, flashMode);
//     if (ret == CAMERA_OK) {
//         LOGE( "OH_CaptureSession_SetFlashMode success.");
//     } else {
//         LOGE( "OH_CaptureSession_SetFlashMode failed. %{public}d ", ret);
//     }
//
//     // Obtain the flash mode of the current device
//     ret = OH_CaptureSession_GetFlashMode(captureSession_, &flashMode);
//     if (ret == CAMERA_OK) {
//         LOGE( "OH_CaptureSession_GetFlashMode success. flashMode：%{public}d ", flashMode);
//     } else {
//         LOGE( "OH_CaptureSession_GetFlashMode failed. %d ", ret);
//     }
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::IsVideoStabilizationModeSupportedFn(uint32_t mode)
// {
//     Camera_VideoStabilizationMode videoMode = static_cast<Camera_VideoStabilizationMode>(mode);
//     // Check if the specified video anti shake mode is supported
//     bool isSupported = false;
//     Camera_ErrorCode ret =
//         OH_CaptureSession_IsVideoStabilizationModeSupported(captureSession_, videoMode, &isSupported);
//     if (ret != CAMERA_OK) {
//         LOGE( "OH_CaptureSession_IsVideoStabilizationModeSupported failed.");
//     }
//     if (isSupported) {
//         LOGE( "OH_CaptureSession_IsVideoStabilizationModeSupported success-----");
//     } else {
//         LOGE( "OH_CaptureSession_IsVideoStabilizationModeSupported fail-----");
//     }
//
//     // Set video stabilization
//     ret = OH_CaptureSession_SetVideoStabilizationMode(captureSession_, videoMode);
//     if (ret == CAMERA_OK) {
//         LOGE( "OH_CaptureSession_SetVideoStabilizationMode success.");
//     } else {
//         LOGE( "OH_CaptureSession_SetVideoStabilizationMode failed. %{public}d ", ret);
//     }
//
//     ret = OH_CaptureSession_GetVideoStabilizationMode(captureSession_, &videoMode);
//     if (ret == CAMERA_OK) {
//         LOGE( "OH_CaptureSession_GetVideoStabilizationMode success. videoMode：%u ", videoMode);
//     } else {
//         LOGE( "OH_CaptureSession_GetVideoStabilizationMode failed. %{public}d ", ret);
//     }
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::setZoomRatioFn(uint32_t zoomRatio)
// {
//     float zoom = float(zoomRatio);
//     // Obtain supported zoom range
//     float minZoom;
//     float maxZoom;
//     Camera_ErrorCode ret = OH_CaptureSession_GetZoomRatioRange(captureSession_, &minZoom, &maxZoom);
//     if (captureSession_ == nullptr || ret != CAMERA_OK) {
//         LOGE( "OH_CaptureSession_GetZoomRatioRange failed.");
//     } else {
//         LOGE( "OH_CaptureSession_GetZoomRatioRange success. minZoom: %{public}f, maxZoom:%{public}f",
//                     minZoom, maxZoom);
//     }
//
//     // Set Zoom
//     ret = OH_CaptureSession_SetZoomRatio(captureSession_, zoom);
//     if (ret == CAMERA_OK) {
//         LOGE( "OH_CaptureSession_SetZoomRatio success.");
//     } else {
//         LOGE( "OH_CaptureSession_SetZoomRatio failed. %{public}d ", ret);
//     }
//
//     // Obtain the zoom value of the current device
//     ret = OH_CaptureSession_GetZoomRatio(captureSession_, &zoom);
//     if (ret == CAMERA_OK) {
//         LOGE( "OH_CaptureSession_GetZoomRatio success. zoom：%{public}f ", zoom);
//     } else {
//         LOGE( "OH_CaptureSession_GetZoomRatio failed. %{public}d ", ret);
//     }
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::SessionBegin(void)
// {
//     Camera_ErrorCode ret = OH_CaptureSession_BeginConfig(captureSession_);
//     if (ret == CAMERA_OK) {
//         LOGE( "OH_CaptureSession_BeginConfig success.");
//     } else {
//         LOGE( "OH_CaptureSession_BeginConfig failed. %d ", ret);
//     }
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::SessionCommitConfig(void)
// {
//     Camera_ErrorCode ret = OH_CaptureSession_CommitConfig(captureSession_);
//     if (ret == CAMERA_OK) {
//         LOGE( "OH_CaptureSession_CommitConfig success.");
//     } else {
//         LOGE( "OH_CaptureSession_CommitConfig failed. %d ", ret);
//     }
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::SessionStart(void)
// {
//     Camera_ErrorCode ret = OH_CaptureSession_Start(captureSession_);
//     if (ret == CAMERA_OK) {
//         LOGE( "OH_CaptureSession_Start success.");
//     } else {
//         LOGE( "OH_CaptureSession_Start failed. %d ", ret);
//     }
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::SessionStop(void)
// {
//     Camera_ErrorCode ret = OH_CaptureSession_Stop(captureSession_);
//     if (ret == CAMERA_OK) {
//         LOGE( "OH_CaptureSession_Stop success.");
//     } else {
//         LOGE( "OH_CaptureSession_Stop failed. %d ", ret);
//     }
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::SessionFlowFn(void)
// {
//     LOGE( "Start SessionFlowFn IN.");
//     // Start configuring session
//     LOGE( "session beginConfig.");
//     Camera_ErrorCode ret = OH_CaptureSession_BeginConfig(captureSession_);
//
//     // Add CameraInput to the session
//     LOGE( "session addInput.");
//     ret = OH_CaptureSession_AddInput(captureSession_, cameraInput_);
//
//     // Add previewOutput to the session
//     LOGE( "session add Preview Output.");
//     ret = OH_CaptureSession_AddPreviewOutput(captureSession_, previewOutput_);
//
//     // Adding PhotoOutput to the Session
//     LOGE( "session add Photo Output.");
//
//     // Submit configuration information
//     LOGE( "session commitConfig");
//     ret = OH_CaptureSession_CommitConfig(captureSession_);
//
//     // Start Session Work
//     LOGE( "session start");
//     ret = OH_CaptureSession_Start(captureSession_);
//     LOGE( "session success");
//
//     // Start focusing
//     LOGE( "IsFocusMode start");
//     ret = IsFocusMode(focusMode_);
//     LOGE( "IsFocusMode success");
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::CreateCameraInput(void)
// {
//     LOGE( "enter CreateCameraInput.");
//     ret_ = OH_CameraManager_CreateCameraInput(cameraManager_, &cameras_[cameraDeviceIndex_], &cameraInput_);
//     if (cameraInput_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "CreateCameraInput failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     LOGE( "exit CreateCameraInput.");
//     CameraInputRegisterCallback();
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::CameraInputOpen(void)
// {
//     LOGE( "enter CameraInputOpen.");
//     ret_ = OH_CameraInput_Open(cameraInput_);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "CameraInput_Open failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     LOGE( "exit CameraInputOpen.");
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::CameraInputClose(void)
// {
//     LOGE( "enter CameraInput_Close.");
//     ret_ = OH_CameraInput_Close(cameraInput_);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "CameraInput_Close failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     LOGE( "exit CameraInput_Close.");
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::CameraInputRelease(void)
// {
//     LOGE( "enter CameraInputRelease.");
//     ret_ = OH_CameraInput_Release(cameraInput_);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "CameraInput_Release failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     LOGE( "exit CameraInputRelease.");
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::GetSupportedCameras(void)
// {
//     ret_ = OH_CameraManager_GetSupportedCameras(cameraManager_, &cameras_, &size_);
//     if (cameras_ == nullptr || &size_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "Get supported cameras failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::GetSupportedOutputCapability(void)
// {
//     ret_ = OH_CameraManager_GetSupportedCameraOutputCapability(cameraManager_, &cameras_[cameraDeviceIndex_],
//                                                                &cameraOutputCapability_);
//     if (cameraOutputCapability_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "GetSupportedCameraOutputCapability failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::CreatePreviewOutput(void)
// {
//     profile_ = cameraOutputCapability_->previewProfiles[0];
//     if (profile_ == nullptr) {
//         LOGE( "Get previewProfiles failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     ret_ = OH_CameraManager_CreatePreviewOutput(cameraManager_, profile_, previewSurfaceId_, &previewOutput_);
//     if (previewSurfaceId_ == nullptr || previewOutput_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "CreatePreviewOutput failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return ret_;
//     PreviewOutputRegisterCallback();
// }
//
// Camera_ErrorCode NDKCamera::CreatePhotoOutput(char *photoSurfaceId)
// {
//     profile_ = cameraOutputCapability_->photoProfiles[0];
//     if (profile_ == nullptr) {
//         LOGE( "Get photoProfiles failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//
//     if (photoSurfaceId == nullptr) {
//         LOGE( "CreatePhotoOutput failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//
//     ret_ = OH_CameraManager_CreatePhotoOutput(cameraManager_, profile_, photoSurfaceId, &photoOutput_);
//     PhotoOutputRegisterCallback();
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::CreateVideoOutput(char *videoId)
// {
//     videoProfile_ = cameraOutputCapability_->videoProfiles[0];
//
//     if (videoProfile_ == nullptr) {
//         LOGE( "Get videoProfiles failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     ret_ = OH_CameraManager_CreateVideoOutput(cameraManager_, videoProfile_, videoId, &videoOutput_);
//     if (videoId == nullptr || videoOutput_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "CreateVideoOutput failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::AddVideoOutput(void)
// {
//     Camera_ErrorCode ret = OH_CaptureSession_AddVideoOutput(captureSession_, videoOutput_);
//     if (ret == CAMERA_OK) {
//         LOGE( "OH_CaptureSession_AddVideoOutput success.");
//     } else {
//         LOGE( "OH_CaptureSession_AddVideoOutput failed. %d ", ret);
//     }
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::AddPhotoOutput()
// {
//     Camera_ErrorCode ret = OH_CaptureSession_AddPhotoOutput(captureSession_, photoOutput_);
//     if (ret == CAMERA_OK) {
//         LOGE( "OH_CaptureSession_AddPhotoOutput success.");
//     } else {
//         LOGE( "OH_CaptureSession_AddPhotoOutput failed. %d ", ret);
//     }
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::CreateMetadataOutput(void)
// {
//     metaDataObjectType_ = cameraOutputCapability_->supportedMetadataObjectTypes[0];
//     if (metaDataObjectType_ == nullptr) {
//         LOGE( "Get metaDataObjectType failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     ret_ = OH_CameraManager_CreateMetadataOutput(cameraManager_, metaDataObjectType_, &metadataOutput_);
//     if (metadataOutput_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "CreateMetadataOutput failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     MetadataOutputRegisterCallback();
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::IsCameraMuted(void)
// {
//     ret_ = OH_CameraManager_IsCameraMuted(cameraManager_, isCameraMuted_);
//     if (isCameraMuted_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "IsCameraMuted failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::PreviewOutputStop(void)
// {
//     LOGE( "enter PreviewOutputStop.");
//     ret_ = OH_PreviewOutput_Stop(previewOutput_);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "PreviewOutputStop failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::PreviewOutputRelease(void)
// {
//     LOGE( "enter PreviewOutputRelease.");
//     ret_ = OH_PreviewOutput_Release(previewOutput_);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "PreviewOutputRelease failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::PhotoOutputRelease(void)
// {
//     LOGE( "enter PhotoOutputRelease.");
//     ret_ = OH_PhotoOutput_Release(photoOutput_);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "PhotoOutputRelease failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::StartVideo(char *videoId, char *photoId)
// {
//     LOGE( "StartVideo begin.");
//     Camera_ErrorCode ret = SessionStop();
//     if (ret == CAMERA_OK) {
//         LOGE( "SessionStop success.");
//     } else {
//         LOGE( "SessionStop failed. %d ", ret);
//     }
//     ret = SessionBegin();
//     if (ret == CAMERA_OK) {
//         LOGE( "SessionBegin success.");
//     } else {
//         LOGE( "SessionBegin failed. %d ", ret);
//     }
//     OH_CaptureSession_RemovePhotoOutput(captureSession_, photoOutput_);
//     CreatePhotoOutput(photoId);
//     AddPhotoOutput();
//     CreateVideoOutput(videoId);
//     AddVideoOutput();
//     SessionCommitConfig();
//     SessionStart();
//     VideoOutputRegisterCallback();
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::VideoOutputStart(void)
// {
//     LOGE( "VideoOutputStart begin.");
//     Camera_ErrorCode ret = OH_VideoOutput_Start(videoOutput_);
//     if (ret == CAMERA_OK) {
//         LOGE( "OH_VideoOutput_Start success.");
//     } else {
//         LOGE( "OH_VideoOutput_Start failed. %d ", ret);
//     }
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::StartPhoto(char *mSurfaceId)
// {
//     Camera_ErrorCode ret = CAMERA_OK;
//     if (takePictureTimes == 0) {
//         ret = SessionStop();
//         if (ret == CAMERA_OK) {
//             LOGE( "SessionStop success.");
//         } else {
//             LOGE( "SessionStop failed. %d ", ret);
//         }
//         ret = SessionBegin();
//         if (ret == CAMERA_OK) {
//             LOGE( "SessionBegin success.");
//         } else {
//             LOGE( "SessionBegin failed. %d ", ret);
//         }
//         LOGE( "startPhoto begin.");
//         ret = CreatePhotoOutput(mSurfaceId);
//
//         LOGE( "startPhoto CreatePhotoOutput ret = %{public}d.", ret);
//         ret = OH_CaptureSession_AddPhotoOutput(captureSession_, photoOutput_);
//         LOGE( "startPhoto AddPhotoOutput ret = %{public}d.", ret);
//         ret = SessionCommitConfig();
//
//         LOGE( "startPhoto SessionCommitConfig ret = %{public}d.", ret);
//         ret = SessionStart();
//         LOGE( "startPhoto SessionStart ret = %{public}d.", ret);
//     }
//     ret = TakePicture();
//     LOGE( "startPhoto OH_PhotoOutput_Capture ret = %{public}d.", ret);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "startPhoto failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     takePictureTimes++;
//     return ret_;
// }
//
// // exposure mode
// Camera_ErrorCode NDKCamera::IsExposureModeSupportedFn(uint32_t mode)
// {
//     LOGE( "IsExposureModeSupportedFn start.");
//     exposureMode_ = static_cast<Camera_ExposureMode>(mode);
//     ret_ = OH_CaptureSession_IsExposureModeSupported(captureSession_, exposureMode_, &isExposureModeSupported_);
//     if (&isExposureModeSupported_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "IsExposureModeSupported failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     ret_ = OH_CaptureSession_SetExposureMode(captureSession_, exposureMode_);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "SetExposureMode failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     ret_ = OH_CaptureSession_GetExposureMode(captureSession_, &exposureMode_);
//     if (&exposureMode_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "GetExposureMode failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     LOGE( "IsExposureModeSupportedFn end.");
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::IsMeteringPoint(int x, int y)
// {
//     LOGE( "IsMeteringPoint start.");
//     ret_ = OH_CaptureSession_GetExposureMode(captureSession_, &exposureMode_);
//     if (&exposureMode_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "GetExposureMode failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     Camera_Point exposurePoint;
//     exposurePoint.x = x;
//     exposurePoint.y = y;
//     ret_ = OH_CaptureSession_SetMeteringPoint(captureSession_, exposurePoint);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "SetMeteringPoint failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     ret_ = OH_CaptureSession_GetMeteringPoint(captureSession_, &exposurePoint);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "GetMeteringPoint failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     LOGE( "IsMeteringPoint end.");
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::IsExposureBiasRange(int exposureBias)
// {
//     LOGE( "IsExposureBiasRange end.");
//     float exposureBiasValue = static_cast<float>(exposureBias);
//     ret_ = OH_CaptureSession_GetExposureBiasRange(captureSession_, &minExposureBias_, &maxExposureBias_, &step_);
//     if (&minExposureBias_ == nullptr || &maxExposureBias_ == nullptr || &step_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "GetExposureBiasRange failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     ret_ = OH_CaptureSession_SetExposureBias(captureSession_, exposureBiasValue);
//     LOGE( "OH_CaptureSession_SetExposureBias end.");
//     if (ret_ != CAMERA_OK) {
//         LOGE( "SetExposureBias failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     ret_ = OH_CaptureSession_GetExposureBias(captureSession_, &exposureBiasValue);
//     if (&exposureBiasValue == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "GetExposureBias failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     LOGE( "IsExposureBiasRange end.");
//     return ret_;
// }
//
// // focus mode
// Camera_ErrorCode NDKCamera::IsFocusModeSupported(uint32_t mode)
// {
//     Camera_FocusMode focusMode = static_cast<Camera_FocusMode>(mode);
//     ret_ = OH_CaptureSession_IsFocusModeSupported(captureSession_, focusMode, &isFocusModeSupported_);
//     if (&isFocusModeSupported_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "IsFocusModeSupported failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::IsFocusMode(uint32_t mode)
// {
//     LOGE( "IsFocusMode start.");
//     Camera_FocusMode focusMode = static_cast<Camera_FocusMode>(mode);
//     ret_ = OH_CaptureSession_IsFocusModeSupported(captureSession_, focusMode, &isFocusModeSupported_);
//     if (&isFocusModeSupported_ == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "IsFocusModeSupported failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     ret_ = OH_CaptureSession_SetFocusMode(captureSession_, focusMode);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "SetFocusMode failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     ret_ = OH_CaptureSession_GetFocusMode(captureSession_, &focusMode);
//     if (&focusMode == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "GetFocusMode failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     LOGE( "IsFocusMode end.");
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::IsFocusPoint(float x, float y)
// {
//     LOGE( "IsFocusPoint start.");
//     Camera_Point focusPoint;
//     focusPoint.x = x;
//     focusPoint.y = y;
//     ret_ = OH_CaptureSession_SetFocusPoint(captureSession_, focusPoint);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "SetFocusPoint failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     ret_ = OH_CaptureSession_GetFocusPoint(captureSession_, &focusPoint);
//     if (&focusPoint == nullptr || ret_ != CAMERA_OK) {
//         LOGE( "GetFocusPoint failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     LOGE( "IsFocusPoint end.");
//     return ret_;
// }
//
// int32_t NDKCamera::GetVideoFrameWidth(void)
// {
//     videoProfile_ = cameraOutputCapability_->videoProfiles[0];
//     if (videoProfile_ == nullptr) {
//         LOGE( "Get videoProfiles failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return videoProfile_->size.width;
// }
//
// int32_t NDKCamera::GetVideoFrameHeight(void)
// {
//     videoProfile_ = cameraOutputCapability_->videoProfiles[0];
//     if (videoProfile_ == nullptr) {
//         LOGE( "Get videoProfiles failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return videoProfile_->size.height;
// }
//
// int32_t NDKCamera::GetVideoFrameRate(void)
// {
//     videoProfile_ = cameraOutputCapability_->videoProfiles[0];
//     if (videoProfile_ == nullptr) {
//         LOGE( "Get videoProfiles failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return videoProfile_->range.min;
// }
//
// Camera_ErrorCode NDKCamera::VideoOutputStop(void)
// {
//     LOGE( "enter VideoOutputStop.");
//     ret_ = OH_VideoOutput_Stop(videoOutput_);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "VideoOutputStop failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::VideoOutputRelease(void)
// {
//     LOGE( "enter VideoOutputRelease.");
//     ret_ = OH_VideoOutput_Release(videoOutput_);
//     if (ret_ != CAMERA_OK) {
//         LOGE( "VideoOutputRelease failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return ret_;
// }
//
// Camera_ErrorCode NDKCamera::TakePicture(void)
// {
//     Camera_ErrorCode ret = CAMERA_OK;
//     ret = OH_PhotoOutput_Capture(photoOutput_);
//     LOGE( "takePicture OH_PhotoOutput_Capture ret = %{public}d.", ret);
//     if (ret != CAMERA_OK) {
//         LOGE( "startPhoto failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return ret;
// }
//
// Camera_ErrorCode NDKCamera::TakePictureWithPhotoSettings(Camera_PhotoCaptureSetting photoSetting)
// {
//     Camera_ErrorCode ret = CAMERA_OK;
//     ret = OH_PhotoOutput_Capture_WithCaptureSetting(photoOutput_, photoSetting);
//
//     OH_LOG_INFO(LOG_APP,
//                 "TakePictureWithPhotoSettings get quality %{public}d, rotation %{public}d, mirror %{public}d, "
//                 "latitude, %{public}d, longitude %{public}d, altitude %{public}d",
//                 photoSetting.quality, photoSetting.rotation, photoSetting.mirror, photoSetting.location->latitude,
//                 photoSetting.location->longitude, photoSetting.location->altitude);
//
//     LOGE( "takePicture TakePictureWithPhotoSettings ret = %{public}d.", ret);
//     if (ret != CAMERA_OK) {
//         LOGE( "startPhoto failed.");
//         return CAMERA_INVALID_ARGUMENT;
//     }
//     return ret;
// }
//
// // CameraManager Callback
// void CameraManagerStatusCallback(Camera_Manager *cameraManager, Camera_StatusInfo *status)
// {
//     LOGE( "CameraManagerStatusCallback");
// }
//
// CameraManager_Callbacks *NDKCamera::GetCameraManagerListener(void)
// {
//     static CameraManager_Callbacks cameraManagerListener = {.onCameraStatus = CameraManagerStatusCallback};
//     return &cameraManagerListener;
// }
//
// Camera_ErrorCode NDKCamera::CameraManagerRegisterCallback(void)
// {
//     ret_ = OH_CameraManager_RegisterCallback(cameraManager_, GetCameraManagerListener());
//     if (ret_ != CAMERA_OK) {
//         LOGE( "OH_CameraManager_RegisterCallback failed.");
//     }
//     return ret_;
// }
//
// // CameraInput Callback
// void OnCameraInputError(const Camera_Input *cameraInput, Camera_ErrorCode errorCode)
// {
//     LOGE( "OnCameraInput errorCode = %{public}d", errorCode);
// }
//
// CameraInput_Callbacks *NDKCamera::GetCameraInputListener(void)
// {
//     static CameraInput_Callbacks cameraInputCallbacks = {.onError = OnCameraInputError};
//     return &cameraInputCallbacks;
// }
//
// Camera_ErrorCode NDKCamera::CameraInputRegisterCallback(void)
// {
//     ret_ = OH_CameraInput_RegisterCallback(cameraInput_, GetCameraInputListener());
//     if (ret_ != CAMERA_OK) {
//         LOGE( "OH_CameraInput_RegisterCallback failed.");
//     }
//     return ret_;
// }
//
// // PreviewOutput Callback
// void PreviewOutputOnFrameStart(Camera_PreviewOutput *previewOutput)
// {
//     LOGE( "PreviewOutputOnFrameStart");
// }
//
// void PreviewOutputOnFrameEnd(Camera_PreviewOutput *previewOutput, int32_t frameCount)
// {
//     LOGE( "PreviewOutput frameCount = %{public}d", frameCount);
// }
//
// void PreviewOutputOnError(Camera_PreviewOutput *previewOutput, Camera_ErrorCode errorCode)
// {
//     LOGE( "PreviewOutput errorCode = %{public}d", errorCode);
// }
//
// PreviewOutput_Callbacks *NDKCamera::GetPreviewOutputListener(void)
// {
//     static PreviewOutput_Callbacks previewOutputListener = {.onFrameStart = PreviewOutputOnFrameStart,
//                                                             .onFrameEnd = PreviewOutputOnFrameEnd,
//                                                             .onError = PreviewOutputOnError};
//     return &previewOutputListener;
// }
//
// Camera_ErrorCode NDKCamera::PreviewOutputRegisterCallback(void)
// {
//     ret_ = OH_PreviewOutput_RegisterCallback(previewOutput_, GetPreviewOutputListener());
//     if (ret_ != CAMERA_OK) {
//         LOGE( "OH_PreviewOutput_RegisterCallback failed.");
//     }
//     return ret_;
// }
//
// // PhotoOutput Callback
// void PhotoOutputOnFrameStart(Camera_PhotoOutput *photoOutput)
// {
//     LOGE( "PhotoOutputOnFrameStart");
// }
//
// void PhotoOutputOnFrameShutter(Camera_PhotoOutput *photoOutput, Camera_FrameShutterInfo *info)
// {
//     LOGE( "PhotoOutputOnFrameShutter");
// }
//
// void PhotoOutputOnFrameEnd(Camera_PhotoOutput *photoOutput, int32_t frameCount)
// {
//     LOGE( "PhotoOutput frameCount = %{public}d", frameCount);
// }
//
// void PhotoOutputOnError(Camera_PhotoOutput *photoOutput, Camera_ErrorCode errorCode)
// {
//     LOGE( "PhotoOutput errorCode = %{public}d", errorCode);
// }
//
// PhotoOutput_Callbacks *NDKCamera::GetPhotoOutputListener(void)
// {
//     static PhotoOutput_Callbacks photoOutputListener = {.onFrameStart = PhotoOutputOnFrameStart,
//                                                         .onFrameShutter = PhotoOutputOnFrameShutter,
//                                                         .onFrameEnd = PhotoOutputOnFrameEnd,
//                                                         .onError = PhotoOutputOnError};
//     return &photoOutputListener;
// }
//
// Camera_ErrorCode NDKCamera::PhotoOutputRegisterCallback(void)
// {
//     ret_ = OH_PhotoOutput_RegisterCallback(photoOutput_, GetPhotoOutputListener());
//     if (ret_ != CAMERA_OK) {
//         LOGE( "OH_PhotoOutput_RegisterCallback failed.");
//     }
//     return ret_;
// }
//
// // VideoOutput Callback
// void VideoOutputOnFrameStart(Camera_VideoOutput *videoOutput)
// {
//     LOGE( "VideoOutputOnFrameStart");
// }
//
// void VideoOutputOnFrameEnd(Camera_VideoOutput *videoOutput, int32_t frameCount)
// {
//     LOGE( "VideoOutput frameCount = %{public}d", frameCount);
// }
//
// void VideoOutputOnError(Camera_VideoOutput *videoOutput, Camera_ErrorCode errorCode)
// {
//     LOGE( "VideoOutput errorCode = %{public}d", errorCode);
// }
//
// VideoOutput_Callbacks *NDKCamera::GetVideoOutputListener(void)
// {
//     static VideoOutput_Callbacks videoOutputListener = {
//         .onFrameStart = VideoOutputOnFrameStart, .onFrameEnd = VideoOutputOnFrameEnd, .onError = VideoOutputOnError};
//     return &videoOutputListener;
// }
//
// Camera_ErrorCode NDKCamera::VideoOutputRegisterCallback(void)
// {
//     ret_ = OH_VideoOutput_RegisterCallback(videoOutput_, GetVideoOutputListener());
//     if (ret_ != CAMERA_OK) {
//         LOGE( "OH_VideoOutput_RegisterCallback failed.");
//     }
//     return ret_;
// }
//
// // Metadata Callback
// void OnMetadataObjectAvailable(Camera_MetadataOutput *metadataOutput, Camera_MetadataObject *metadataObject,
//                                uint32_t size)
// {
//     LOGE( "size = %{public}d", size);
// }
//
// void OnMetadataOutputError(Camera_MetadataOutput *metadataOutput, Camera_ErrorCode errorCode)
// {
//     LOGE( "OnMetadataOutput errorCode = %{public}d", errorCode);
// }
//
// MetadataOutput_Callbacks *NDKCamera::GetMetadataOutputListener(void)
// {
//     static MetadataOutput_Callbacks metadataOutputListener = {.onMetadataObjectAvailable = OnMetadataObjectAvailable,
//                                                               .onError = OnMetadataOutputError};
//     return &metadataOutputListener;
// }
//
// Camera_ErrorCode NDKCamera::MetadataOutputRegisterCallback(void)
// {
//     ret_ = OH_MetadataOutput_RegisterCallback(metadataOutput_, GetMetadataOutputListener());
//     if (ret_ != CAMERA_OK) {
//         LOGE( "OH_MetadataOutput_RegisterCallback failed.");
//     }
//     return ret_;
// }
//
// // Session Callback
// void CaptureSessionOnFocusStateChange(Camera_CaptureSession *session, Camera_FocusState focusState)
// {
//     LOGE( "CaptureSessionOnFocusStateChange");
// }
//
// void CaptureSessionOnError(Camera_CaptureSession *session, Camera_ErrorCode errorCode)
// {
//     LOGE( "CaptureSession errorCode = %{public}d", errorCode);
// }
//
// CaptureSession_Callbacks *NDKCamera::GetCaptureSessionRegister(void)
// {
//     static CaptureSession_Callbacks captureSessionCallbacks = {.onFocusStateChange = CaptureSessionOnFocusStateChange,
//                                                                .onError = CaptureSessionOnError};
//     return &captureSessionCallbacks;
// }
//
// Camera_ErrorCode NDKCamera::CaptureSessionRegisterCallback(void)
// {
//     ret_ = OH_CaptureSession_RegisterCallback(captureSession_, GetCaptureSessionRegister());
//     if (ret_ != CAMERA_OK) {
//         LOGE( "OH_CaptureSession_RegisterCallback failed.");
//     }
//     return ret_;
// }
// }  // namespace OHOS_CAMERA_SAMPLE