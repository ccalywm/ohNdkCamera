//
// Created by Lixiaoyao on 2026/1/6.
//

#include "GLRenderer.h"
#include "util/DebugLog.h"
#include <cstdlib>
#include <cstring> // for memset

// ======================= GLRenderer 实现 =======================

// ======================= Shader 代码 =======================
const char *V_SHADER = R"(
attribute vec4 aPosition;
attribute vec4 aTexCoord;
uniform mat4 uTexMatrix;
uniform float uRotation;
uniform vec2 uScale;
varying vec2 vTexCoord;
void main() {
    gl_Position = aPosition;
    // 居中缩放/镜像逻辑
    vec2 centered = (aTexCoord.xy - 0.5) * uScale;
    // 旋转逻辑
    float rad = radians(uRotation);
    float c = cos(rad);
    float s = sin(rad);
    vec2 rotated = vec2(centered.x * c - centered.y * s, centered.x * s + centered.y * c);
    // 恢复坐标系并应用纹理矩阵
    vTexCoord = (uTexMatrix * vec4(rotated + 0.5, 0.0, 1.0)).xy;
})";

const char *F_SHADER = R"(
#extension GL_OES_EGL_image_external : require
precision mediump float;
varying vec2 vTexCoord;
uniform samplerExternalOES sTexture;
uniform float uWarmFilter; uniform float uContrast; uniform float uSaturation;
uniform float uGamma; uniform float uBrightness; uniform float uRed; uniform float uGreen; uniform float uBlue; uniform float uHue;

void main() {
    vec4 color = texture2D(sTexture, vTexCoord);

    // 1. 亮度
    vec3 brightColor = color.rgb + uBrightness;

    // 2. RGB通道
    vec3 rgbColor = vec3(brightColor.r * uRed, brightColor.g * uGreen, brightColor.b * uBlue);

    // 3. 暖色调 (简单的通道增益模拟)
    float rB = 1.0 + 0.1 * uWarmFilter;
    float gB = 1.0 + 0.05 * uWarmFilter;
    float bB = 1.0 - 0.1 * uWarmFilter;
    vec3 warmColor = vec3(rgbColor.r * rB, rgbColor.g * gB, rgbColor.b * bB);

    // 4. 对比度
    vec3 contrastColor = (warmColor - 0.5) * uContrast + 0.5;

    // 5. 饱和度
    float gray = dot(contrastColor, vec3(0.299, 0.587, 0.114));
    vec3 satColor = mix(vec3(gray), contrastColor, uSaturation);

    // 6. Gamma
    vec3 gammaColor = pow(satColor, vec3(1.0/uGamma));

    // 7. 色相 (简易版混合，非标准HSV旋转)
    vec3 hueAdj = mix(gammaColor, vec3(gammaColor.g, gammaColor.b, gammaColor.r), uHue * 0.5);

    gl_FragColor = vec4(clamp(hueAdj, 0.0, 1.0), color.a);
})";

// ======================= 实现 =======================
GLRenderer::GLRenderer() {
    // 安全初始化顶点数据，避免第一帧可能的随机值
    memset(mVertexCoords, 0, sizeof(mVertexCoords));
    // 默认填满屏幕
    float defaultCoords[] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    memcpy(mVertexCoords, defaultCoords, sizeof(defaultCoords));
}

GLRenderer::~GLRenderer() {
    release();
}

void GLRenderer::prepare(GLuint textureId) {
    mTextureId = textureId;
    mProgram = createProgram(V_SHADER, F_SHADER);

    if (mProgram == 0) {
        LOGE("Failed to create program");
        return;
    }

    mPositionHandle = glGetAttribLocation(mProgram, "aPosition");
    mTexCoordHandle = glGetAttribLocation(mProgram, "aTexCoord");
    mTexMatrixHandle = glGetUniformLocation(mProgram, "uTexMatrix");
    mRotationHandle = glGetUniformLocation(mProgram, "uRotation");
    mScaleHandle = glGetUniformLocation(mProgram, "uScale");

    mFilterHandles.warm = glGetUniformLocation(mProgram, "uWarmFilter");
    mFilterHandles.contrast = glGetUniformLocation(mProgram, "uContrast");
    mFilterHandles.saturation = glGetUniformLocation(mProgram, "uSaturation");
    mFilterHandles.gamma = glGetUniformLocation(mProgram, "uGamma");
    mFilterHandles.brightness = glGetUniformLocation(mProgram, "uBrightness");
    mFilterHandles.red = glGetUniformLocation(mProgram, "uRed");
    mFilterHandles.green = glGetUniformLocation(mProgram, "uGreen");
    mFilterHandles.blue = glGetUniformLocation(mProgram, "uBlue");
    mFilterHandles.hue = glGetUniformLocation(mProgram, "uHue");
}

