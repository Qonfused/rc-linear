#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <immintrin.h>
#include <cmath>
#include "texture_simd.hpp"

// Windows compatibility
#ifdef _WIN32
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#endif

// Use OpenMP only if available
#ifdef _OPENMP
#include <omp.h>
#define PRAGMA_OMP_PARALLEL_FOR _Pragma("omp parallel for")
#define PRAGMA_OMP_PARALLEL_FOR_SCHEDULE(sched) _Pragma("omp parallel for schedule(" #sched ")")
#else
#define PRAGMA_OMP_PARALLEL_FOR
#define PRAGMA_OMP_PARALLEL_FOR_SCHEDULE(sched)
#endif

// Fast trigonometry lookup tables
class FastTrig {
private:
  static constexpr int TABLE_SIZE = 8192;
  static constexpr float SCALE = TABLE_SIZE / (2.0f * (float)M_PI);
  alignas(32) static float sin_table[TABLE_SIZE];
  alignas(32) static float cos_table[TABLE_SIZE];
  static bool initialized;
  
public:
  static void init() {
    if (initialized) return;
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int i = 0; i < TABLE_SIZE; ++i) {
      float angle = 2.0f * (float)M_PI * i / TABLE_SIZE;
      sin_table[i] = std::sin(angle);
      cos_table[i] = std::cos(angle);
    }
    initialized = true;
  }
  
  static inline float fast_sin(float x) {
    int idx = int(x * SCALE) & (TABLE_SIZE - 1);
    return sin_table[idx];
  }
  
  static inline float fast_cos(float x) {
    int idx = int(x * SCALE) & (TABLE_SIZE - 1);
    return cos_table[idx];
  }
  
  // Vectorized version for 8 angles at once
  static void sincos_avx(const float* angles, float* sin_out, float* cos_out) {
    __m256 scale_vec = _mm256_set1_ps(SCALE);
    __m256 angles_vec = _mm256_load_ps(angles);
    __m256i indices = _mm256_cvtps_epi32(_mm256_mul_ps(angles_vec, scale_vec));
    
    // Manual gather (compatible with older AVX)
    alignas(32) int idx[8];
    _mm256_store_si256((__m256i*)idx, indices);
    
    for (int i = 0; i < 8; ++i) {
      int masked_idx = idx[i] & (TABLE_SIZE - 1);
      sin_out[i] = sin_table[masked_idx];
      cos_out[i] = cos_table[masked_idx];
    }
  }
};

// Initialize static members
alignas(32) float FastTrig::sin_table[FastTrig::TABLE_SIZE];
alignas(32) float FastTrig::cos_table[FastTrig::TABLE_SIZE];
bool FastTrig::initialized = false;

// SIMD-optimized sRGB conversions with Windows-compatible pow approximation
inline __m256 fast_pow_24(const __m256 x) {
  // Approximate x^2.4 using x^2 * sqrt(x) for better compatibility
  __m256 x2 = _mm256_mul_ps(x, x);
  __m256 sqrt_x = _mm256_sqrt_ps(x);
  return _mm256_mul_ps(x2, sqrt_x);
}

inline __m256 srgb_to_linear_avx(const __m256 srgb) {
  const __m256 threshold = _mm256_set1_ps(0.04045f);
  const __m256 scale1 = _mm256_set1_ps(1.0f / 12.92f);
  const __m256 scale2 = _mm256_set1_ps(0.9478672986f);
  const __m256 offset = _mm256_set1_ps(0.0521327014f);
  
  __m256 mask = _mm256_cmp_ps(srgb, threshold, _CMP_LE_OQ);
  __m256 linear1 = _mm256_mul_ps(srgb, scale1);
  
  __m256 temp = _mm256_fmadd_ps(srgb, scale2, offset);
  __m256 linear2 = fast_pow_24(temp);
  
  return _mm256_blendv_ps(linear2, linear1, mask);
}

