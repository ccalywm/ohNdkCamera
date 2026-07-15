#include <js_native_api.h>
#include "camera_manager.h"
// #include "opengl_manager.h"
#include "opengl/OpenGLManager.h"
#include "video_codec.h"          // 新增：编解码器头文件
#include "util/DebugLog.h"
#include <multimedia/image_framework/image/pixelmap_native.h>
#include <native_buffer/native_buffer.h>   // 提供 BUFFER_USAGE_* 宏
#include <native_window/external_window.h>
#include <unistd.h>

// int32_t renderWidth = 1920; 
// int32_t renderHeight = 1080;
int32_t renderWidth = 1280; 
int32_t renderHeight = 720;
int32_t kbps = 6000000;
int32_t fps = 30;
using namespace OHOS_CAMERA_SAMPLE;
static NDKCamera *ndkCamera_ = nullptr;
const int32_t ARGS_TWO = 2;

// ---------- 新增：编解码器全局变量 ----------
static VideoEncoder* g_videoEncoder = nullptr;
static VideoDecoder* g_videoDecoder = nullptr;
static std::shared_ptr<EncodedDataQueue> g_encodeQueue = nullptr;
static std::thread g_decoderThread;
static std::atomic<bool> g_decoderRunning{false};
static OHNativeWindow* g_decoderOutputWindow = nullptr;

// ---------- 新增：解码线程函数 ----------
static void DecoderLoop() {
    LOGI("DecoderLoop started");
    int count = 0;
    while (g_decoderRunning.load()) {
        auto packet = g_encodeQueue->Pop(50);
        if (!packet) continue;
        if (!g_videoDecoder || !g_videoDecoder->IsRunning()) continue;

        count++;
        LOGI("DL: #%d sz=%zu key=%d cfg=%d",
             count, packet->data.size(), packet->isKeyFrame, packet->isCodecConfig);

        g_videoDecoder->QueuePacket(
            packet->data.data(), packet->data.size(),
            packet->pts, packet->isKeyFrame, packet->isCodecConfig);
    }
    LOGI("DecoderLoop exit, total=%d", count);
}

