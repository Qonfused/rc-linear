#pragma once

#define GLEW_STATIC
#include <GL/glew.h>
#include <glm/glm.hpp>

// Fills an RGBA32F texture with an analytical scene via a compute shader.
// Writes linear radiance; no sRGB conversion.
class GPUScene {
public:
  GPUScene() : prog_(0), u_resolution_(-1), u_radius_(-1), u_color_(-1) {}
  ~GPUScene() { if (prog_) glDeleteProgram(prog_); }

  // Generate a simple scene into 'sceneTex' of size 'res'.
  // sceneTex must be a GL_TEXTURE_2D with internal format GL_RGBA32F (or GL_RGBA16F if desired).
  void generate(GLuint sceneTex,
                const glm::ivec2& res,
                float circleRadius,
                const glm::vec4& circleColor) {
    ensureProgram_();
    glUseProgram(prog_);
    glUniform2f(u_resolution_, float(res.x), float(res.y));
    glUniform1f(u_radius_, circleRadius);
    glUniform4f(u_color_, circleColor.r, circleColor.g, circleColor.b, circleColor.a);

    // Bind as image for write
    glBindImageTexture(0, sceneTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    GLuint gx = (GLuint)((res.x + 15) / 16);
    GLuint gy = (GLuint)((res.y + 15) / 16);
    glDispatchCompute(gx, gy, 1);

    // Ensure subsequent texture fetches see the generated data
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  }

private:
  GLuint prog_;
  GLint  u_resolution_;
  GLint  u_radius_;
  GLint  u_color_;

  static GLuint compileCompute_(const char* src) {
    GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(cs, 1, &src, nullptr);
    glCompileShader(cs);
    GLint ok = GL_FALSE;
    glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[4096];
      glGetShaderInfoLog(cs, 4096, nullptr, log);
      glDeleteShader(cs);
      return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, cs);
    glLinkProgram(prog);
    glDeleteShader(cs);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
      glDeleteProgram(prog);
      return 0;
    }
    return prog;
  }

  void ensureProgram_() {
    if (prog_) return;
    prog_ = compileCompute_(CS());
    // Cache uniform locations
    glUseProgram(prog_);
    u_resolution_ = glGetUniformLocation(prog_, "resolution");
    u_radius_     = glGetUniformLocation(prog_, "circleRadius");
    u_color_      = glGetUniformLocation(prog_, "circleColor");
  }

  static const char* CS() {
    return R"(
#version 430
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba32f) uniform writeonly image2D sceneImage;
uniform vec2  resolution;
uniform float circleRadius;
uniform vec4  circleColor;

void main() {
  ivec2 p = ivec2(gl_GlobalInvocationID.xy);
  if (p.x >= int(resolution.x) || p.y >= int(resolution.y)) return;

  // gl_FragCoord-like center with y up (match RC compute path)
  vec2 frag = vec2(float(p.x) + 0.5, resolution.y - 0.5 - float(p.y));
  vec2 center = (resolution * 0.5) - frag;

  vec4 radiance = vec4(0.0);
  if (length(center) - circleRadius < 0.0) {
    radiance = circleColor; // linear radiance in alpha-premultiplied-like "opacity" channel
  }

  imageStore(sceneImage, p, radiance);
}
    )";
  }
};
