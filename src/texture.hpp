#pragma once

#include <vector>
#include <algorithm>
#include <glm/glm.hpp>

struct Texture2D {
  Texture2D() : w(0), h(0) {}
  Texture2D(int W, int H) { resize(W, H); }

  void resize(int W, int H) {
    w = W; h = H;
    data.assign(size_t(W) * size_t(H), glm::vec4(0.0f));
  }

  inline int width() const { return w; }
  inline int height() const { return h; }

  void clear(const glm::vec4& c) {
    std::fill(data.begin(), data.end(), c);
  }

  inline bool in_bounds(const glm::ivec2& p) const {
    return (p.x >= 0 && p.x < w && p.y >= 0 && p.y < h);
  }

  glm::vec4 texelFetchClamp(const glm::ivec2& p) const {
    glm::ivec2 q = p;
    q.x = std::max(0, std::min(q.x, w - 1));
    q.y = std::max(0, std::min(q.y, h - 1));
    return data[size_t(q.y) * size_t(w) + size_t(q.x)];
  }

  glm::vec4 get(const glm::ivec2& p) const {
    return data[size_t(p.y) * size_t(w) + size_t(p.x)];
  }

  void set(const glm::ivec2& p, const glm::vec4& c) {
    data[size_t(p.y) * size_t(w) + size_t(p.x)] = c;
  }

private:
  int w, h;
  std::vector<glm::vec4> data;
};