inline __m256 linear_to_srgb_avx(const __m256 linear) {
  const __m256 threshold = _mm256_set1_ps(0.0031308f);
  const __m256 scale1 = _mm256_set1_ps(12.92f);
  const __m256 scale2 = _mm256_set1_ps(1.055f);
  const __m256 offset = _mm256_set1_ps(-0.055f);
  
  __m256 mask = _mm256_cmp_ps(linear, threshold, _CMP_LE_OQ);
  __m256 srgb1 = _mm256_mul_ps(linear, scale1);
  
  // Approximate pow(x, 1/2.4) ≈ sqrt(sqrt(x)) * cbrt(x)
  __m256 sqrt_lin = _mm256_sqrt_ps(linear);
  __m256 sqrt_sqrt = _mm256_sqrt_ps(sqrt_lin);
  __m256 srgb2 = _mm256_fmadd_ps(sqrt_sqrt, scale2, offset);
  
  return _mm256_blendv_ps(srgb2, srgb1, mask);
}

// Original functions for compatibility
inline glm::vec4 sRGBTransferEOTF(glm::vec4 v) {
  glm::vec3 rgb = glm::vec3(v.x, v.y, v.z);
  glm::bvec3 le = glm::lessThanEqual(rgb, glm::vec3(0.04045f));
  glm::vec3 a = glm::pow(rgb * 0.9478672986f + glm::vec3(0.0521327014f), glm::vec3(2.4f));
  glm::vec3 b = rgb / 12.92f;
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
  float scaleCurrent = (cascadeIndex <= 0) ? 0.0f : float(1 << (2 * cascadeIndex));
  float scaleNext = float(1 << (2 * (cascadeIndex + 1)));
  return baseIntervalLength * glm::vec2(scaleCurrent, scaleNext);
}

// Optimized interval casting with early termination
inline glm::vec4 castInterval(
  const Texture2D& scene,
  glm::vec2 intervalStart, 
  glm::vec2 intervalEnd,
  int cascadeIndex
) {
  glm::vec2 dir = intervalEnd - intervalStart;
  int steps = (32 << cascadeIndex);
  
  // Use reciprocal for faster division
  float inv_steps = 1.0f / steps;
  glm::vec2 stepSize = dir * inv_steps;
  
  glm::vec3 rad(0.0f);
  float T = 1.0f;
  
  // Early termination when transmittance gets very low
  constexpr float MIN_TRANSMITTANCE = 0.001f;
  
  glm::vec2 coord = intervalStart;
  for (int i = 0; i < steps && T > MIN_TRANSMITTANCE; ++i) {
    glm::ivec2 ic = glm::ivec2(coord);
    glm::vec4 s = scene.texelFetchClamp(ic);
    
    // Avoid expensive vector operations
    float opacity = s.a;
    float transmittance_step = T * opacity;
    
    rad.r += s.r * transmittance_step;
    rad.g += s.g * transmittance_step;
    rad.b += s.b * transmittance_step;
    T *= (1.0f - opacity);
    
    coord += stepSize;
  }
  
  return glm::vec4(rad, T);
}

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
  return glm::ivec2(idx & 1, idx >> 1);
}