// ---------- 新增：启动编解码链路（由 ArkTS 调用） ----------
static napi_value StartEncodeDecode(napi_env env, napi_callback_info info) {
    napi_value result = nullptr;
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        LOGE("StartEncodeDecode requires decoder surfaceId");
        napi_create_int32(env, -1, &result);
        return result;
    }

    size_t typeLen = 0;
    napi_get_value_string_utf8(env, args[0], nullptr, 0, &typeLen);
    if (typeLen == 0) {
        LOGE("StartEncodeDecode: empty surfaceId");
        napi_create_int32(env, -1, &result);
        return result;
    }
    char* decoderSurfaceId = new char[typeLen + 1];
    napi_get_value_string_utf8(env, args[0], decoderSurfaceId, typeLen + 1, &typeLen);

    uint64_t surfId = std::stoull(decoderSurfaceId);
    OHNativeWindow* decoderWindow = nullptr;
    int ret = OH_NativeWindow_CreateNativeWindowFromSurfaceId(surfId, &decoderWindow);
    delete[] decoderSurfaceId;

    // 重试机制
    int retryCount = 0;
    while ((ret != 0 || decoderWindow == nullptr) && retryCount < 5) {
        LOGE("StartEncodeDecode: create decoder window failed, ret=%d, retry %d", ret, retryCount);
        usleep(200000);
        ret = OH_NativeWindow_CreateNativeWindowFromSurfaceId(surfId, &decoderWindow);
        retryCount++;
    }

    if (ret != 0 || decoderWindow == nullptr) {
        LOGE("StartEncodeDecode: failed to create decoder NativeWindow after retries");
        napi_create_int32(env, -1, &result);
        return result;
    }

    auto& glManager = OpenGLManager::GetInstance();

    // 设置解码输出窗口尺寸
    OH_NativeWindow_NativeWindowHandleOpt(decoderWindow, SET_BUFFER_GEOMETRY, renderWidth, renderHeight);

    // 1. 清理旧编码器
    if (g_videoEncoder) {
        OHNativeWindow* oldEncoderWindow = g_videoEncoder->GetInputSurface();
        if (oldEncoderWindow) {
            glManager.RemoveOutput(oldEncoderWindow);
        }
        g_videoEncoder->Stop();
        delete g_videoEncoder;
        g_videoEncoder = nullptr;
    }

    // 2. 创建并启动编码器
    g_videoEncoder = new VideoEncoder();
    if (!g_videoEncoder->Init(renderWidth, renderHeight, kbps, fps, "video/avc")) {
        LOGE("VideoEncoder Init failed");
        delete g_videoEncoder;
        g_videoEncoder = nullptr;
        OH_NativeWindow_DestroyNativeWindow(decoderWindow);
        napi_create_int32(env, -1, &result);
        return result;
    }
    if (!g_videoEncoder->Start()) {
        LOGE("VideoEncoder Start failed");
        delete g_videoEncoder;
        g_videoEncoder = nullptr;
        OH_NativeWindow_DestroyNativeWindow(decoderWindow);
        napi_create_int32(env, -1, &result);
        return result;
    }

    // 3. 将编码器输入 Surface 添加到 OpenGL 输出
    OHNativeWindow* encoderWindow = g_videoEncoder->GetInputSurface();
    if (!encoderWindow) {
        LOGE("GetInputSurface failed");
        g_videoEncoder->Stop();
        delete g_videoEncoder;
        g_videoEncoder = nullptr;
        OH_NativeWindow_DestroyNativeWindow(decoderWindow);
        napi_create_int32(env, -1, &result);
        return result;
    }
    OH_NativeWindow_NativeWindowHandleOpt(encoderWindow, SET_BUFFER_GEOMETRY, renderWidth, renderHeight);
    glManager.AddOutput(encoderWindow, renderWidth, renderHeight);

    g_encodeQueue = g_videoEncoder->GetOutputQueue();

    // 4. 等待编码器产出第一批数据
    usleep(200000);

    // 5. 创建并启动解码器
    g_decoderOutputWindow = decoderWindow;
    g_videoDecoder = new VideoDecoder();
    if (!g_videoDecoder->Init(decoderWindow, "video/avc", renderWidth, renderHeight)) {
        LOGE("VideoDecoder Init failed");
        glManager.RemoveOutput(encoderWindow);
        g_videoEncoder->Stop();
        delete g_videoEncoder;
        g_videoEncoder = nullptr;
        delete g_videoDecoder;
        g_videoDecoder = nullptr;
        OH_NativeWindow_DestroyNativeWindow(decoderWindow);
        g_decoderOutputWindow = nullptr;
        napi_create_int32(env, -1, &result);
        return result;
    }
    if (!g_videoDecoder->Start()) {
        LOGE("VideoDecoder Start failed");
        glManager.RemoveOutput(encoderWindow);
        g_videoEncoder->Stop();
        delete g_videoEncoder;
        g_videoEncoder = nullptr;
        delete g_videoDecoder;
        g_videoDecoder = nullptr;
        OH_NativeWindow_DestroyNativeWindow(decoderWindow);
        g_decoderOutputWindow = nullptr;
        napi_create_int32(env, -1, &result);
        return result;
    }

    // 6. 启动解码线程
    g_decoderRunning.store(true);
    g_decoderThread = std::thread(DecoderLoop);

    LOGI("StartEncodeDecode success");
    napi_create_int32(env, 0, &result);
    return result;
}

struct CaptureSetting {
    int32_t quality;
    int32_t rotation;
    int32_t location;
    bool mirror;
    int32_t latitude;
    int32_t longitude;
    int32_t altitude;
};

static napi_value SetZoomRatio(napi_env env, napi_callback_info info)
{
    size_t requireArgc = 2;
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_value result;

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_valuetype valuetype0;
    napi_typeof(env, args[0], &valuetype0);

    int32_t zoomRatio;
    napi_get_value_int32(env, args[0], &zoomRatio);

    LOGE( "SetZoomRatio : %{public}d", zoomRatio);

    ndkCamera_->setZoomRatioFn(zoomRatio);
    napi_create_int32(env, argc, &result);
    return result;
}

