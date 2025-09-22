#pragma once

#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <string>
#include <iostream>
#include "texture.hpp"

inline glm::vec4 sRGBTransferEOTF(glm::vec4 v) {
  glm::vec3 rgb = glm::vec3(v.x, v.y, v.z);
  glm::bvec3 le = glm::lessThanEqual(rgb, glm::vec3(0.04045f));
  glm::vec3 a = glm::pow(rgb * 0.9478672986f + glm::vec3(0.0521327014f), glm::vec3(2.4f));
  glm::vec3 b = rgb * 0.0773993808f;
  glm::vec3 out_rgb = glm::mix(a, b, glm::vec3(le));
  return glm::vec4(out_rgb, v.a);
}

inline glm::vec4 sRGBTransferOETF(glm::vec4 v) {
  glm::vec3 rgb = glm::vec3(v.x, v.y, v.z);
  glm::bvec3 le = glm::lessThanEqual(rgb, glm::vec3(0.0031308f));
  glm::vec3 a = glm::pow(rgb, glm::vec3(1.0f/2.4f)) * 1.055f - glm::vec3(0.055f);
  glm::vec3 b = rgb * 12.92f;
  glm::vec3 out_rgb = glm::mix(a, b, glm::vec3(le));
  return glm::vec4(out_rgb, v.a);
}

inline glm::vec2 getIntervalRange(int cascadeIndex, float baseIntervalLength) {
  // Interval scaling: 4x branching => 2^(2n) scale
  float scaleCurrent = (cascadeIndex <= 0) ? 0.0f : float(1 << (2 * cascadeIndex));
  float scaleNext = float(1 << (2 * (cascadeIndex + 1)));
  return baseIntervalLength * glm::vec2(scaleCurrent, scaleNext);
}

// Ray-march interval against scene alpha with front-to-back composition
inline glm::vec4 castInterval(
  const Texture2D& scene,
  glm::vec2 intervalStart,
  glm::vec2 intervalEnd,
  int cascadeIndex) {
  glm::vec2 dir = intervalEnd - intervalStart;
  int steps = (32 << cascadeIndex);
  glm::vec2 stepSize = dir / float(steps);
  glm::vec3 rad(0.0f);
  float T = 1.0f;
  for (int i = 0; i < steps; ++i) {
    glm::vec2 coord = intervalStart + (stepSize * float(i));
    glm::ivec2 ic = glm::ivec2(coord);
    glm::vec4 s = scene.texelFetchClamp(ic);
    glm::vec3 s_rgb = glm::vec3(s.x, s.y, s.z);
    rad += s_rgb * (T * s.a);
    T *= (1.0f - s.a);
  }
  return glm::vec4(rad, T);
}

// Merge intervals (near occludes far via transmittance)
inline glm::vec4 mergeIntervals(glm::vec4 nearV, glm::vec4 farV) {
  glm::vec3 near_rgb = glm::vec3(nearV.x, nearV.y, nearV.z);
  glm::vec3 far_rgb = glm::vec3(farV.x, farV.y, farV.z);
  return glm::vec4(near_rgb + far_rgb * nearV.a, nearV.a * farV.a);
}

inline glm::vec4 bilinearWeights(glm::vec2 ratio) {
  return glm::vec4(
    (1.0f - ratio.x) * (1.0f - ratio.y),
    ratio.x * (1.0f - ratio.y),
    (1.0f - ratio.x) * ratio.y,
    ratio.x * ratio.y
  );
}

inline glm::ivec2 bilinearOffset(int idx) {
  return glm::ivec2(idx % 2, idx / 2);
}

