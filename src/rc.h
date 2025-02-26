#ifndef RC_H
#define RC_H

#include <glm/glm.hpp>
using glm::vec2, glm::vec3, glm::vec4, glm::ivec2;

#include "texture.h"

vec2 getIntervalRange(int cascadeIndex, float baseIntervalLength);
vec4 castInterval(const Texture2D& sceneTexture, vec2 intervalStart, vec2 intervalEnd, int cascadeIndex);
vec4 mergeIntervals(const vec4& near, const vec4& far);

vec4 getBilinearWeights(const vec2& ratio);
void getBilinearSamples(const vec2& baseCoord, vec4& weights, ivec2& baseIndex);
ivec2 getBilinearOffset(int offsetIndex);

vec4 sRGBTransferEOTF(const vec4& value);
vec4 sRGBTransferOETF(const vec4& value);
float rand(const vec2& uv);
vec4 dithering(const vec4& color, const vec2& fragCoord);

vec4 computeRadiance(
  const ivec2& fragCoord,
  const Texture2D& sceneTexture,
  const Texture2D& cascadeTexture,
  int baseProbeSize,
  float baseIntervalLength,
  int cascadeIndex);

#endif // RC_H