static napi_value HasFlash(napi_env env, napi_callback_info info)
{
    LOGE( "HasFlash");
    size_t requireArgc = 2;
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_value result;

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_valuetype valuetype0;
    napi_typeof(env, args[0], &valuetype0);

    int32_t flashMode;
    napi_get_value_int32(env, args[0], &flashMode);

    LOGE( "HasFlash flashMode : %{public}d", flashMode);

    ndkCamera_->HasFlashFn(flashMode);
    napi_create_int32(env, argc, &result);
    return result;
}

static napi_value IsVideoStabilizationModeSupported(napi_env env, napi_callback_info info)
{
    LOGE( "IsVideoStabilizationModeSupportedFn");
    size_t requireArgc = 2;
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_value result;

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_valuetype valuetype0;
    napi_typeof(env, args[0], &valuetype0);

    int32_t videoMode;
    napi_get_value_int32(env, args[0], &videoMode);

    LOGE( "IsVideoStabilizationModeSupportedFn videoMode : %{public}d", videoMode);

    ndkCamera_->IsVideoStabilizationModeSupportedFn(videoMode);
    napi_create_int32(env, argc, &result);
    return result;
}

// static napi_value InitCamera(napi_env env, napi_callback_info info)
// {
//     LOGE( "InitCamera Start");
//     size_t requireArgc = 3;
//     size_t argc = 3;
//     napi_value args[3] = {nullptr};
//     napi_value result;
//     size_t typeLen = 0;
//     char* surfaceId = nullptr;
//
//     napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
//
//     napi_get_value_string_utf8(env, args[0], nullptr, 0, &typeLen);
//     surfaceId = new char[typeLen + 1];
//     napi_get_value_string_utf8(env, args[0], surfaceId, typeLen + 1, &typeLen);
//
//     napi_valuetype valuetype1;
//     napi_typeof(env, args[1], &valuetype1);
//
//     int32_t focusMode;
//     napi_get_value_int32(env, args[1], &focusMode);
//
//     uint32_t cameraDeviceIndex;
//     napi_get_value_uint32(env, args[ARGS_TWO], &cameraDeviceIndex);
//
//     LOGE( "InitCamera focusMode : %{public}d", focusMode);
//     LOGE( "InitCamera surfaceId : %{public}s", surfaceId);
//     LOGE( "InitCamera cameraDeviceIndex : %{public}d", cameraDeviceIndex);
//
//     if (ndkCamera_) {
//         LOGE( "ndkCamera_ is not null");
//         delete ndkCamera_;
//         ndkCamera_ = nullptr;
//     }
//
//     // 先启动 OpenGL 管线，将 XComponent 的 surfaceId 作为渲染目标
//     // 默认分辨率 640x480，后续可通过 UpdateSize 动态调整
//     int32_t renderWidth = 640;
//     int32_t renderHeight = 480;
//     bool glStarted = OpenGLManager::GetInstance().Start(std::string(surfaceId), renderWidth, renderHeight);
//     if (!glStarted) {
//         LOGE("OpenGLManager 启动失败");
//     }
//
//     // NDKCamera 构造函数内部会从 OpenGLManager 获取输入 Surface ID
//     // 摄像头数据将写入 OpenGLManager 创建的 Surface
//     ndkCamera_ = new NDKCamera(surfaceId, focusMode, cameraDeviceIndex);
//     LOGE( "InitCamera End");
//     napi_create_int32(env, argc, &result);
//     return result;
// }