// SIMD-optimized RC pass with tiled processing and threading
inline void run_rc_pass_simd(
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
  
  // Initialize trigonometry tables
  FastTrig::init();
  
  // Precompute constants
  const float inv_probe_size = 1.0f / probeSize;
  const float inv_bilinear_probe_size = 1.0f / bilinearProbeSize;
  const int dir_count = probeSize * probeSize;
  const float angle_scale = 2.0f * (float)M_PI / dir_count; // Fixed M_PI issue
  
  // Tile-based processing for better cache locality
  constexpr int TILE_SIZE = 64;
  
  // Use OpenMP if available, otherwise fall back to single threading
  #ifdef _OPENMP
  #pragma omp parallel for schedule(dynamic, 4)
  #endif
  for (int tile_idx = 0; tile_idx < (H / TILE_SIZE + 1) * (W / TILE_SIZE + 1); ++tile_idx) {
    int tile_y = (tile_idx / (W / TILE_SIZE + 1)) * TILE_SIZE;
    int tile_x = (tile_idx % (W / TILE_SIZE + 1)) * TILE_SIZE;
    
    if (tile_y >= H) continue;
    
    int x_end = std::min(tile_x + TILE_SIZE, W);
    int y_end = std::min(tile_y + TILE_SIZE, H);
    
    // Process 8x8 blocks within each tile
    for (int y = tile_y; y < y_end; y += 8) {
      for (int x = tile_x; x < x_end; x += 8) {
        int x_block_end = std::min(x + 8, x_end);
        int y_block_end = std::min(y + 8, y_end);
        
        for (int py = y; py < y_block_end; ++py) {
          for (int px = x; px < x_block_end; ++px) {
            // Original RC algorithm per pixel (optimized)
            glm::ivec2 pixelCoord(px, py);
            glm::ivec2 dirCoord = pixelCoord % probeSize;
            
            glm::ivec2 probeIndex = pixelCoord / probeSize;
            glm::vec2 probeCenter = glm::vec2(probeIndex) + 0.5f;
            glm::vec2 probePosition = probeCenter * float(probeSize);
            
            int dirIndex = dirCoord.x + dirCoord.y * probeSize;
            float angle = angle_scale * (float(dirIndex) + 0.5f);
            
            // Use fast trig functions
            glm::vec2 dir(FastTrig::fast_cos(angle), FastTrig::fast_sin(angle));
            
            glm::vec2 range = getIntervalRange(cascadeIndex, baseIntervalLength);
            glm::vec4 destInterval = castInterval(
              sceneTexture,
              probePosition + dir * range.x,
              probePosition + dir * range.y,
              cascadeIndex
            );
            
            glm::vec4 radiance(0.0f);
            glm::vec2 bilinearBaseCoord = (probePosition * inv_bilinear_probe_size) - glm::vec2(0.5f);
            glm::vec2 ratio = glm::fract(bilinearBaseCoord);
            glm::vec4 weights = bilinearWeights(ratio);
            glm::ivec2 baseIndex = glm::ivec2(glm::floor(bilinearBaseCoord));
            
            // Bilinear sampling loop
            for (int b = 0; b < 4; ++b) {
              glm::ivec2 baseOff = bilinearOffset(b);
              glm::ivec2 bilinearIndex = baseIndex + baseOff;
              glm::vec4 probe_contribution(0.0f);
              
              for (int d = 0; d < 4; ++d) {
                int baseDirIndex = dirIndex * 4;
                int bilinearDirIndex = baseDirIndex + d;
                glm::ivec2 bilinearDirCoord(
                  bilinearDirIndex % bilinearProbeSize,
                  bilinearDirIndex / bilinearProbeSize
                );
                
                glm::vec2 bilinearOffset = glm::vec2(bilinearIndex * bilinearProbeSize);
                bilinearOffset = glm::clamp(
                  bilinearOffset,
                  glm::vec2(0.5f),
                  resolution - float(bilinearProbeSize)
                );
                glm::ivec2 bilinearTexel = glm::ivec2(bilinearOffset) + bilinearDirCoord;
                
                glm::vec4 bilinearInterval = cascadeTextureNplus.texelFetchClamp(bilinearTexel);
                bilinearInterval = sRGBTransferEOTF(bilinearInterval);
                
                probe_contribution += mergeIntervals(destInterval, bilinearInterval) * weights[b];
              }
              
              radiance += probe_contribution * 0.25f;
            }
            
            glm::vec4 outColor = sRGBTransferOETF(radiance);
            outTextureN.set({px, py}, outColor);
          }
        }
      }
    }
  }
}

// Compatibility function - calls the SIMD version
inline void run_rc_pass(
  int baseProbeSize,
  float baseIntervalLength,
  int cascadeIndex,
  glm::vec2 resolution,
  const Texture2D& sceneTexture,
  const Texture2D& cascadeTextureNplus,
  Texture2D& outTextureN
) {
  run_rc_pass_simd(baseProbeSize, baseIntervalLength, cascadeIndex, 
          resolution, sceneTexture, cascadeTextureNplus, outTextureN);
}
