#include <cmath>
#include "rc.h"

static constexpr vec3 rgb(const vec4& color) {
  return vec3(color.x, color.y, color.z);
}

const float PI = std::acos(-1.0f);

float getIntervalScale(int cascade_index) {
  if (cascade_index <= 0) return 0.0;
  // Scale interval by 4x for each cascade level
  return static_cast<float>(1 << (2 * cascade_index));
}

vec2 getIntervalRange(int cascadeIndex, float baseIntervalLength) {
  return baseIntervalLength * vec2(
    getIntervalScale(cascadeIndex),
    getIntervalScale(cascadeIndex + 1)
  );
}

vec4 castInterval(
    const Texture2D& sceneTexture,
    vec2 intervalStart,
    vec2 intervalEnd,
    int cascadeIndex) {
  vec2 dir = intervalEnd - intervalStart;
  int steps = 32 << cascadeIndex;
  vec2 stepSize = dir / static_cast<float>(steps);

  vec3 radiance(0.0f);
  float transmittance = 1.0f;

  // Ray marching loop
  for (int i = 0; i < steps; ++i) {
    ivec2 coord = intervalStart + stepSize * static_cast<float>(i);
    vec4 scene = sceneTexture[coord];
    // Accumulate color with transmittance weighting
    radiance += rgb(scene) * (transmittance * scene.a);
    // Update transmittance with alpha composition
    transmittance *= 1.0f - scene.a;
  }

  return vec4(radiance, transmittance);
}

vec4 mergeIntervals(const vec4& near, const vec4& far) {
  // Far radiance can get occluded by near visibility term
  vec3 radiance = rgb(near) + (rgb(far) * near.a);
  // Merging the transmittence terms will carry a ray hit downwards
  // (i.e. if near is occluded, far will be occluded as well)
  return vec4(radiance, near.a * far.a);
}

vec4 getBilinearWeights(const vec2& ratio) {
  float w0 = (1.0f - ratio.x) * (1.0f - ratio.y);
  float w1 =         ratio.x  * (1.0f - ratio.y);
  float w2 = (1.0f - ratio.x) *         ratio.y;
  float w3 =         ratio.x  *         ratio.y;
  return vec4(w0, w1, w2, w3);
}

std::tuple<vec4, ivec2> getBilinearSamples(
  const vec2& destCenter,
  int bilinearProbeSize) {
  vec2 baseCoord = (destCenter / static_cast<float>(bilinearProbeSize)) - 0.5f;

  vec2 ratio = fract(baseCoord);          // Sub-bilinear probe position
  vec4 weights = getBilinearWeights(ratio);
  ivec2 baseIndex(floor(baseCoord));   // Top-left bilinear probe coordinate

  return std::make_tuple(weights, baseIndex);
}

/* Convert index 0..4 to a 2d index in a 2x2 square */
ivec2 getBilinearOffset(int offsetIndex) {
  return ivec2(offsetIndex % 2, offsetIndex / 2);
}

vec4 sRGBTransferEOTF(const vec4& value) {
  vec3 lin;
  for (int i = 0; i < 3; ++i) {
    if (value[i] <= 0.04045f) {
      lin[i] = value[i] * 0.0773993808f;
    } else {
      lin[i] = std::pow(value[i] * 0.9478672986f + 0.0521327014f, 2.4f);
    }
  }
  return vec4(lin, value.a);
}

vec4 sRGBTransferOETF(const vec4& value) {
  vec3 srgb;
  for (int i = 0; i < 3; ++i) {
    if (value[i] <= 0.0031308f) {
      srgb[i] = value[i] * 12.92f;
    } else {
      srgb[i] = 1.055f * std::pow(value[i], 0.41666667f) - 0.055f;
    }
  }
  return vec4(srgb, value.a);
}

float rand(const vec2& uv) {
  const float a = 12.9898f;
  const float b = 78.233f;
  const float c = 43758.5453f;
  float dt = dot(uv, vec2(a, b));
  float sn = std::fmod(dt, PI);
  float value = std::sin(sn) * c;
  return value - std::floor(value);
}

vec4 dithering(const vec4& color, const vec2& fragCoord) {
  // Calculate grid position
  float grid_position = rand(fragCoord);
  // Shift the individual colors differently to obscure the dithering pattern.
  vec3 dither_shift_RGB(0.25f / 255.0f, -0.25f / 255.0f, 0.25f / 255.0f);
  // modify shift according to grid position.
  dither_shift_RGB = mix(
    2.0f * dither_shift_RGB,
    -2.0f * dither_shift_RGB,
    grid_position
  );

  // shift the color by dither_shift
  return vec4(rgb(color) + dither_shift_RGB, color.a);
}