static napi_value InitCamera(napi_env env, napi_callback_info info)
{
    LOGE("InitCamera Start");
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_value result;
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // 1. 获取 surfaceId
    size_t typeLen = 0;
    napi_get_value_string_utf8(env, args[0], nullptr, 0, &typeLen);
    char* surfaceId = new char[typeLen + 1];
    napi_get_value_string_utf8(env, args[0], surfaceId, typeLen + 1, &typeLen);

    int32_t focusMode;
    napi_get_value_int32(env, args[1], &focusMode);

    uint32_t cameraDeviceIndex;
    napi_get_value_uint32(env, args[2], &cameraDeviceIndex);

    LOGE("InitCamera focusMode : %d", focusMode);
    LOGE("InitCamera surfaceId : %s", surfaceId);
    LOGE("InitCamera cameraDeviceIndex : %d", cameraDeviceIndex);

    if (ndkCamera_) {
        delete ndkCamera_;
        ndkCamera_ = nullptr;
    }

    // 2. 创建输出 NativeWindow（XComponent 的 Surface）
    uint64_t surfId = std::stoull(surfaceId);
    OHNativeWindow* outputWindow = nullptr;
    int ret = OH_NativeWindow_CreateNativeWindowFromSurfaceId(surfId, &outputWindow);
    if (ret != 0 || outputWindow == nullptr) {
        LOGE("创建输出 NativeWindow 失败, ret=%d", ret);
        delete[] surfaceId;
        napi_create_int32(env, -1, &result);
        return result;
    }     
    LOGE("创建输出 NativeWindow , renderWidth=%d renderHeight=%d", renderWidth,renderHeight);
    LOGE("创建输出 NativeWindow , renderWidth=%{public}d renderHeight=%{public}d", renderWidth, renderHeight);
    OH_NativeWindow_NativeWindowHandleOpt(outputWindow, SET_BUFFER_GEOMETRY, renderWidth, renderHeight);

    // 3. 初始化 OpenGLManager
    auto& glManager = OpenGLManager::GetInstance();
    if (!glManager.Initialize(renderWidth, renderHeight)) {
        LOGE("OpenGLManager 初始化失败");
        OH_NativeWindow_DestroyNativeWindow(outputWindow);
        delete[] surfaceId;
        napi_create_int32(env, -1, &result);
        return result;
    }

    if (!glManager.Start()) {
        LOGE("OpenGLManager 启动失败");
        glManager.Stop();
        OH_NativeWindow_DestroyNativeWindow(outputWindow);
        delete[] surfaceId;
        napi_create_int32(env, -1, &result);
        return result;
    }

    // 4. 添加输出目标（XComponent）
    glManager.AddOutput(outputWindow, renderWidth, renderHeight);

    // 5. 获取摄像头输入 Surface ID（OpenGLManager 内部已创建 NativeImage）
    std::string inputSurfaceId = glManager.GetInputSurfaceId();
    if (inputSurfaceId.empty()) {
        LOGE("获取输入 Surface ID 失败");
        glManager.Stop();
        OH_NativeWindow_DestroyNativeWindow(outputWindow);
        delete[] surfaceId;
        napi_create_int32(env, -1, &result);
        return result;
    }

    LOGE("摄像头输入 Surface ID: %s", inputSurfaceId.c_str());

    // 6. 创建 NDKCamera，传入输入 Surface ID
    ndkCamera_ = new NDKCamera(inputSurfaceId.c_str(), focusMode, cameraDeviceIndex);

    // 7. 保存 outputWindow 以便后续释放（例如在 ReleaseCamera 中）
    // 建议将 outputWindow 保存到全局或类静态变量中，这里简单演示：
    static OHNativeWindow* s_outputWindow = nullptr;
    s_outputWindow = outputWindow;  // 注意：实际项目中应该用更安全的方式管理

    LOGE("InitCamera End");
    delete[] surfaceId;
    napi_create_int32(env, 0, &result);
    return result;
}

