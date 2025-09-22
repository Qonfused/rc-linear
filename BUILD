cc_binary(
  name = "rc_linear",
  srcs = glob([
    "src/*.cpp", 
    "src/*.hpp",
  ]),
  deps = [
    "@glm//:glm",
    "@glfw//:glfw", 
    "@imgui//:imgui",
    "@imgui//:imgui_glfw_opengl2",
    "@implot//:implot",
  ],
  copts = select({
    "@platforms//os:windows": [
      "/std:c++17",
      "/O2", "/Ob3", "/arch:AVX2", 
      "/fp:fast",         # Enable fast floating point
      "/GL",              # Enable whole program optimization
      "/Qvec-report:1",   # Vectorization reports
      "/openmp",          # Enable OpenMP threading
    ],
    "//conditions:default": [
      "-std=c++17",
      "-O3", "-march=native", 
      "-mfma", "-mavx", "-mavx2",
      "-ffast-math",      # Fast math optimizations
      "-funroll-loops",   # Aggressive loop unrolling
      "-flto",            # Link time optimization
      "-fvectorize",      # Force vectorization
      "-ftree-vectorize", # Tree vectorizer
      "-fopenmp",         # Enable OpenMP threading
    ],
  }),
  linkopts = select({
    "@platforms//os:windows": [
      "-DEFAULTLIB:opengl32",
      "/LTCG", # Link-time code generation
    ],
    "//conditions:default": [
      "-lGL",
      "-flto", # Link time optimization
    ],
  }),
)
