#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <immintrin.h>
#include <algorithm>
#include <cstring>

// SIMD-optimized texture with SOA layout for better vectorization
struct Texture2D_SIMD {
  int w, h, pitch;
  
  // Structure of Arrays for SIMD efficiency
  alignas(32) std::unique_ptr<float[]> r, g, b, a;
  
  // Fallback AOS storage for compatibility
  std::vector<glm::vec4> data_aos;
  
  Texture2D_SIMD() : w(0), h(0), pitch(0) {}
  
  Texture2D_SIMD(int W, int H) { resize(W, H); }
  
  void resize(int W, int H) {
    w = W; 
    h = H;
    pitch = (W + 7) & ~7;  // Align to 8-float boundaries for AVX
    
    size_t size = pitch * H;
    r = std::make_unique<float[]>(size);
    g = std::make_unique<float[]>(size);
    b = std::make_unique<float[]>(size);
    a = std::make_unique<float[]>(size);
    
    // Clear to zero
    std::fill(r.get(), r.get() + size, 0.0f);
    std::fill(g.get(), g.get() + size, 0.0f);
    std::fill(b.get(), b.get() + size, 0.0f);
    std::fill(a.get(), a.get() + size, 0.0f);
    
    // AOS fallback for compatibility
    data_aos.assign(W * H, glm::vec4(0.0f));
  }
  
  inline int width() const { return w; }
  inline int height() const { return h; }
  
  void clear(const glm::vec4& c) {
    size_t size = pitch * h;
    std::fill(r.get(), r.get() + size, c.r);
    std::fill(g.get(), g.get() + size, c.g);
    std::fill(b.get(), b.get() + size, c.b);
    std::fill(a.get(), a.get() + size, c.a);
    std::fill(data_aos.begin(), data_aos.end(), c);
  }
  
  inline bool in_bounds(const glm::ivec2& p) const {
    return (p.x >= 0 && p.x < w && p.y >= 0 && p.y < h);
  }
  
  // SIMD-optimized batch operations
  void get_8_horizontal(int x, int y, float* out_r, float* out_g, float* out_b, float* out_a) const {
    if (x + 8 <= w && y < h) {
      size_t idx = y * pitch + x;
      _mm256_store_ps(out_r, _mm256_load_ps(&r[idx]));
      _mm256_store_ps(out_g, _mm256_load_ps(&g[idx]));
      _mm256_store_ps(out_b, _mm256_load_ps(&b[idx]));
      _mm256_store_ps(out_a, _mm256_load_ps(&a[idx]));
    } else {
      // Fallback for boundaries
      for (int i = 0; i < 8; ++i) {
        auto val = get(glm::ivec2(x + i, y));
        out_r[i] = val.r; out_g[i] = val.g; 
        out_b[i] = val.b; out_a[i] = val.a;
      }
    }
  }
  
  void set_8_horizontal(int x, int y, const float* in_r, const float* in_g, const float* in_b, const float* in_a) {
    if (x + 8 <= w && y < h) {
      size_t idx = y * pitch + x;
      _mm256_store_ps(&r[idx], _mm256_load_ps(in_r));
      _mm256_store_ps(&g[idx], _mm256_load_ps(in_g));
      _mm256_store_ps(&b[idx], _mm256_load_ps(in_b));
      _mm256_store_ps(&a[idx], _mm256_load_ps(in_a));
      
      // Update AOS for compatibility
      for (int i = 0; i < 8 && (x + i) < w; ++i) {
        data_aos[y * w + x + i] = glm::vec4(in_r[i], in_g[i], in_b[i], in_a[i]);
      }
    } else {
      // Fallback for boundaries
      for (int i = 0; i < 8 && (x + i) < w; ++i) {
        set(glm::ivec2(x + i, y), glm::vec4(in_r[i], in_g[i], in_b[i], in_a[i]));
      }
    }
  }
  
  // Compatibility functions
  glm::vec4 texelFetchClamp(const glm::ivec2& p) const {
    glm::ivec2 q = p;
    q.x = std::max(0, std::min(q.x, w - 1));
    q.y = std::max(0, std::min(q.y, h - 1));
    return get(q);
  }
  
  glm::vec4 get(const glm::ivec2& p) const {
    if (p.x >= 0 && p.x < w && p.y >= 0 && p.y < h) {
      return data_aos[p.y * w + p.x];
    }
    return glm::vec4(0.0f);
  }
  
  void set(const glm::ivec2& p, const glm::vec4& c) {
    if (p.x >= 0 && p.x < w && p.y >= 0 && p.y < h) {
      size_t idx = p.y * pitch + p.x;
      r[idx] = c.r; g[idx] = c.g; b[idx] = c.b; a[idx] = c.a;
      data_aos[p.y * w + p.x] = c;
    }
  }
};

// Maintain compatibility with original Texture2D
using Texture2D = Texture2D_SIMD;
