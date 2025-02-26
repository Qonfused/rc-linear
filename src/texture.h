#ifndef TEXTURE_H
#define TEXTURE_H

#include <vector>
#include <string>
#include <iostream>
#include <stdexcept>

#include <glm/glm.hpp>
using glm::vec2, glm::vec4, glm::ivec2;

#include "lodepng.h"

struct Texture2D {
  int width;
  int height;
  ivec2 resolution;
  std::vector<float> data;

  vec4 zero = vec4(0.0f);

  Texture2D(int width, int height) : width(width), height(height) {
    resolution = ivec2(width, height);
    data.resize(width * height * 4);
  }

  Texture2D(ivec2 size) : Texture2D(size.x, size.y) {}

  int index(ivec2 coord) const {
    return 4 * (coord.x + coord.y * width);
  }

  vec4& operator[](ivec2 coord) {
    if (coord.x < 0 || coord.x >= width ||
        coord.y < 0 || coord.y >= height) {
      return zero;

      // throw std::out_of_range("Texture2D index ("
      //   + std::to_string(coord.x) + ", "
      //   + std::to_string(coord.y) + ") out of range");
    }

    int i = index(coord);
    return *reinterpret_cast<vec4*>(&data[i]);
  }

  const vec4& operator[](ivec2 coord) const {
    if (coord.x < 0 || coord.x >= width ||
        coord.y < 0 || coord.y >= height) {
      return zero;

      // throw std::out_of_range("Texture2D index ("
      //   + std::to_string(coord.x) + ", "
      //   + std::to_string(coord.y) + ") out of range");
    }

    int i = index(coord);
    return *reinterpret_cast<const vec4*>(&data[i]);
  }

  // Bilinear sampling method - accepts coordinates in pixel space
  vec4 sample(const vec2& coord) const {
    // Convert to texel space
    vec2 texel = coord;

    // Get integer and fractional parts
    vec2 texel_i = floor(texel);
    vec2 fract_part = texel - texel_i;

    // Get the four surrounding texels
    ivec2 i00 = ivec2(texel_i);
    ivec2 i10 = ivec2(texel_i.x + 1, texel_i.y);
    ivec2 i01 = ivec2(texel_i.x, texel_i.y + 1);
    ivec2 i11 = ivec2(texel_i.x + 1, texel_i.y + 1);

    // Get the texel values
    vec4 v00 = (*this)[i00];
    vec4 v10 = (*this)[i10];
    vec4 v01 = (*this)[i01];
    vec4 v11 = (*this)[i11];

    // Interpolate along x
    vec4 v0 = mix(v00, v10, fract_part.x);
    vec4 v1 = mix(v01, v11, fract_part.x);

    // Interpolate along y
    return mix(v0, v1, fract_part.y);
  }

  static vec4 mix(const vec4& a, const vec4& b, float t) {
    return a * (1.0f - t) + b * t;
  }

  void to_png(const std::string& filename) const {
    std::vector<unsigned char> pngData(width * height * 4);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        int i = index(ivec2(x, y));
        int j = (x + y * width) * 4;
        pngData[j    ] = static_cast<unsigned char>(data[i    ] * 255.0f); // R
        pngData[j + 1] = static_cast<unsigned char>(data[i + 1] * 255.0f); // G
        pngData[j + 2] = static_cast<unsigned char>(data[i + 2] * 255.0f); // B
        pngData[j + 3] = static_cast<unsigned char>(data[i + 3] * 255.0f); // A
      }
    }

    // Encode the image to PNG
    std::vector<unsigned char> png;
    unsigned error = lodepng::encode(png, pngData, width, height);
    if (error) {
      std::cerr << "encoder error " << error << ": " << lodepng_error_text(error) << std::endl;
      return;
    }

    // Save the png file
    lodepng::save_file(png, filename);
  }
};

#endif // TEXTURE_H
