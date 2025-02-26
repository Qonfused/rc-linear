#include <cmath>
#include "scene.h"
#include "texture.h"

// https://iquilezles.org/articles/distfunctions2d
vec4 sdCircle(vec2 position, float radius, vec2 pixelPos, vec4 color) {
  if (length(pixelPos - position) - radius < radius) {
    return color;
  }
  return vec4(0.0f);
}

float sdBox(vec2 p, vec2 b) {
  vec2 d = abs(p) - b;
  return length(max(d, 0.0f)) +
      fmin(fmax(d.x, d.y), 0.0f);
}

vec4 sdRectangle(vec2 position, vec2 size, vec2 pixelPos, vec4 color) {
  vec2 localPos = pixelPos - position;
  return sdBox(localPos, size) < 0.0f ? color : vec4(0.0f);
}

vec4 computeSceneColor(ivec2 resolution, ivec2 fragCoord) {
  vec2 fResolution(resolution);
  vec2 fFragCoord(fragCoord);

  vec4 radiance(0.0f);

  vec2 center = (fResolution * 0.5f);// - fFragCoord;
  vec2 edge(0, fResolution.y * 0.5f);
  vec2 corner(fResolution.x * 0.5f, fResolution.y * 0.5f);

  radiance += sdCircle(center, 5.0f, fFragCoord, vec4(1, 1, 1, 1));
  // radiance += sdRectangle(center + vec2(-50, 0), vec2(0.005f, 100), fFragCoord, vec4(0, 0, 0, 1));

  // radiance += sdCircle(center, 7.5f, fFragCoord, vec4(1, 1, 1, 1));
  // radiance += sdRectangle(center + vec2(-50, 0), vec2(0.005f, 100), fFragCoord, vec4(0, 0, 0, 1));
  // radiance += sdRectangle(center + edge, vec2(100, 5), fFragCoord, vec4(0, 0, 0, 1));
  // radiance += sdRectangle(center + corner - vec2(200, 300), vec2(300, 1), fFragCoord, vec4(0, 0, 0, 1));

  return radiance;
}