// CPU implementation with OpenMP threading if available
inline void run_rc_pass_cpu(
  int baseProbeSize,
  float baseIntervalLength,
  int cascadeIndex,
  glm::vec2 resolution,
  const Texture2D& sceneTexture,
  const Texture2D& cascadeTextureNplus,
  Texture2D& outTextureN
) {
  const int W = int(resolution.x), H = int(resolution.y);
  const int probeSize = (baseProbeSize << cascadeIndex);
  const int bilinearProbeSize = (baseProbeSize << (cascadeIndex + 1));
  
  #ifdef _OPENMP
  #pragma omp parallel for schedule(dynamic, 32)
  #endif
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      glm::ivec2 pixelCoord(x, y);
      glm::ivec2 dirCoord = pixelCoord % probeSize;

      // Probe center and position for this fragment
      glm::ivec2 probeIndex = pixelCoord / probeSize;
      glm::vec2 probeCenter = glm::vec2(probeIndex) + 0.5f;
      glm::vec2 probePosition = probeCenter * float(probeSize);

      // Direction index within probe
      int dirIndex = dirCoord.x + dirCoord.y * probeSize;
      int dirCount = probeSize * probeSize;
      float angle = 2.0f * glm::pi<float>() * ((float(dirIndex) + 0.5f) / float(dirCount));
      glm::vec2 dir = glm::vec2(glm::cos(angle), glm::sin(angle));

      // Interval casting for cascade N
      glm::vec2 range = getIntervalRange(cascadeIndex, baseIntervalLength);
      glm::vec4 destInterval = castInterval(
        sceneTexture,
        probePosition + dir * range.x,
        probePosition + dir * range.y,
        cascadeIndex
      );

      glm::vec4 radiance(0.0f);

      // Base continuous coordinate in N+1 probe grid
      glm::vec2 bilinearBaseCoord = (probePosition / float(bilinearProbeSize)) - glm::vec2(0.5f, 0.5f);
      glm::vec2 ratio = glm::fract(bilinearBaseCoord);
      glm::vec4 weights = bilinearWeights(ratio);
      glm::ivec2 baseIndex = glm::ivec2(glm::floor(bilinearBaseCoord));

      // For each of the 4 bilinear probes
      for (int b = 0; b < 4; ++b) {
        glm::ivec2 baseOff = bilinearOffset(b);
        glm::ivec2 bilinearIndex = baseIndex + baseOff;
        glm::vec4 probe_contribution(0.0f);

        // For the 4 directions contributed by N+1 per N dir (4x branching)
        for (int d = 0; d < 4; ++d) {
          int baseDirIndex = dirIndex * 4;
          int bilinearDirIndex = baseDirIndex + d;
          glm::ivec2 bilinearDirCoord(
            bilinearDirIndex % bilinearProbeSize,
            bilinearDirIndex / bilinearProbeSize
          );

          // Clamp probe offset to texture boundaries
          glm::vec2 bilinearOffset = glm::vec2(bilinearIndex * bilinearProbeSize);
          bilinearOffset = glm::clamp(
            bilinearOffset,
            glm::vec2(0.5f, 0.5f),
            resolution - float(bilinearProbeSize)
          );
          glm::ivec2 bilinearTexel = glm::ivec2(bilinearOffset) + bilinearDirCoord;

          // Fetch N+1 interval, decode from sRGB to linear RGB
          glm::vec4 bilinearInterval = cascadeTextureNplus.texelFetchClamp(bilinearTexel);
          bilinearInterval = sRGBTransferEOTF(bilinearInterval);

          // Merge with dest interval, accumulate for this probe
          float w = weights[b];
          probe_contribution += mergeIntervals(destInterval, bilinearInterval) * w;
        }

        // Apply bilinear weight and accumulate to final radiance
        radiance += probe_contribution * 0.25f;
      }

      // Encode back to sRGB
      glm::vec4 outColor = sRGBTransferOETF(radiance);
      outTextureN.set({x, y}, outColor);
    }
  }
}

// GPU implementation using GLEW and compute shaders
class RCGPURenderer {
private:
  GLuint compute_program;
  GLuint scene_texture, cascade_input, cascade_output;
  int current_width, current_height;
  bool gpu_available;

  const std::string compute_shader_source = R"(
#version 430

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba32f) uniform readonly image2D sceneImage;
layout(binding = 1, rgba32f) uniform readonly image2D cascadeInput;  
layout(binding = 2, rgba32f) uniform writeonly image2D cascadeOutput;

