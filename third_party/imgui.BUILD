cc_library(
  name = "imgui",
  srcs = [
    "imgui.cpp",
    "imgui_demo.cpp", 
    "imgui_draw.cpp",
    "imgui_tables.cpp",
    "imgui_widgets.cpp",
  ],
  hdrs = [
    "imgui.h",
    "imconfig.h",
    "imgui_internal.h",
    "imstb_rectpack.h",
    "imstb_textedit.h",
    "imstb_truetype.h",
  ],
  includes = ["."],
  visibility = ["//visibility:public"],
)

cc_library(
  name = "imgui_glfw_opengl3",
  srcs = [
    "backends/imgui_impl_glfw.cpp",
    "backends/imgui_impl_opengl3.cpp",
  ],
  hdrs = [
    "backends/imgui_impl_glfw.h",
    "backends/imgui_impl_opengl3.h",
  ],
  includes = ["backends"],
  deps = [
    ":imgui",
    "@glfw//:glfw",
    "@glew//:glew",
  ],
  defines = [
    "IMGUI_IMPL_OPENGL_LOADER_GLEW=1",
  ],
  linkopts = select({
    "@platforms//os:windows": [
      "-DEFAULTLIB:opengl32",
    ],
    "//conditions:default": [
      "-lGL",
    ],
  }),
  visibility = ["//visibility:public"],
)
