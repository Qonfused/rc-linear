#ifndef SCENE_H
#define SCENE_H

#include <glm/glm.hpp>
using glm::vec2, glm::vec4, glm::ivec2;

#include "texture.h"

// Signed distance field functions and rendering
vec4 sdCircle(vec2 position, float radius, vec2 pixelPos, vec4 color);
float sdBox(vec2 p, vec2 b);
vec4 sdRectangle(vec2 position, vec2 size, vec2 pixelPos, vec4 color);

vec4 computeSceneColor(ivec2 resolution, ivec2 fragCoord);

#endif // SCENE_H
