#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include "texture.hpp"

inline float sdBox(glm::vec2 p, glm::vec2 b) {
  glm::vec2 d = glm::abs(p) - b;
  return glm::length(glm::max(d, glm::vec2(0.0f))) +
         std::min(std::max(d.x, d.y), 0.0f);
}

inline void sdRectangle(
    glm::vec4& radiance,
    glm::vec2 position,
    glm::vec2 size,
    glm::vec4 color) {
  if (sdBox(position, size) < 0.0f) radiance = color;
}

inline void sdCircle(
    glm::vec4& radiance,
    glm::vec2 position,
    float radius,
    glm::vec4 color) {
  if (glm::length(position) - radius < radius) radiance = color;
}

inline void render_scene(Texture2D& dst, glm::vec2 resolution) {
  const int W = dst.width(), H = dst.height();
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      // gl_FragCoord approximation at pixel center (y flipped)
      glm::vec2 fragCoord(x + 0.5f, (resolution.y - 1.0f - float(y)) + 0.5f);
      glm::vec4 radiance(0.0f);

      glm::vec2 center = (resolution * 0.5f) - fragCoord;
      glm::vec2 edge(0.0f, resolution.y * 0.5f);
      glm::vec2 corner(resolution.x * 0.5f, resolution.y * 0.5f);

      sdCircle(radiance, center + glm::vec2(0.0f, 0.0f), 7.5f, glm::vec4(1, 1, 1, 1));
      // sdRectangle(radiance, center + glm::vec2(-50.0f, 0.0f), glm::vec2(0.005f, 100.0f), glm::vec4(0, 0, 0, 1));
      // sdRectangle(radiance, center + edge, glm::vec2(100.0f, 5.0f), glm::vec4(0, 0, 0, 1));
      // sdRectangle(radiance, center + corner - glm::vec2(200.0f, 300.0f), glm::vec2(300.0f, 1.0f), glm::vec4(0, 0, 0, 1));

      dst.set({x, y}, radiance);
    }
  }
}
