#include <iostream>
#include <chrono>
#include "rc.h"
#include "scene.h"

using namespace std::chrono;

int main() {
  ivec2 resolution(512);
  Texture2D sceneTexture(resolution);
  for (int x = 0; x < resolution.x; x++) {
    for (int y = 0; y < resolution.y; y++) {
      ivec2 fragCoord(x, y);
      sceneTexture[fragCoord] = computeSceneColor(resolution, fragCoord);
    }
  }

  auto start = high_resolution_clock::now();

  int numCascades = 5;
  int baseProbeSize = 1;
  float baseIntervalLength = 0.2f;
  // float baseIntervalLength = 0.372923228578f;
  Texture2D cascadeTextureA(resolution);

  for (int i = numCascades; i >= 0; i--)
  {
    int cascadeIndex = i;
    Texture2D cascadeTextureB(resolution);
    for (int x = 0; x < resolution.x; x++) {
      for (int y = 0; y < resolution.y; y++) {
        ivec2 fragCoord(x, y);
        vec4 radiance = computeRadiance(
          fragCoord,
          sceneTexture,
          cascadeTextureA,
          baseProbeSize,
          baseIntervalLength,
          cascadeIndex
        );
        cascadeTextureB[fragCoord] = radiance;
      }
    }
    cascadeTextureA = cascadeTextureB;
  }

  auto stop = high_resolution_clock::now();
  double duration = duration_cast<milliseconds>(stop - start).count() / 1e3;

  std::cout << "Total runtime: " << duration << " seconds" << std::endl;

  // Apply alpha correction to remove transparency from the cascade texture
  for (int x = 0; x < resolution.x; x++) {
    for (int y = 0; y < resolution.y; y++) {
      ivec2 fragCoord(x, y);
      // vec4 radiance = sRGBTransferEOTF(cascadeTextureA[fragCoord]);
      vec4 radiance = cascadeTextureA[fragCoord];
      radiance = vec4(
        radiance.r * radiance.a,
        radiance.g * radiance.a,
        radiance.b * radiance.a,
        1.0f
      );
      // cascadeTextureA[fragCoord] = sRGBTransferOETF(radiance);
      cascadeTextureA[fragCoord] = radiance;
    }
  }

  sceneTexture.to_png("/Users/csqua/Documents/rc-linear/scene.png");
  cascadeTextureA.to_png("/Users/csqua/Documents/rc-linear/output.png");

  return 0;
}
