//
// Created on 2026/7/13.
//
// Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// please include "napi/native_api.h".

#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

class GLRenderer {
public:
    GLRenderer();
    ~GLRenderer();

    // 准备：创建着色器、VBO等，传入OES纹理ID
    bool prepare(GLuint oesTextureId);

    // 绘制
    void draw(int width, int height, const float* texMatrix);

    // 变换控制
    void setTransform(int rotationDeg, bool flipH, bool flipV);

    // 释放资源
    void release();

private:
    bool createShaderProgram();
    void createFullscreenQuad();

    GLuint program_;
    GLuint vao_, vbo_, ebo_;
    GLuint oesTextureId_;

    // Uniform 位置
    GLint uTextureLoc_;
    GLint uTexMatrixLoc_;

    // 变换参数
    int  rotationDeg_  =0;
    bool flipH_ = false;
    bool flipV_ = false;
};

#endif // GL_RENDERER_H
