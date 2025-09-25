#pragma once

#define GLEW_STATIC

#include <string>
#include <iostream>
#include <utility>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "texture.hpp"
#include "scene.hpp"
#include "perf.hpp"

// GPU-only Radiance Cascade renderer.
// - Inputs sampled via sampler2D (texelFetch); outputs written via imageStore (RGBA32F).
// - Intermediates remain linear; final sRGB OETF is applied only in the blit-to-display compute.
class RCGPURenderer {
public:
  RCGPURenderer()
  : rc_program_(0)
  , blit_program_(0)
  , scene_texture_(0)
  , cascade_input_(0)
  , cascade_output_(0)
  , display_texture_(0)
  , width_(0)
  , height_(0)
  , gpu_available_(false) {}

  ~RCGPURenderer() {
    cleanup();
  }

  bool initialize() {
    // Check compute support
    if (!GLEW_VERSION_4_3 && !GLEW_ARB_compute_shader) {
      std::cerr << "Compute shaders not supported on this context.\n";
      gpu_available_ = false;
      return false;
    }
    // Compile RC compute shader now; blit program is compiled on first use
    rc_program_ = compileCompute_(rcCS_());
    if (rc_program_ == 0) {
      std::cerr << "Failed to compile RC compute program.\n";
      gpu_available_ = false;
      return false;
    }
    gpu_available_ = true;
    return true;
  }

  // Orchestrates scene generation and all cascade passes; then blits to an internal RGBA8 texture.
  // If 'perf' is provided, brackets the entire RC workload (scene + cascades + blit) with RC timers.
  void run_full_rc(int baseProbeSize,
                   float baseIntervalLength,
                   int numCascades,
                   const glm::ivec2& resolution,
                   Perf* perf = nullptr) {
    if (!gpu_available_) return;

    ensureTextures_(resolution.x, resolution.y);

    if (perf) { perf->beginCpuRC(); perf->beginGpuRC(); }

    // Generate analytical scene into scene_texture_ (RGBA32F, linear)
    scene_.generate(scene_texture_, resolution, /*circleRadius*/15.0f, /*circleColor*/glm::vec4(1,1,1,1));

    // Prepare initial N+1 texture (cascade_input_) to zero; barrier so subsequent sampling is coherent
    clearTexture2D(cascade_input_, width_, height_);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // Run cascades from top (N = numCascades-1) down to 0
    for (int i = numCascades - 1; i >= 0; --i) {
      run_cascade_pass(baseProbeSize, baseIntervalLength, i, resolution);
    }

    // Single barrier after all cascades complete
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // Postprocess blit from final RGBA32F (linear) into RGBA8 (sRGB) for display
    ensureTexture2D(display_texture_, width_, height_, GL_RGBA8, GL_LINEAR, GL_LINEAR);
    run_blit_to_display(resolution);

    // Barrier so callers can immediately sample display_texture_
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    if (perf) { perf->endGpuRC(); perf->endCpuRC(); }
  }

  // Final linear RGBA32F after last pass (ping-pong leaves newest in cascade_input_)
  GLuint resultTex() const { return cascade_input_; }

  // Display-friendly RGBA8 texture after blit
  GLuint displayTex() const { return display_texture_; }

  bool gpuAvailable() const { return gpu_available_; }

private:
  // ----------------------------
  // GL objects and state
  // ----------------------------
  GLuint rc_program_;
  GLuint blit_program_;
  GPUScene scene_;

  GLuint scene_texture_;
  GLuint cascade_input_;
  GLuint cascade_output_;
  GLuint display_texture_;

  int  width_;
  int  height_;
  bool gpu_available_;

  // ----------------------------
  // Helpers
  // ----------------------------
  void cleanup() {
    if (rc_program_)     { glDeleteProgram(rc_program_);     rc_program_ = 0; }
    if (blit_program_)   { glDeleteProgram(blit_program_);   blit_program_ = 0; }
    if (scene_texture_)  { glDeleteTextures(1, &scene_texture_);  scene_texture_ = 0; }
    if (cascade_input_)  { glDeleteTextures(1, &cascade_input_);  cascade_input_ = 0; }
    if (cascade_output_) { glDeleteTextures(1, &cascade_output_); cascade_output_ = 0; }
    if (display_texture_){ glDeleteTextures(1, &display_texture_);display_texture_ = 0; }
  }