// ---------- 修改 ReleaseCamera：清理编解码器 ----------
static napi_value ReleaseCamera(napi_env env, napi_callback_info info) {
    LOGE("ReleaseCamera Start");
    ndkCamera_->ReleaseCamera();
    if (ndkCamera_) {
        delete ndkCamera_;
        ndkCamera_ = nullptr;
    }

    // ---- 新增：清理编解码器 ----
    g_decoderRunning.store(false);
    if (g_encodeQueue) {
        g_encodeQueue->Stop();
    }
    if (g_decoderThread.joinable()) {
        g_decoderThread.join();
    }

    if (g_videoEncoder) {
        g_videoEncoder->Stop();
        delete g_videoEncoder;
        g_videoEncoder = nullptr;
    }
    if (g_videoDecoder) {
        g_videoDecoder->Stop();
        delete g_videoDecoder;
        g_videoDecoder = nullptr;
    }
    g_encodeQueue.reset();

    if (g_decoderOutputWindow) {
        OH_NativeWindow_DestroyNativeWindow(g_decoderOutputWindow);
        g_decoderOutputWindow = nullptr;
    }

    OpenGLManager::GetInstance().Stop();

    LOGE("ReleaseCamera End");
    napi_value result;
    napi_create_int32(env, 0, &result);
    return result;
}
static napi_value ReleaseSession(napi_env env, napi_callback_info info)
{
    LOGE( "ReleaseCamera Start");
    size_t requireArgc = 2;
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_value result;
    size_t typeLen = 0;
    char* surfaceId = nullptr;

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    ndkCamera_->ReleaseSession();

    LOGE( "ReleaseCamera End");
    napi_create_int32(env, argc, &result);
    return result;
}
static napi_value StartPhotoOrVideo(napi_env env, napi_callback_info info)
{
    OH_LOG_INFO(LOG_APP, "StartPhotoOrVideo Start");
    Camera_ErrorCode ret = CAMERA_OK;
    size_t requireArgc = 3;
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_value result;
    size_t typeLen = 0;
    size_t videoIdLen = 0;
    size_t photoIdLen = 0;
    char* modeFlag = nullptr;
    char* videoId = nullptr;
    char* photoId = nullptr;

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_get_value_string_utf8(env, args[0], nullptr, 0, &typeLen);
    modeFlag = new char[typeLen + 1];
    napi_get_value_string_utf8(env, args[0], modeFlag, typeLen + 1, &typeLen);

    napi_get_value_string_utf8(env, args[1], nullptr, 0, &videoIdLen);
    videoId = new char[videoIdLen + 1];
    napi_get_value_string_utf8(env, args[1], videoId, videoIdLen + 1, &videoIdLen);

    napi_get_value_string_utf8(env, args[ARGS_TWO], nullptr, 0, &photoIdLen);
    photoId = new char[photoIdLen + 1];
    napi_get_value_string_utf8(env, args[ARGS_TWO], photoId, photoIdLen + 1, &photoIdLen);

    if (!strcmp(modeFlag, "photo")) {
        LOGE( "StartPhoto surfaceId %{public}s", photoId);
        ret = ndkCamera_->StartPhoto(photoId);
    } else if (!strcmp(modeFlag, "video")) {
        ret = ndkCamera_->StartVideo(videoId, photoId);
        LOGE( "StartPhotoOrVideo %{public}s, %{public}s", videoId, photoId);
    }
    napi_create_int32(env, ret, &result);
    return result;
}

static napi_value VideoOutputStart(napi_env env, napi_callback_info info)
{
    OH_LOG_INFO(LOG_APP, "VideoOutputStart Start");
    napi_value result;
    Camera_ErrorCode ret = ndkCamera_->VideoOutputStart();
    napi_create_int32(env, ret, &result);
    return result;
}

static napi_value IsExposureModeSupported(napi_env env, napi_callback_info info)
{
    OH_LOG_INFO(LOG_APP, "IsExposureModeSupported exposureMode start.");
    size_t requireArgc = 2;
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_value result;

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_valuetype valuetype0;
    napi_typeof(env, args[0], &valuetype0);

    int32_t exposureMode;
    napi_get_value_int32(env, args[0], &exposureMode);

    LOGE( "IsExposureModeSupported exposureMode : %{public}d", exposureMode);

    ndkCamera_->IsExposureModeSupportedFn(exposureMode);
    OH_LOG_INFO(LOG_APP, "IsExposureModeSupported exposureMode end.");
    napi_create_int32(env, argc, &result);
    return result;
}

static napi_value IsMeteringPoint(napi_env env, napi_callback_info info)
{
    size_t requireArgc = 2;
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_value result;

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_valuetype valuetype0;
    napi_typeof(env, args[0], &valuetype0);
    int x;
    napi_get_value_int32(env, args[0], &x);

    napi_valuetype valuetype1;
    napi_typeof(env, args[0], &valuetype0);
    int y;
    napi_get_value_int32(env, args[1], &y);
    ndkCamera_->IsMeteringPoint(x, y);
    napi_create_int32(env, argc, &result);
    return result;
}

