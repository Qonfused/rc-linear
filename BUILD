cc_binary(
  name = "rc_linear",
  srcs = glob([
    "src/*.cpp",
    "src/*.h",
  ]),
  deps = [
    "@glm//:glm",
    "@lodepng//:lodepng"
  ],
  copts = [
    "-std=c++17",
    "-O3", "-march=native",
    "-mfma", "-mavx", "-mavx2",
    # "-ffast-math", "-funroll-loops",
    # "-flto",
  ],
)
