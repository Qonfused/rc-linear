#pragma once

#define GLEW_STATIC
#include <GL/glew.h>
#include <vector>

// Create or resize a 2D texture with specified parameters.
inline void ensureTexture2D(GLuint& tex,
                            int width,
                            int height,
                            GLint internalFormat = GL_RGBA32F,
                            GLint minFilter = GL_NEAREST,
                            GLint magFilter = GL_NEAREST,
                            GLint wrapS = GL_CLAMP_TO_EDGE,
                            GLint wrapT = GL_CLAMP_TO_EDGE) {
  if (tex != 0) {
    GLint w = 0, h = 0, fmt = 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
    if (w == width && h == height && fmt == internalFormat) {
      // Keep parameters in sync
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
      return;
    }
    glDeleteTextures(1, &tex);
    tex = 0;
  }

  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
}

// Clear a 2D texture to zero using glTexSubImage2D (portable without requiring GL 4.4 glClearTexImage).
inline void clearTexture2D(GLuint tex, int width, int height, GLenum format = GL_RGBA, GLenum type = GL_FLOAT) {
  if (tex == 0 || width <= 0 || height <= 0) return;
  std::vector<float> zeros(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0.0f);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format, type, zeros.data());
}

// Bind a texture to a texture unit for sampling (sampler2D).
inline void bindTextureUnit(GLuint tex, GLuint unit) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, tex);
}

// Bind a texture level as an image for read/write in shaders.
inline void bindImage(GLuint tex, GLuint binding, GLenum access, GLenum fmt = GL_RGBA32F, GLint level = 0) {
  glBindImageTexture(binding, tex, level, GL_FALSE, 0, access, fmt);
}

// Simple helper to delete a texture safely.
inline void deleteTexture(GLuint& tex) {
  if (tex) { glDeleteTextures(1, &tex); tex = 0; }
}