static napi_value IsExposureBiasRange(napi_env env, napi_callback_info info)
{
    OH_LOG_INFO(LOG_APP, "IsExposureBiasRange start.");
    size_t requireArgc = 2;
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_value result;

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_valuetype valuetype0;
    napi_typeof(env, args[0], &valuetype0);

    int exposureBiasValue;
    napi_get_value_int32(env, args[0], &exposureBiasValue);
    ndkCamera_->IsExposureBiasRange(exposureBiasValue);
    OH_LOG_INFO(LOG_APP, "IsExposureBiasRange end.");
    napi_create_int32(env, argc, &result);
    return result;
}

static napi_value IsFocusModeSupported(napi_env env, napi_callback_info info)
{
    OH_LOG_INFO(LOG_APP, "IsFocusModeSupported start.");
    size_t requireArgc = 2;
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_value result;

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_valuetype valuetype0;
    napi_typeof(env, args[0], &valuetype0);

    int32_t focusMode;
    napi_get_value_int32(env, args[0], &focusMode);

    LOGE( "IsFocusModeSupportedFn videoMode : %{public}d", focusMode);

    ndkCamera_->IsFocusModeSupported(focusMode);
    OH_LOG_INFO(LOG_APP, "IsFocusModeSupported end.");
    napi_create_int32(env, argc, &result);
    return result;
}

static napi_value IsFocusPoint(napi_env env, napi_callback_info info)
{
    size_t requireArgc = 2;
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_value result;

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_valuetype valuetype0;
    napi_typeof(env, args[0], &valuetype0);
    double x;
    napi_get_value_double(env, args[0], &x);

    napi_valuetype valuetype1;
    napi_typeof(env, args[1], &valuetype1);
    double y;
    napi_get_value_double(env, args[1], &y);

    float focusPointX = static_cast<float>(x);
    float focusPointY = static_cast<float>(y);
    ndkCamera_->IsFocusPoint(focusPointX, focusPointY);
    napi_create_int32(env, argc, &result);
    return result;
}

static napi_value GetVideoFrameWidth(napi_env env, napi_callback_info info)
{
    LOGE( "GetVideoFrameWidth Start");
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_value result = nullptr;
    napi_create_int32(env, ndkCamera_->GetVideoFrameWidth(), &result);

    LOGE( "GetVideoFrameWidth End");
    return result;
}

static napi_value GetVideoFrameHeight(napi_env env, napi_callback_info info)
{
    LOGE( "GetVideoFrameHeight Start");
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_value result = nullptr;
    napi_create_int32(env, ndkCamera_->GetVideoFrameHeight(), &result);

    LOGE( "GetVideoFrameHeight End");
    return result;
}

static napi_value GetVideoFrameRate(napi_env env, napi_callback_info info)
{
    LOGE( "GetVideoFrameRate Start");
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_value result = nullptr;
    napi_create_int32(env, ndkCamera_->GetVideoFrameRate(), &result);

    LOGE( "GetVideoFrameRate End");
    return result;
}

static napi_value VideoOutputStopAndRelease(napi_env env, napi_callback_info info)
{
    LOGE( "VideoOutputStopAndRelease Start");
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_value result = nullptr;
    ndkCamera_->VideoOutputStop();
    ndkCamera_->VideoOutputRelease();

    LOGE( "VideoOutputStopAndRelease End");
    napi_create_int32(env, argc, &result);
    return result;
}

static napi_value TakePicture(napi_env env, napi_callback_info info)
{
    OH_LOG_INFO(LOG_APP, "TakePicture Start");
    napi_value result;
    Camera_ErrorCode ret = ndkCamera_->TakePicture();
    LOGE( "TakePicture result is %{public}d", ret);
    napi_create_int32(env, ret, &result);
    return result;
}