uniform int cascadeIndex;
uniform int baseProbeSize;
uniform float baseIntervalLength;
uniform vec2 resolution;

// sRGB transfer functions
vec3 sRGBTransferEOTF(vec3 v) {
  bvec3 le = lessThanEqual(v, vec3(0.04045));
  vec3 a = pow(v * 0.9478672986 + vec3(0.0521327014), vec3(2.4));
  vec3 b = v * 0.0773993808;
  return mix(a, b, vec3(le));
}

vec3 sRGBTransferOETF(vec3 v) {
  bvec3 le = lessThanEqual(v, vec3(0.0031308));
  vec3 a = pow(v, vec3(0.41666)) * 1.055 - vec3(0.055);
  vec3 b = v * 12.92;
  return mix(a, b, vec3(le));
}

vec2 getIntervalRange(int cascadeIdx, float baseLength) {
  float scaleCurrent = (cascadeIdx <= 0) ? 0.0 : float(1 << (2 * cascadeIdx));
  float scaleNext = float(1 << (2 * (cascadeIdx + 1)));
  return baseLength * vec2(scaleCurrent, scaleNext);
}

vec4 castInterval(vec2 intervalStart, vec2 intervalEnd, int cascadeIdx) {
  vec2 dir = intervalEnd - intervalStart;
  int steps = 32 << cascadeIdx;
  vec2 stepSize = dir / float(steps);
  
  vec3 rad = vec3(0.0);
  float T = 1.0;
  
  vec2 coord = intervalStart;
  for (int i = 0; i < steps && T > 0.001; ++i) {
    ivec2 ic = ivec2(coord);
    if (ic.x >= 0 && ic.x < int(resolution.x) && ic.y >= 0 && ic.y < int(resolution.y)) {
      vec4 s = imageLoad(sceneImage, ic);
      rad += s.rgb * (T * s.a);
      T *= (1.0 - s.a);
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
    (1.0 - ratio.x) * ratio.y,
    ratio.x * ratio.y
  );
}

ivec2 bilinearOffset(int idx) {
  return ivec2(idx & 1, idx >> 1);
}

void main() {
  ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
  if (pixelCoord.x >= int(resolution.x) || pixelCoord.y >= int(resolution.y)) return;
  
  int probeSize = baseProbeSize << cascadeIndex;
  int bilinearProbeSize = baseProbeSize << (cascadeIndex + 1);
  
  ivec2 dirCoord = pixelCoord % probeSize;
  ivec2 probeIndex = pixelCoord / probeSize;
  vec2 probeCenter = vec2(probeIndex) + 0.5;
  vec2 probePosition = probeCenter * float(probeSize);
  
  int dirIndex = dirCoord.x + dirCoord.y * probeSize;
  int dirCount = probeSize * probeSize;
  float angle = 2.0 * 3.14159265 * (float(dirIndex) + 0.5) / float(dirCount);
  vec2 dir = vec2(cos(angle), sin(angle));
  
  vec2 range = getIntervalRange(cascadeIndex, baseIntervalLength);
  vec4 destInterval = castInterval(
    probePosition + dir * range.x,
    probePosition + dir * range.y,
    cascadeIndex
  );
  
  vec4 radiance = vec4(0.0);
  vec2 bilinearBaseCoord = (probePosition / float(bilinearProbeSize)) - vec2(0.5);
  vec2 ratio = fract(bilinearBaseCoord);
  vec4 weights = bilinearWeights(ratio);
  ivec2 baseIndex = ivec2(floor(bilinearBaseCoord));
  
  for (int b = 0; b < 4; ++b) {
    ivec2 baseOff = bilinearOffset(b);
    ivec2 bilinearIndex = baseIndex + baseOff;
    vec4 probe_contribution = vec4(0.0);
    
    for (int d = 0; d < 4; ++d) {
      int baseDirIndex = dirIndex * 4;
      int bilinearDirIndex = baseDirIndex + d;
      ivec2 bilinearDirCoord = ivec2(
        bilinearDirIndex % bilinearProbeSize,
        bilinearDirIndex / bilinearProbeSize
      );
      
      vec2 bilinearOffset = vec2(bilinearIndex * bilinearProbeSize);
      bilinearOffset = clamp(
        bilinearOffset,
        vec2(0.5),
        resolution - float(bilinearProbeSize)
      );
      ivec2 bilinearTexel = ivec2(bilinearOffset) + bilinearDirCoord;
      
      vec4 bilinearInterval = imageLoad(cascadeInput, bilinearTexel);
      bilinearInterval.rgb = sRGBTransferEOTF(bilinearInterval.rgb);
      
      probe_contribution += mergeIntervals(destInterval, bilinearInterval) * weights[b];
    }
    
    radiance += probe_contribution * 0.25;
  }
  
  vec4 outColor = vec4(sRGBTransferOETF(radiance.rgb), radiance.a);
  imageStore(cascadeOutput, pixelCoord, outColor);
}
)";

