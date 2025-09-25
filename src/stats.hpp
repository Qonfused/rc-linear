#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include <iostream>

#define GLEW_STATIC
#include <GL/glew.h>
#include <glm/glm.hpp>

// Radial statistics payload used by plots/UI
struct RadialStats {
  std::vector<float> radii;
  std::vector<float> mean;
  std::vector<float> stddev;
  std::vector<int>   count;
  std::vector<float> ground_truth;
  std::vector<float> stddev_upper;
  std::vector<float> stddev_lower;
};

static inline const char* kRadialStatsCS = R"(
#version 430
layout(local_size_x = 16, local_size_y = 16) in;

// Read the rendered image directly (RGBA32F)
layout(binding=3, rgba32f) uniform readonly image2D resultImage;

// Per-radius accumulators (unsized arrays)
layout(std430, binding=0) buffer CountBuf { uint count[]; };
layout(std430, binding=1) buffer SumBuf   { uint sumQ[]; };     // fixed-point sum of luminance
layout(std430, binding=2) buffer SsqBuf   { uint sumsqQ[]; };   // fixed-point sum of squares

uniform ivec2 imgSize;
uniform vec2  center;
uniform int   maxRadius;

const float SCALE = 1048576.0; // 2^20 fixed-point scale

void main() {
  ivec2 p = ivec2(gl_GlobalInvocationID.xy);
  if (p.x >= imgSize.x || p.y >= imgSize.y) return;

  vec4 c = imageLoad(resultImage, p);
  float lum = 0.2126*c.r + 0.7152*c.g + 0.0722*c.b;

  vec2 fp = vec2(p) + vec2(0.5);
  int r = int(length(fp - center));
  if (r < 0 || r > maxRadius) return;

  atomicAdd(count[r], 1u);
  uint q  = uint(round(lum * SCALE));
  uint qq = uint(round(lum * lum * SCALE));
  atomicAdd(sumQ[r], q);
  atomicAdd(sumsqQ[r], qq);
}
)";

static inline GLuint compileCS(const char* src) {
  GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = GL_FALSE;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
    std::cerr << "Compute shader compile error:\n" << log << std::endl;
  }
  GLuint prog = glCreateProgram();
  glAttachShader(prog, sh);
  glLinkProgram(prog);
  glDeleteShader(sh);
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
    std::cerr << "Compute shader link error:\n" << log << std::endl;
  }
  return prog;
}

struct GPUBins {
  std::vector<uint32_t> count, sumQ, sumsqQ;
};

