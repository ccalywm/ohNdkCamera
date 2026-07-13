//
// Created on 2026/7/13.
//
// Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// please include "napi/native_api.h".

#include "GLRenderer.h"
#include "util/DebugLog.h"
#include <cstring>
#include <cmath>

static const char* VERTEX_SHADER = R"(#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aTexCoord;
uniform mat4 uTexMatrix;
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = (uTexMatrix * vec4(aTexCoord, 0.0, 1.0)).xy;
}
)";

static const char* FRAGMENT_SHADER = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
uniform samplerExternalOES uTexture;
in vec2 vTexCoord;
out vec4 fragColor;
void main() {
    fragColor = texture(uTexture, vTexCoord);
}
)";

static const float QUAD_VERTICES[] = {
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 0.0f
};
static const uint16_t QUAD_INDICES[] = {0,1,2, 0,2,3};

GLRenderer::GLRenderer()
    : program_(0), vao_(0), vbo_(0), ebo_(0), oesTextureId_(0),
      uTextureLoc_(-1), uTexMatrixLoc_(-1),
      rotationDeg_(0), flipH_(false), flipV_(false) {}

GLRenderer::~GLRenderer() {
    release();
}

bool GLRenderer::prepare(GLuint oesTextureId) {
    oesTextureId_ = oesTextureId;

    if (!createShaderProgram()) {
        LOGE("createShaderProgram failed");
        return false;
    }

    createFullscreenQuad();
    return true;
}

bool GLRenderer::createShaderProgram() {
    auto compileShader = [](GLenum type, const char* src) -> GLuint {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint status;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status != GL_TRUE) {
            GLchar log[512];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            LOGE("Shader compile error: %s", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    GLuint vs = compileShader(GL_VERTEX_SHADER, VERTEX_SHADER);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);

    GLint linked;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLchar log[512];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        LOGE("Program link error: %s", log);
        glDeleteProgram(program_);
        program_ = 0;
        glDeleteShader(vs);
        glDeleteShader(fs);
        return false;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    uTextureLoc_ = glGetUniformLocation(program_, "uTexture");
    uTexMatrixLoc_ = glGetUniformLocation(program_, "uTexMatrix");

    return true;
}

void GLRenderer::createFullscreenQuad() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTICES), QUAD_VERTICES, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(QUAD_INDICES), QUAD_INDICES, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void GLRenderer::setTransform(int rotationDeg, bool flipH, bool flipV) {
    rotationDeg_ = rotationDeg;
    flipH_ = flipH;
    flipV_ = flipV;
    // LOGI("GLRenderer::setTransform: rotation=%d, flipH=%d, flipV=%d", rotationDeg, flipH, flipV);
}
static void multiplyMat4(const float a[16], const float b[16], float out[16]) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i*4 + j] = 0.0f;
            for (int k = 0; k < 4; k++) {
                out[i*4 + j] += a[i*4 + k] * b[k*4 + j];
            }
        }
    }
}
static void buildUserTransformMatrix(float angleDeg, bool flipH, bool flipV, float out[16]) {
    // 初始化为单位矩阵
    memset(out, 0, sizeof(float) * 16);
    out[0] = out[5] = out[10] = out[15] = 1.0f;

    // 无变换时直接返回
    if (angleDeg == 0.0f && !flipH && !flipV) return;

    float rad = angleDeg * 3.141592653589793f / 180.0f;
    float c = cosf(rad);
    float s = sinf(rad);
    float sx = flipH ? -1.0f : 1.0f;
    float sy = flipV ? -1.0f : 1.0f;

    // 变换公式：先平移至中心 (-0.5, -0.5) -> 缩放/旋转 -> 再平移回 (+0.5, +0.5)
    // 矩阵列主序：
    //  m00 = sx*c,    m01 = -sy*s,    m03 = 0.5 - 0.5*sx*c + 0.5*sy*s
    //  m10 = sx*s,    m11 = sy*c,     m13 = 0.5 - 0.5*sx*s - 0.5*sy*c
    out[0] = sx * c;
    out[4] = -sy * s;
    out[12] = 0.5f - 0.5f * sx * c + 0.5f * sy * s;

    out[1] = sx * s;
    out[5] = sy * c;
    out[13] = 0.5f - 0.5f * sx * s - 0.5f * sy * c;
}
void GLRenderer::draw(int width, int height, const float* texMatrix) {
    if (program_ == 0 || oesTextureId_ == 0) return;

    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTextureId_);
    glUniform1i(uTextureLoc_, 0);

    // 构建用户变换矩阵
    float userMat[16];
    buildUserTransformMatrix((float)rotationDeg_, flipH_, flipV_, userMat);

    // // 打印调试信息
    // LOGI("draw: rotationDeg_=%d, flipH=%d, flipV=%d", rotationDeg_, flipH_, flipV_);
    // // 打印 userMat 前4个元素
    // LOGI("userMat[0]=%f, userMat[4]=%f, userMat[12]=%f, userMat[1]=%f, userMat[5]=%f, userMat[13]=%f",
    //      userMat[0], userMat[4], userMat[12], userMat[1], userMat[5], userMat[13]);

    float finalMat[16];
    // 组合矩阵 = texMatrix * userMat（先用户变换，再摄像头校正）
    for (int i=0; i<4; ++i) {
        for (int j=0; j<4; ++j) {
            finalMat[i*4+j] = 0;
            for (int k=0; k<4; ++k) {
                finalMat[i*4+j] += texMatrix[i*4+k] * userMat[k*4+j];
            }
        }
    }

    glUniformMatrix4fv(uTexMatrixLoc_, 1, GL_FALSE, finalMat);

    glViewport(0, 0, width, height);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
    glBindVertexArray(0);
}

void GLRenderer::release() {
    if (program_) glDeleteProgram(program_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    program_ = 0; vao_ = vbo_ = ebo_ = 0;
}
