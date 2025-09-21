#pragma once
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
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
  glm::vec3 a = glm::pow(rgb, glm::vec3(0.41666f)) * 1.055f - glm::vec3(0.055f);
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
     ratio.x     * (1.0f - ratio.y),
    (1.0f - ratio.x) *  ratio.y,
     ratio.x     *  ratio.y
  );
}

inline glm::ivec2 bilinearOffset(int idx) {
  return glm::ivec2(idx % 2, idx / 2);
}

// One full RC pass for a given cascade index (N), reading N+1 and writing N
inline void run_rc_pass(
  int baseProbeSize,
  float baseIntervalLength,
  int cascadeIndex,
  glm::vec2 resolution,                 // texture size in pixels
  const Texture2D& sceneTexture,        // sRGB scene with alpha
  const Texture2D& cascadeTextureNplus, // prior cascade (N+1), stored in sRGB
  Texture2D& outTextureN                // output texture for cascade N (sRGB)
) {
  const int W = int(resolution.x), H = int(resolution.y);
  const int probeSize = (baseProbeSize << cascadeIndex);
  const int bilinearProbeSize = (baseProbeSize << (cascadeIndex + 1));

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

      // Prepare bilinear sampling from cascade N+1
      glm::vec4 accumRadiance(0.0f, 0.0f, 0.0f, 1.0f);

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