public:
  RCGPURenderer() : compute_program(0), scene_texture(0), cascade_input(0), 
                    cascade_output(0), current_width(0), current_height(0), 
                    gpu_available(false) {}
  
  ~RCGPURenderer() {
    cleanup();
  }
  
  bool initialize() {
    // Check if compute shaders are supported
    if (!GLEW_VERSION_4_3 && !GLEW_ARB_compute_shader) {
      std::cout << "Compute shaders not supported, using CPU fallback" << std::endl;
      gpu_available = false;
      return false;
    }
    
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    
    // Compile compute shader
    compute_program = compile_compute_shader(compute_shader_source);
    if (compute_program == 0) {
      std::cout << "Failed to compile compute shader, using CPU fallback" << std::endl;
      gpu_available = false;
      return false;
    }
    
    gpu_available = true;
    std::cout << "GPU RC renderer initialized successfully" << std::endl;
    return true;
  }
  
  void run_rc_pass(
    int baseProbeSize,
    float baseIntervalLength,
    int cascadeIndex,
    glm::vec2 resolution,
    const Texture2D& sceneTexture,
    const Texture2D& cascadeTextureNplus,
    Texture2D& outTextureN
  ) {
    if (!gpu_available) {
      // Use CPU fallback
      run_rc_pass_cpu(baseProbeSize, baseIntervalLength, cascadeIndex,
          resolution, sceneTexture, cascadeTextureNplus, outTextureN);
      return;
    }
    
    // GPU implementation
    setup_textures(int(resolution.x), int(resolution.y));
    upload_scene(sceneTexture);
    upload_cascade(cascadeTextureNplus);
    
    run_cascade_pass(baseProbeSize, baseIntervalLength, cascadeIndex, resolution);
    
    download_result(outTextureN);
  }

