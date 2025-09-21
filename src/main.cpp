#include <cstdio>
#include <vector>
#include <string>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "texture.hpp"
#include "scene.hpp"
#include "rc.hpp"

static void write_ppm(const std::string& path, const Texture2D& tex) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) { std::perror("fopen"); return; }
  std::fprintf(f, "P6\n%d %d\n255\n", tex.width(), tex.height());
  for (int y = 0; y < tex.height(); ++y) {
    for (int x = 0; x < tex.width(); ++x) {
      glm::vec4 c = tex.get({x, y});
      c = glm::clamp(c, glm::vec4(0.0f), glm::vec4(1.0f));
      unsigned char rgb[3] = {
        (unsigned char)std::lround(c.r * 255.0f),
        (unsigned char)std::lround(c.g * 255.0f),
        (unsigned char)std::lround(c.b * 255.0f)
      };
      std::fwrite(rgb, 1, 3, f);
    }
  }
  std::fclose(f);
}

int main() {
  // Match the reference constants
  constexpr int NUM_CASCADES = 8;
  const int baseProbeSize = 1;
  const float baseIntervalLength = 0.2f;

  // Resolution
  const int W = 512;
  const int H = 512;
  glm::vec2 resolution(W, H);

  // Scene texture (RGBA in sRGB space, A = opacity)
  Texture2D scene(W, H);
  render_scene(scene, resolution);

  // Cascade ping-pong textures (store sRGB after each pass as in GLSL)
  Texture2D cascadeA(W, H), cascadeB(W, H);
  cascadeA.clear(glm::vec4(0.0f));
  cascadeB.clear(glm::vec4(0.0f));

  // Final display/output buffer (sRGB)
  Texture2D output(W, H);
  output.clear(glm::vec4(0.0f));

  // Run cascades top-down as in the JS loop
  // For i = NUM_CASCADES-1 .. 0, ping-pong cascade textures and write final at i=0
  Texture2D* readTex = &cascadeA;
  Texture2D* writeTex = &cascadeB;

  for (int i = NUM_CASCADES - 1; i >= 0; --i) {
    // For i > 0, write into cascade texture; for i == 0, write into final output
    Texture2D& dst = (i == 0) ? output : *writeTex;

    run_rc_pass(
      // uniforms
      baseProbeSize,
      baseIntervalLength,
      i,
      resolution,
      // textures
      scene,     // sceneTexture (read)
      *readTex,  // cascadeTexture (N+1) read
      dst        // current pass output (N)
    );

    // Ping-pong for next pass unless we just wrote to final output
    if (i != 0) std::swap(readTex, writeTex);
  }

  write_ppm("output.ppm", output);
  std::cout << "Wrote output.ppm\n";
  return 0;
}
