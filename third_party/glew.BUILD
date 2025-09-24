cc_library(
  name = "glew",
  srcs = [
    "src/glew.c",
  ],
  hdrs = [
    "include/GL/glew.h",
  ],
  includes = ["include"],
  defines = [
    "GLEW_STATIC",
    "GLEW_BUILD",
    # Only include compute shader extensions
    "GLEW_ARB_compute_shader",
    "GLEW_ARB_shader_image_load_store",
  ],
  linkopts = select({
    "@platforms//os:windows": [
      "-DEFAULTLIB:opengl32",
    ],
    "@platforms//os:linux": [
      "-lGL",
    ],
    "@platforms//os:macos": [
      "-framework OpenGL", 
    ],
    "//conditions:default": [
      "-lGL",
    ],
  }),
  visibility = ["//visibility:public"],
)