// Launch-only variant for non-blocking pipelines (no readback here).
// Callers can use a fence or a separate read-back pass on the next frame.
static inline void dispatch_radial_bins_compute(GLuint tex, int W, int H,
                                                GLuint ssbo_count, GLuint ssbo_sumQ, GLuint ssbo_sumsqQ,
                                                GLuint program /* optional precompiled */) {
  const int max_radius = int(glm::length(glm::vec2(float(W), float(H)) * 0.5f));
  GLuint prog = program ? program : compileCS(kRadialStatsCS);

  glUseProgram(prog);
  glUniform2i(glGetUniformLocation(prog, "imgSize"), W, H);
  glUniform2f(glGetUniformLocation(prog, "center"), 0.5f * float(W), 0.5f * float(H));
  glUniform1i(glGetUniformLocation(prog, "maxRadius"), max_radius);

  glBindImageTexture(3, tex, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo_count);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo_sumQ);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo_sumsqQ);

  GLuint gx = (GLuint)((W + 15) / 16);
  GLuint gy = (GLuint)((H + 15) / 16);
  glDispatchCompute(gx, gy, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

class AsyncStatsManager {
private:
  GLuint ssbo_buffers_[6] = {0}; // Double-buffered: [count0, sum0, sumsq0, count1, sum1, sumsq1]
  GLuint stats_program_ = 0;
  int active_write_buffer_ = 0;
  int max_radius_ = 0;
  bool initialized_ = false;
  
public:
  void init(int max_radius) {
    if (initialized_ && max_radius_ == max_radius) return;
    
    cleanup();
    max_radius_ = max_radius;
    const size_t bins = max_radius + 1;
    
    glGenBuffers(6, ssbo_buffers_);
    std::vector<uint32_t> zero(bins, 0u);
    
    for (int i = 0; i < 6; ++i) {
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_buffers_[i]);
      glBufferData(GL_SHADER_STORAGE_BUFFER, bins * sizeof(uint32_t), 
                  zero.data(), GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    
    if (!stats_program_) {
      stats_program_ = compileCS(kRadialStatsCS);
    }
    initialized_ = true;
  }
  
  // Launch async stats computation (no readback)
  void dispatch_async(GLuint tex, int W, int H) {
    if (!initialized_) return;
    
    int write_base = active_write_buffer_ * 3;
    
    // Clear current write buffers
    const size_t bins = max_radius_ + 1;
    std::vector<uint32_t> zero(bins, 0u);
    for (int i = 0; i < 3; ++i) {
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_buffers_[write_base + i]);
      glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bins * sizeof(uint32_t), zero.data());
    }
    
    dispatch_radial_bins_compute(tex, W, H, 
      ssbo_buffers_[write_base], ssbo_buffers_[write_base + 1], ssbo_buffers_[write_base + 2], 
      stats_program_);
    
    // Let next frame read the just-written buffer
    active_write_buffer_ = 1 - active_write_buffer_;
  }
  
  // Try to read previous frame's results (non-blocking)
  bool try_read_stats(RadialStats& out_stats, int W, int H) {
    if (!initialized_) return false;
    
    int read_base = (1 - active_write_buffer_) * 3;
    const size_t bins = max_radius_ + 1;
    
    // Read previous frame's compute results
    GPUBins bins_data;
    bins_data.count.resize(bins);
    bins_data.sumQ.resize(bins);
    bins_data.sumsqQ.resize(bins);
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_buffers_[read_base]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bins * sizeof(uint32_t), bins_data.count.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_buffers_[read_base + 1]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bins * sizeof(uint32_t), bins_data.sumQ.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_buffers_[read_base + 2]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bins * sizeof(uint32_t), bins_data.sumsqQ.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    
    // Convert to RadialStats
    out_stats = convert_bins_to_stats(bins_data, W, H);
    return true;
  }
  
  void cleanup() {
    if (initialized_) {
      glDeleteBuffers(6, ssbo_buffers_);
      memset(ssbo_buffers_, 0, sizeof(ssbo_buffers_));
      if (stats_program_) {
        glDeleteProgram(stats_program_);
        stats_program_ = 0;
      }
      initialized_ = false;
    }
  }
  
  ~AsyncStatsManager() {
    cleanup();
  }

private:
  // Convert GPU bins to RadialStats structure
  RadialStats convert_bins_to_stats(const GPUBins& bins, int W, int H) {
    const int max_radius = int(glm::length(glm::vec2(float(W), float(H)) * 0.5f));
    const float SCALE = 1048576.0f;

    std::vector<float> radii;
    std::vector<float> mean(max_radius + 1, 0.0f);
    std::vector<float> stddev(max_radius + 1, 0.0f);
    std::vector<float> stddev_upper;
    std::vector<float> stddev_lower;
    std::vector<int>   count(max_radius + 1, 0);
    std::vector<float> ground_truth(max_radius + 1, 0.0f);

    radii.reserve(max_radius + 1);
    stddev_upper.reserve(max_radius + 1);
    stddev_lower.reserve(max_radius + 1);

    for (int r = 0; r <= max_radius; ++r) {
      radii.push_back(float(r));
      uint32_t n  = bins.count[size_t(r)];
      count[r] = int(n);
      if (n > 0u) {
        float s  = float(bins.sumQ[size_t(r)]) / SCALE;
        float ss = float(bins.sumsqQ[size_t(r)]) / SCALE;
        float mu = s / float(n);
        float var = std::max(ss / float(n) - mu * mu, 0.0f);
        mean[r] = mu;
        stddev[r] = std::sqrt(var);
      }
      stddev_upper.push_back(mean[r] + stddev[r]);
      stddev_lower.push_back(std::max(mean[r] - stddev[r], 0.0f));
    }

    // Ground truth curve (plateau then inverse-square)
    float disk_radius = 15.0f;
    float peak = 1.0f;
    for (int r = 0; r <= max_radius; ++r) {
      if (r <= int(disk_radius)) {
        ground_truth[r] = peak;
      } else {
        ground_truth[r] = peak * disk_radius * disk_radius / (float(r) * float(r) + 1e-3f);
      }
    }

    return RadialStats{radii, mean, stddev, count, ground_truth, stddev_upper, stddev_lower};
  }
};

// Legacy blocking API - kept for backward compatibility but not recommended for use
static inline GPUBins compute_radial_bins_gpu(GLuint tex, int W, int H) {
  const int max_radius = int(glm::length(glm::vec2(float(W), float(H)) * 0.5f));
  const size_t bins = size_t(max_radius + 1);

  static GLuint prog = 0;
  if (prog == 0) {
    prog = compileCS(kRadialStatsCS);
  }

  // SSBOs
  GLuint ssbo[3] = {0, 0, 0};
  glGenBuffers(3, ssbo);
  std::vector<uint32_t> zero(bins, 0u);
  for (int i = 0; i < 3; ++i) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[i]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bins * sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bins * sizeof(uint32_t), zero.data());
  }
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  // Dispatch compute
  dispatch_radial_bins_compute(tex, W, H, ssbo[0], ssbo[1], ssbo[2], prog);

  // Read back small SSBOs
  GPUBins out;
  out.count.resize(bins);
  out.sumQ.resize(bins);
  out.sumsqQ.resize(bins);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[0]);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bins * sizeof(uint32_t), out.count.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[1]);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bins * sizeof(uint32_t), out.sumQ.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[2]);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bins * sizeof(uint32_t), out.sumsqQ.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  glDeleteBuffers(3, ssbo);
  return out;
}