  static GLuint compileCompute_(const char* src) {
    GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(cs, 1, &src, nullptr);
    glCompileShader(cs);
    GLint ok = GL_FALSE;
    glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[4096];
      glGetShaderInfoLog(cs, 4096, nullptr, log);
      std::cerr << "Compute shader compile error:\n" << log << std::endl;
      glDeleteShader(cs);
      return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, cs);
    glLinkProgram(prog);
    glDeleteShader(cs);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
      char log[4096];
      glGetProgramInfoLog(prog, 4096, nullptr, log);
      std::cerr << "Compute program link error:\n" << log << std::endl;
      glDeleteProgram(prog);
      return 0;
    }
    return prog;
  }

  void ensureTextures_(int w, int h) {
    if (width_ == w && height_ == h &&
        scene_texture_ != 0 && cascade_input_ != 0 && cascade_output_ != 0)
      return;

    width_ = w; height_ = h;

    // Linear-space RGBA32F for scene and cascades
    ensureTexture2D(scene_texture_,   w, h, GL_RGBA32F, GL_NEAREST, GL_NEAREST);
    ensureTexture2D(cascade_input_,   w, h, GL_RGBA32F, GL_NEAREST, GL_NEAREST);  // ping
    ensureTexture2D(cascade_output_,  w, h, GL_RGBA32F, GL_NEAREST, GL_NEAREST);  // pong

    // display_texture_ is ensured on blit
  }

  void run_cascade_pass(int baseProbeSize, float baseIntervalLength, int cascadeIndex, const glm::ivec2& res) {
    glUseProgram(rc_program_);

    // Uniforms
    glUniform1i(glGetUniformLocation(rc_program_, "cascadeIndex"), cascadeIndex);
    glUniform1i(glGetUniformLocation(rc_program_, "baseProbeSize"), baseProbeSize);
    glUniform1f(glGetUniformLocation(rc_program_, "baseIntervalLength"), baseIntervalLength);
    glUniform2f(glGetUniformLocation(rc_program_, "resolution"), float(res.x), float(res.y));

    // Sampler bindings
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_texture_);
    glUniform1i(glGetUniformLocation(rc_program_, "sceneTex"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, cascade_input_);
    glUniform1i(glGetUniformLocation(rc_program_, "cascadeInputTex"), 1);

    // Output image (writeonly)
    glBindImageTexture(2, cascade_output_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    // Dispatch (workgroup size chosen independent of ray step schedule)
    GLuint gx = (GLuint)((res.x + 15) / 16);
    GLuint gy = (GLuint)((res.y + 15) / 16);
    glDispatchCompute(gx, gy, 1);

    // Ping-pong swap: next pass will sample 'cascade_input_' (previous output)
    std::swap(cascade_input_, cascade_output_);
  }

  void run_blit_to_display(const glm::ivec2& res) {
    if (!blit_program_) blit_program_ = compileCompute_(blitCS_());
    glUseProgram(blit_program_);

    // Source: final RC in cascade_input_ (RGBA32F linear)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cascade_input_);
    glUniform1i(glGetUniformLocation(blit_program_, "src"), 0);
    glUniform2f(glGetUniformLocation(blit_program_, "resolution"), float(res.x), float(res.y));

    // Destination: RGBA8 display texture
    glBindImageTexture(1, display_texture_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    GLuint gx = (GLuint)((res.x + 15) / 16);
    GLuint gy = (GLuint)((res.y + 15) / 16);
    glDispatchCompute(gx, gy, 1);
  }

  // ----------------------------
  // RC compute shader (sampler2D inputs, imageStore output)
  // Intermediates remain linear; no OETF here (done in blitCS_).
  // ----------------------------
  static const char* rcCS_() {
    return R"(
#version 430
layout(local_size_x = 16, local_size_y = 16) in;

// Inputs via sampler2D to leverage texture cache (linear RGBA32F)
uniform sampler2D sceneTex;        // texture unit 0
uniform sampler2D cascadeInputTex; // texture unit 1

// Output via imageStore
layout(binding = 2, rgba32f) uniform writeonly image2D cascadeOutput;

uniform int   cascadeIndex;
uniform int   baseProbeSize;
uniform float baseIntervalLength;
uniform vec2  resolution;

vec2 getIntervalRange(int cascadeIdx, float baseLength) {
  float scaleCurrent = (cascadeIdx <= 0) ? 0.0 : float(1 << (2 * cascadeIdx));
  float scaleNext    = float(1 << (2 * (cascadeIdx + 1)));
  return baseLength * vec2(scaleCurrent, scaleNext);
}

vec4 castIntervalLinear(vec2 intervalStart, vec2 intervalEnd, int cascadeIdx) {
  vec2 dir = intervalEnd - intervalStart;

  // Reference step schedule
  int steps = 32 << cascadeIdx;

  vec2 stepSize = dir / float(steps);

  vec3 rad = vec3(0.0);
  float T  = 1.0;
  vec2 coord = intervalStart;

  for (int i = 0; i < steps && T > 0.001; ++i) {
    ivec2 ic = ivec2(coord);
    if (ic.x >= 0 && ic.x < int(resolution.x) && ic.y >= 0 && ic.y < int(resolution.y)) {
      vec4 s = texelFetch(sceneTex, ic, 0); // linear RGBA
      rad += s.rgb * (T * s.a);
      T   *= (1.0 - s.a);
    }
    coord += stepSize;
  }
  return vec4(rad, T);
}

vec4 mergeIntervals(vec4 nearV, vec4 farV) {
  return vec4(nearV.rgb + farV.rgb * nearV.a, nearV.a * farV.a);
}

vec4 bilinearWeights(vec2 ratio) {
  return vec4(
    (1.0 - ratio.x) * (1.0 - ratio.y),
     ratio.x * (1.0 - ratio.y),
    (1.0 - ratio.x) *  ratio.y,
     ratio.x *  ratio.y
  );
}

ivec2 bilinearOffset(int idx) { return ivec2(idx & 1, idx >> 1); }

void main() {
  ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
  if (pixelCoord.x >= int(resolution.x) || pixelCoord.y >= int(resolution.y)) return;

  // Probe geometry
  int probeSize         = baseProbeSize << cascadeIndex;
  int bilinearProbeSize = baseProbeSize << (cascadeIndex + 1);
  ivec2 dirCoord    = ivec2(pixelCoord.x % probeSize, pixelCoord.y % probeSize);
  ivec2 probeIndex  = pixelCoord / probeSize;
  vec2  probeCenter = vec2(probeIndex) + 0.5;
  vec2  probePosition = probeCenter * float(probeSize);

  int   dirIndex = dirCoord.x + dirCoord.y * probeSize;
  int   dirCount = probeSize * probeSize;

  // Direction
  const float TWO_PI = 6.283185307179586;
  float angle = TWO_PI * ( (float(dirIndex) + 0.5) / float(dirCount) );
  vec2  dir   = vec2(cos(angle), sin(angle));

  // Destination interval
  vec2 range = getIntervalRange(cascadeIndex, baseIntervalLength);
  vec4 destInterval = castIntervalLinear(
    probePosition + dir * range.x,
    probePosition + dir * range.y,
    cascadeIndex
  );

  // Bilinear accumulation from N+1 (stored linear in cascadeInputTex)
  vec4 radiance = vec4(0.0);
  vec2 bilinearBaseCoord = (probePosition / float(bilinearProbeSize)) - vec2(0.5);
  vec2 ratio   = fract(bilinearBaseCoord);
  vec4 weights = bilinearWeights(ratio);
  ivec2 baseIndex = ivec2(floor(bilinearBaseCoord));

  for (int b = 0; b < 4; ++b) {
    ivec2 baseOff = bilinearOffset(b);
    ivec2 bilinearIndex = baseIndex + baseOff;
    vec4 probe_contribution = vec4(0.0);

    for (int d = 0; d < 4; ++d) {
      int baseDirIndex     = dirIndex * 4;
      int bilinearDirIndex = baseDirIndex + d;

      ivec2 bilinearDirCoord = ivec2(
        bilinearDirIndex % bilinearProbeSize,
        bilinearDirIndex / bilinearProbeSize
      );

      vec2 bilinearOff = vec2(bilinearIndex * bilinearProbeSize);
      bilinearOff = clamp(bilinearOff, vec2(0.5), resolution - float(bilinearProbeSize));
      ivec2 bilinearTexel = ivec2(bilinearOff) + bilinearDirCoord;

      vec4 bilinearInterval = texelFetch(cascadeInputTex, bilinearTexel, 0); // linear
      probe_contribution += mergeIntervals(destInterval, bilinearInterval) * weights[b];
    }

    radiance += probe_contribution * 0.25;
  }

  // Keep linear; sRGB encode happens in blitCS_
  imageStore(cascadeOutput, pixelCoord, radiance);
}
    )";
  }

  // ----------------------------
  // Blit compute shader (linear RGBA32F -> sRGB RGBA8) for display
  // ----------------------------
  static const char* blitCS_() {
    return R"(
#version 430
layout(local_size_x = 16, local_size_y = 16) in;

uniform sampler2D src; // linear RGBA32F
uniform vec2 resolution;
layout(binding = 1, rgba8) uniform writeonly image2D dst;

vec3 sRGBTransferOETF(vec3 v){
  v = max(v, vec3(0.0));
  bvec3 le = lessThanEqual(v, vec3(0.0031308));
  vec3 a = pow(v, vec3(1.0/2.4)) * 1.055 - vec3(0.055);
  vec3 b = v * 12.92;
  return mix(a, b, vec3(le));
}

void main(){
  ivec2 p = ivec2(gl_GlobalInvocationID.xy);
  if (p.x >= int(resolution.x) || p.y >= int(resolution.y)) return;
  vec4 c = texelFetch(src, p, 0);    // linear radiance
  vec3 srgb = sRGBTransferOETF(c.rgb);
  imageStore(dst, p, vec4(srgb, 1.0));
}
    )";
  }
};