private:
  void setup_textures(int width, int height) {
    if (current_width == width && current_height == height && scene_texture != 0) {
      return; // Already set up
    }
    
    cleanup_textures();
    
    current_width = width;
    current_height = height;
    
    // Create GPU textures
    glGenTextures(1, &scene_texture);
    glBindTexture(GL_TEXTURE_2D, scene_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    glGenTextures(1, &cascade_input);
    glBindTexture(GL_TEXTURE_2D, cascade_input);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    glGenTextures(1, &cascade_output);
    glBindTexture(GL_TEXTURE_2D, cascade_output);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  }
  
  void upload_scene(const Texture2D& scene) {
    setup_textures(scene.width(), scene.height());
    
    // Convert to float array
    std::vector<float> float_data(scene.width() * scene.height() * 4);
    for (int y = 0; y < scene.height(); ++y) {
      for (int x = 0; x < scene.width(); ++x) {
        glm::vec4 pixel = scene.get({x, y});
        int idx = (y * scene.width() + x) * 4;
        float_data[idx + 0] = pixel.r;
        float_data[idx + 1] = pixel.g;
        float_data[idx + 2] = pixel.b;
        float_data[idx + 3] = pixel.a;
      }
    }
    
    glBindTexture(GL_TEXTURE_2D, scene_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, scene.width(), scene.height(), 
             GL_RGBA, GL_FLOAT, float_data.data());
  }
  
  void upload_cascade(const Texture2D& cascade) {
    std::vector<float> float_data(cascade.width() * cascade.height() * 4);
    for (int y = 0; y < cascade.height(); ++y) {
      for (int x = 0; x < cascade.width(); ++x) {
        glm::vec4 pixel = cascade.get({x, y});
        int idx = (y * cascade.width() + x) * 4;
        float_data[idx + 0] = pixel.r;
        float_data[idx + 1] = pixel.g;
        float_data[idx + 2] = pixel.b;
        float_data[idx + 3] = pixel.a;
      }
    }
    
    glBindTexture(GL_TEXTURE_2D, cascade_input);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cascade.width(), cascade.height(),
             GL_RGBA, GL_FLOAT, float_data.data());
  }
  
  void run_cascade_pass(int baseProbeSize, float baseIntervalLength, int cascadeIndex, 
             glm::vec2 resolution) {
    glUseProgram(compute_program);
    
    // Set uniforms
    glUniform1i(glGetUniformLocation(compute_program, "cascadeIndex"), cascadeIndex);
    glUniform1i(glGetUniformLocation(compute_program, "baseProbeSize"), baseProbeSize);
    glUniform1f(glGetUniformLocation(compute_program, "baseIntervalLength"), baseIntervalLength);
    glUniform2f(glGetUniformLocation(compute_program, "resolution"), resolution.x, resolution.y);
    
    // Bind textures as images
    glBindImageTexture(0, scene_texture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, cascade_input, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(2, cascade_output, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    
    // Dispatch compute shader
    int group_x = (int(resolution.x) + 15) / 16;
    int group_y = (int(resolution.y) + 15) / 16;
    glDispatchCompute(group_x, group_y, 1);
    
    // Memory barrier to ensure writes are complete
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    
    // Swap input and output for next pass
    std::swap(cascade_input, cascade_output);
  }
  
  void download_result(Texture2D& result) {
    std::vector<float> float_data(current_width * current_height * 4);
    
    glBindTexture(GL_TEXTURE_2D, cascade_input); // Result is in input after swap
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, float_data.data());
    
    for (int y = 0; y < current_height; ++y) {
      for (int x = 0; x < current_width; ++x) {
        int idx = (y * current_width + x) * 4;
        glm::vec4 pixel(
          float_data[idx + 0],
          float_data[idx + 1], 
          float_data[idx + 2],
          float_data[idx + 3]
        );
        result.set({x, y}, pixel);
      }
    }
  }
  
  GLuint compile_compute_shader(const std::string& source) {
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
      char log[1024];
      glGetShaderInfoLog(shader, 1024, nullptr, log);
      std::cout << "Compute shader compile error: " << log << std::endl;
      glDeleteShader(shader);
      return 0;
    }
    
    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
      char log[1024];
      glGetProgramInfoLog(program, 1024, nullptr, log);
      std::cout << "Program link error: " << log << std::endl;
      glDeleteProgram(program);
      glDeleteShader(shader);
      return 0;
    }
    
    glDeleteShader(shader);
    return program;
  }
  
  void cleanup_textures() {
    if (scene_texture) { glDeleteTextures(1, &scene_texture); scene_texture = 0; }
    if (cascade_input) { glDeleteTextures(1, &cascade_input); cascade_input = 0; }
    if (cascade_output) { glDeleteTextures(1, &cascade_output); cascade_output = 0; }
  }
  
  void cleanup() {
    cleanup_textures();
    if (compute_program) { glDeleteProgram(compute_program); compute_program = 0; }
  }
};

// Global GPU renderer instance
static RCGPURenderer g_gpu_renderer;

// Wrapper function to use GPU renderer
inline void run_rc_pass(
  int baseProbeSize,
  float baseIntervalLength,
  int cascadeIndex,
  glm::vec2 resolution,
  const Texture2D& sceneTexture,
  const Texture2D& cascadeTextureNplus,
  Texture2D& outTextureN
) {
  g_gpu_renderer.run_rc_pass(baseProbeSize, baseIntervalLength, cascadeIndex,
                resolution, sceneTexture, cascadeTextureNplus, outTextureN);
}