static napi_value GetCaptureParam(napi_env env, napi_value captureConfigValue, CaptureSetting *config)
{
    napi_value value = nullptr;
    napi_get_named_property(env, captureConfigValue, "quality", &value);
    napi_get_value_int32(env, value, &config->quality);

    napi_get_named_property(env, captureConfigValue, "rotation", &value);
    napi_get_value_int32(env, value, &config->rotation);

    napi_get_named_property(env, captureConfigValue, "mirror", &value);
    napi_get_value_bool(env, value, &config->mirror);

    napi_get_named_property(env, captureConfigValue, "latitude", &value);
    napi_get_value_int32(env, value, &config->latitude);

    napi_get_named_property(env, captureConfigValue, "longitude", &value);
    napi_get_value_int32(env, value, &config->longitude);

    napi_get_named_property(env, captureConfigValue, "altitude", &value);
    napi_get_value_int32(env, value, &config->altitude);

    OH_LOG_INFO(LOG_APP,
                "get quality %{public}d, rotation %{public}d, mirror %{public}d, latitude "
                "%{public}d, longitude %{public}d, altitude %{public}d",
                config->quality, config->rotation, config->mirror, config->latitude, config->longitude,
                config->altitude);
    return 0;
}
static void SetConfig(CaptureSetting settings, Camera_PhotoCaptureSetting *photoSetting, Camera_Location *location)
{
    if (photoSetting == nullptr || location == nullptr) {
        OH_LOG_INFO(LOG_APP, "photoSetting is null");
    }
    photoSetting->quality = static_cast<Camera_QualityLevel>(settings.quality);
    photoSetting->rotation = static_cast<Camera_ImageRotation>(settings.rotation);
    photoSetting->mirror = settings.mirror;
    location->altitude = settings.altitude;
    location->latitude = settings.latitude;
    location->longitude = settings.longitude;
    photoSetting->location = location;
}

static napi_value TakePictureWithSettings(napi_env env, napi_callback_info info)
{
    OH_LOG_INFO(LOG_APP, "TakePictureWithSettings Start");
    size_t requireArgc = 1;
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    Camera_PhotoCaptureSetting photoSetting;
    CaptureSetting settingInner;
    Camera_Location *location = new Camera_Location;

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    GetCaptureParam(env, args[0], &settingInner);
    SetConfig(settingInner, &photoSetting, location);

    napi_value result;
    Camera_ErrorCode ret = ndkCamera_->TakePictureWithPhotoSettings(photoSetting);
    LOGE( "TakePictureWithSettings result is %{public}d", ret);
    napi_create_int32(env, ret, &result);
    return result;
}

static napi_value NAPI_Global_isFocusModeSupported(napi_env env, napi_callback_info info) {}
EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"initCamera", nullptr, InitCamera, nullptr, nullptr, nullptr, napi_default, nullptr},         
        {"startEncodeDecode", nullptr, StartEncodeDecode, nullptr, nullptr, nullptr, napi_default, nullptr}, // 新增
        {"startPhotoOrVideo", nullptr, StartPhotoOrVideo, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"videoOutputStart", nullptr, VideoOutputStart, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setZoomRatio", nullptr, SetZoomRatio, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"hasFlash", nullptr, HasFlash, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"isVideoStabilizationModeSupported", nullptr, IsVideoStabilizationModeSupported, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"isExposureModeSupported", nullptr, IsExposureModeSupported, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"isMeteringPoint", nullptr, IsMeteringPoint, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"isExposureBiasRange", nullptr, IsExposureBiasRange, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"IsFocusModeSupported", nullptr, IsFocusModeSupported, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"isFocusPoint", nullptr, IsFocusPoint, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getVideoFrameWidth", nullptr, GetVideoFrameWidth, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getVideoFrameHeight", nullptr, GetVideoFrameHeight, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getVideoFrameRate", nullptr, GetVideoFrameRate, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"videoOutputStopAndRelease", nullptr, VideoOutputStopAndRelease, nullptr, nullptr, nullptr, napi_default,
         nullptr},
        {"takePicture", nullptr, TakePicture, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"takePictureWithSettings", nullptr, TakePictureWithSettings, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"releaseSession", nullptr, ReleaseSession, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"releaseCamera", nullptr, ReleaseCamera, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"isFocusModeSupported", nullptr, NAPI_Global_isFocusModeSupported, nullptr, nullptr, nullptr, napi_default,
         nullptr}};
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module demoModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "entry",
    .nm_priv = ((void *)0),
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterEntryModule(void)
{
    napi_module_register(&demoModule);
}