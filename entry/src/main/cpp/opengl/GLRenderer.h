//
// Created by Lixiaoyao on 2026/1/6.
//

#pragma once

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>


// -----------------------------------------------------------
// 渲染类：负责 Shader 和 绘制
// -----------------------------------------------------------

class GLRenderer {
public:
    GLRenderer(); // 新增构造函数
    ~GLRenderer();

    void prepare(GLuint textureId);
    void draw(int viewWidth, int viewHeight, const float *textureMatrix);
    void setTransform(int rotation, bool mirrorH, bool mirrorV);
    void setVideoSize(int width, int height);
    void release();
    void clearBuff();

    void setFilterParams(float warm, float contrast, float saturation, float gamma, float brightness,
                         float r, float g, float b, float sharpness, float hue);

private:
    GLuint createProgram(const char *vSource, const char *fSource);
    GLuint loadShader(GLenum type, const char *source);
    void updateVertexScale(int viewWidth, int viewHeight);
    void checkGlError(const char* op); // 新增 GL 错误检查

    GLuint mProgram = 0;
    GLuint mTextureId = 0;

    // Handles
    GLint mPositionHandle = -1;
    GLint mTexCoordHandle = -1;
    GLint mTexMatrixHandle = -1;
    GLint mRotationHandle = -1;
    GLint mScaleHandle = -1;

    struct {
        GLint warm, contrast, saturation, gamma, brightness,
                red, green, blue, sharpness, hue;
    } mFilterHandles;

    // 参数
    int mVideoWidth = 1920, mVideoHeight = 1080;
    int mRotation = 0;
    bool mMirrorH = false, mMirrorV = false;

    // 滤镜值
    struct {
        float warm = 0, contrast = 1, saturation = 1, gamma = 1, brightness = 0,
                r = 1, g = 1, b = 1, sharpness = 0, hue = 0;
    } mFilterValues;

    float mVertexCoords[8];
    const float mTexCoords[8] = {0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
};