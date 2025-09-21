cc_binary(
  name = "rc_linear",
  srcs = glob([
    "src/*.cpp",
    "src/*.hpp",
  ]),
  deps = [
    "@glm//:glm",
  ],
  copts = select({
    "//conditions:default": [
      "-std=c++17",
      "-O3", "-march=native",
      "-mfma", "-mavx", "-mavx2",
      # "-ffast-math", "-funroll-loops",
      # "-flto",
    ],
    "@bazel_tools//src/conditions:windows": [
      "/std:c++17",
      "/O2", "/arch:AVX2",
      # "/fp:fast",
      # "/GL",
    ],
  }),
)
