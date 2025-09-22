cc_binary(
  name = "rc_linear",
  srcs = glob([
    "src/*.cpp",
    "src/*.hpp",
  ]),
  deps = [
    "@glm//:glm",
    "@glew//:glew",
    "@glfw//:glfw",
    "@imgui//:imgui",
    "@imgui//:imgui_glfw_opengl3",
    "@implot//:implot",
  ],
  copts = select({
    "@platforms//os:windows": [
      "/std:c++17",
      "/O2", "/arch:AVX2",
      "/D_USE_MATH_DEFINES",
      "/DGLEW_STATIC",
    ],
    "//conditions:default": [
      "-std=c++17",
      "-O3", "-march=native",
      "-DGLEW_STATIC",
    ],
  }),
  linkopts = select({
    "@platforms//os:windows": [
      "-DEFAULTLIB:opengl32",
    ],
    "//conditions:default": [
      "-lGL",
    ],
  }),
)