vec4 computeRadiance(
    const ivec2& fragCoord,
    const Texture2D& sceneTexture,
    const Texture2D& cascadeTexture,
    int baseProbeSize,
    float baseIntervalLength,
    int cascadeIndex) {
  // --- CASCADE PROBE SETUP ---
  // Calculate the nearest probe position for the current cascade level.
  // - The probe size represents the number of nearby pixels sampled to compute
  //   the radiance at the probe position (at the center of the pixels).
  //   (e.g. a probe of size 4 samples 4x4=16 pixels, a probe of size 8 samples
  //    8x8=64 pixels, etc.)
  // - The probe size doubles per cascade level (e.g. 1, 2, 4, 8, 16, 32, ...)
  int probeSize = baseProbeSize << cascadeIndex;
  // - Probe center is centered within the probe grid, i.e. the first probe is
  //   at (0.5, 0.5), the second probe is at (1.5, 0.5), etc.
  //        x: 0-1                             x: 1-2
  //        y: 0-1                             y: 0-1
  vec2 probeCenter = floor(vec2(fragCoord) / static_cast<float>(probeSize)) + 0.5f;
  // - Probe position is the probe center among the scene texture's pixel grid.
  //   (e.g. a probe of size 4 spans from (0, 0) to (3, 3) in the scene texture,
  //    with a probe center at (2, 2))
  //   ┌───────────────┬───┬───────────┐       Probe Grid:    Pixel Grid:
  //   │ (0,0)   (1,0) │ (2,0)   (3,0) │         x: 0-1         x: 0-3
  //   │               │   │           │         y: 0-1         y: 0-3
  //   │ (0,1)   (1,1) │ (2,1)   (3,1) │
  //   ├───────────────X───┼───────────┤  The real sub-pixel probe center is at 
  //   ├─(0,2)───(1,2)─┼─(2,2)───(3,2)─┤  (1.5, 1.5) in the scene texture, but
  //   │               │   │           │  to align with the integer pixel grid,
  //   │ (0,3)   (1,3) │ (2,3)   (3,3) │  this is always rounded up to (2, 2).
  //   └───────────────┴───┴───────────┘
  vec2 probePosition = probeCenter * static_cast<float>(probeSize);

  // printf("Cascade index: %d\n", cascadeIndex);
  // printf("Fragment coordinate: (%d, %d)\n", fragCoord.x, fragCoord.y);
  // printf("Probe size: %d\n", probeSize);
  // printf("Probe position: (%f, %f)\n", probePosition.x, probePosition.y);

  // --- DIRECTION CALCULATION ---
  // Convert the fragment position to a direction index in the cascade texture.
  // - The direction coordinate is the fragment coordinate modulo the probe size.
  //   (e.g. for a 4x4 probe grid, the direction coordinate is in the range [0,3])
  ivec2 dirCoord = fragCoord % probeSize;
  // - The direction index is the positional index of the pixel in the probe grid.
  //   For example, a 4x4 probe grid has 16 directions (1 direction per texel):
  //   ┌───────────────────┐
  //   │  0    1    2    3 │  4th quadrant (→ ↘ ↘ ↓)
  //   │                   │
  //   │  4    5    6    7 │  3rd quadrant (↓ ↙ ↙ ←)
  //   │                   │
  //   │  8    9   10   11 │  2nd quadrant (← ↖ ↖ ↑)
  //   │                   │
  //   │ 12   13   14   15 │  1st quadrant (↑ ↗ ↗ →)
  //   └───────────────────┘
  int dirIndex = dirCoord.x + dirCoord.y * probeSize;
  int dirCount = probeSize * probeSize;
  // Calculate the interval direction from the direction index (for cascade N).
  // - To compute the interval direction, the direction index is first centered
  //   within its texel (+0 is lower left, +0.5 is center, +1 is upper right).
  //                           (←)              (↑)               (→)
  //   To convert into an angle, it is then normalized to the range [0, 1) by
  //   the number of directions and multiplied by 2π to convert it into an angle
  //   in the range [0, 2π).
  float angle = 2.0f * PI * ((static_cast<float>(dirIndex) + 0.5f) / static_cast<float>(dirCount));
  // - The direction vector is a unit vector pointing in this direction.
  vec2 dir = vec2(std::cos(angle), std::sin(angle));

  // printf("Direction index: %d\n", dirIndex);
  // printf("Direction: (%f, %f)\n", dir.x, dir.y);

  // --- INTERVAL CASTING ---
  // Calculate interval start/end points for the current cascade level.
  // - Interval length grows quadratically with cascade level (2^(2n))
  //   (e.g. 1, 4, 16, 64, 256, ...)
  vec2 intervalRange = getIntervalRange(cascadeIndex, baseIntervalLength);
  // - Interval start/end points are calculated by projecting this interval
  //   range from the probe position along the direction vector.
  vec2 intervalStart = probePosition + dir * intervalRange.x;
  vec2 intervalEnd = probePosition + dir * intervalRange.y;
  // Cast interval at cascade N -> cascade N+1
  // - Ray marches and accumulates radiance along the interval start/end points.
  vec4 destInterval = castInterval(
    sceneTexture,
    intervalStart,
    intervalEnd,
    cascadeIndex
  );
  // printf("Interval start: (%f, %f)\n", intervalStart.x, intervalStart.y);
  // printf("Interval end: (%f, %f)\n", intervalEnd.x, intervalEnd.y);

  vec4 radiance(0, 0, 0, 1);

  // --- CASCADE MERGING ---
  // Prepare for bilinear interpolation from next cascade level
  // - Bilinear probe size is determined from cascade N+1
  const int bilinearProbeSize = baseProbeSize << (cascadeIndex + 1);
  // Get 4 sample positions/weights from next cascade level
  // - Weights are the bilinear weights for each N+1 sample
  // - Base index is the top-left bilinear probe coordinate
  auto [weights, baseIndex] = getBilinearSamples(
    probePosition,
    bilinearProbeSize
  );

  // Process 4 bilinear samples for each N+1 interval
  for (int b = 0; b < 4; ++b) {
    // Get the index of the bilinear probe to merge with
    // (e.g. 0 -> (0,0), 1 -> (1,0), 2 -> (0,1), 3 -> (1,1))
    const ivec2 bilinearIndex = baseIndex + getBilinearOffset(b);

    // Average 4 directional samples per N+1 bilinear probe
    vec4 bilinearRadiance(0.0f);
    float weight = weights[b];
    for (int d = 0; d < 4; ++d) {
      // Calculate direction index for next cascade level
      // (b/4 bilinear probe parameters at cascade N+1)
      // - The base directional index for the current probe
      const int baseDirIndex = dirIndex * 4;
      // - The directional index we want to merge with
      const int bilinearDirIndex = baseDirIndex + d;

      // Fetch merged interval at cascade N+1
      // - The local texel coordinate (computed from the directional index).
      //   Can be one of (0,0), (1,0), (0,1), (1,1) for bilinear probes.
      const ivec2 bilinearDirCoord(
        bilinearDirIndex % bilinearProbeSize,
        bilinearDirIndex / bilinearProbeSize
      );
      // - The bilinear offset from the base probe coordinate.
      //   This is the offset from the top-left corner of the bilinear probe
      //   to center the bilinear probe in the texel grid.
      vec2 bilinearOffset(bilinearIndex * ivec2(bilinearProbeSize));

      // Clamp to texture boundaries to avoid vignetting artifacts
      bilinearOffset = clamp(
        bilinearOffset,
        vec2(0.5f),
        vec2(sceneTexture.resolution - bilinearProbeSize)
      );

      // Fetch and decode cascade data
      // - The texel coordinate to merge with in cascade N+1
      ivec2 bilinearTexel = ivec2(bilinearOffset) + bilinearDirCoord;
      // - Fetches the bilinear interval from the cascade N+1 texture
      vec4 bilinearInterval = cascadeTexture[bilinearTexel];
      // bilinearInterval = sRGBTransferEOTF(bilinearInterval);

      // Merge with current interval using the bilinear weight
      vec4 merged = mergeIntervals(destInterval, bilinearInterval);
      bilinearRadiance += merged * weight;
    }

    // Average of 4 bilinear samples
    radiance += bilinearRadiance * 0.25f;
  }
  // printf("---\n");

  // // --- FINAL OUTPUT ---
  // // Encode to sRGB and apply dithering
  // radiance = sRGBTransferOETF(radiance);
  // radiance = dithering(radiance, fragCoord);

  return radiance;
}