void GLRenderer::draw(int viewWidth, int viewHeight, const float *textureMatrix) {
    if (mProgram == 0) return; // 防止未初始化崩溃

    glViewport(0, 0, viewWidth, viewHeight);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(mProgram);
    checkGlError("glUseProgram");

    updateVertexScale(viewWidth, viewHeight);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, mTextureId);

    // 启用顶点属性
    glEnableVertexAttribArray(mPositionHandle);
    glVertexAttribPointer(mPositionHandle, 2, GL_FLOAT, GL_FALSE, 0, mVertexCoords);

    glEnableVertexAttribArray(mTexCoordHandle);
    glVertexAttribPointer(mTexCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, mTexCoords);

    // 设置 Uniforms
    glUniformMatrix4fv(mTexMatrixHandle, 1, GL_FALSE, textureMatrix);
    glUniform1f(mRotationHandle, (float) mRotation);
    glUniform2f(mScaleHandle, mMirrorH ? -1.0f : 1.0f, mMirrorV ? -1.0f : 1.0f);

    glUniform1f(mFilterHandles.warm, mFilterValues.warm);
    glUniform1f(mFilterHandles.contrast, mFilterValues.contrast);
    glUniform1f(mFilterHandles.saturation, mFilterValues.saturation);
    glUniform1f(mFilterHandles.gamma, mFilterValues.gamma);
    glUniform1f(mFilterHandles.brightness, mFilterValues.brightness);
    glUniform1f(mFilterHandles.red, mFilterValues.r);
    glUniform1f(mFilterHandles.green, mFilterValues.g);
    glUniform1f(mFilterHandles.blue, mFilterValues.b);
    glUniform1f(mFilterHandles.hue, mFilterValues.hue);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // 禁用属性 (好习惯，防止影响其他绘制)
    glDisableVertexAttribArray(mPositionHandle);
    glDisableVertexAttribArray(mTexCoordHandle);
}

void GLRenderer::updateVertexScale(int viewWidth, int viewHeight) {
    // 关键修复：防止除以零崩溃
    if (viewWidth <= 0 || viewHeight <= 0 || mVideoWidth <= 0 || mVideoHeight <= 0) return;

    bool isVertical = (mRotation == 90 || mRotation == 270);
    int vidW = isVertical ? mVideoWidth : mVideoHeight;
    int vidH = isVertical ? mVideoHeight : mVideoWidth;

    float viewRatio = (float) viewWidth / viewHeight;
    float vidRatio = (float) vidW / vidH;
    float sX = 1.0f, sY = 1.0f;

    // Center Crop 算法
    if (viewRatio > vidRatio) {
        sX = vidRatio / viewRatio;
    } else {
        sY = viewRatio / vidRatio;
    }

    mVertexCoords[0] = -sX;
    mVertexCoords[1] = -sY;
    mVertexCoords[2] = sX;
    mVertexCoords[3] = -sY;
    mVertexCoords[4] = -sX;
    mVertexCoords[5] = sY;
    mVertexCoords[6] = sX;
    mVertexCoords[7] = sY;
}

void GLRenderer::setTransform(int r, bool mh, bool mv) {
    mRotation = r;
    mMirrorH = mh;
    mMirrorV = mv;
}

void GLRenderer::setVideoSize(int w, int h) {
    mVideoWidth = w;
    mVideoHeight = h;
}

void GLRenderer::setFilterParams(float w, float c, float s, float g, float b, float r, float gr,
                                 float bl, float sh, float h) {
    mFilterValues = {w, c, s, g, b, r, gr, bl, sh, h};
}

void GLRenderer::clearBuff() {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}

void GLRenderer::release() {
    if (mProgram) {
        glDeleteProgram(mProgram);
        mProgram = 0;
    }
    // 注意：mTextureId 通常是外部管理的(OES)，这里不进行 glDeleteTextures，
    // 除非确认纹理生命周期完全由 Renderer 控制。
    mTextureId = 0;
}

// ======================= 关键：带错误检查的 Shader 加载 =======================

GLuint GLRenderer::loadShader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) return 0;

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    // 检查编译状态
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char *infoLog = (char *) malloc(sizeof(char) * infoLen);
            glGetShaderInfoLog(shader, infoLen, nullptr, infoLog);
            LOGE("Error compiling shader:\n%s", infoLog);
            free(infoLog);
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint GLRenderer::createProgram(const char *v, const char *f) {
    GLuint vs = loadShader(GL_VERTEX_SHADER, v);
    GLuint fs = loadShader(GL_FRAGMENT_SHADER, f);
    if (vs == 0 || fs == 0) return 0;

    GLuint program = glCreateProgram();
    if (program == 0) return 0;

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    // 检查链接状态
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint infoLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char *infoLog = (char *) malloc(sizeof(char) * infoLen);
            glGetProgramInfoLog(program, infoLen, nullptr, infoLog);
            LOGE("Error linking program:\n%s", infoLog);
            free(infoLog);
        }
        glDeleteProgram(program);
        return 0;
    }

    // Shader 链接到 Program 后，可以删掉 Shader 对象了
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

void GLRenderer::checkGlError(const char *op) {
    for (GLint error = glGetError(); error; error = glGetError()) {
        LOGE("after %s() glError (0x%x)\n", op, error);
    }
}