config_setting(
  name = "windows",
  constraint_values = ["@platforms//os:windows"],
)

config_setting(
  name = "linux",
  constraint_values = ["@platforms//os:linux"],
)

config_setting(
  name = "macos",
  constraint_values = ["@platforms//os:macos"],
)

cc_library(
  name = "glfw",
  srcs = glob([
    "src/context.c",
    "src/init.c", 
    "src/input.c",
    "src/monitor.c",
    "src/platform.c",
    "src/vulkan.c",
    "src/window.c",
    "src/egl_context.c",
    "src/osmesa_context.c",
    "src/null_*.c",
  ]) + select({
    ":windows": glob([
      "src/win32_*.c",
      "src/wgl_context.c",
    ]),
    ":linux": glob([
      "src/x11_*.c",
      "src/xkb_unicode.c", 
      "src/posix_*.c",
      "src/glx_context.c",
      "src/linux_joystick.c",
    ]),
    ":macos": glob([
      "src/cocoa_*.c",
      "src/cocoa_*.m",
      "src/posix_thread.c",
      "src/posix_module.c",
      "src/nsgl_context.m",
    ]),
    "//conditions:default": [],
  }),
  hdrs = glob([
    "include/GLFW/*.h",
    "src/*.h",
  ]),
  includes = ["include"],
  defines = select({
    ":windows": ["_GLFW_WIN32"],
    ":linux": ["_GLFW_X11"],
    ":macos": ["_GLFW_COCOA"],
    "//conditions:default": [],
  }),
  linkopts = select({
    ":windows": [
      "-DEFAULTLIB:opengl32",
      "-DEFAULTLIB:gdi32", 
      "-DEFAULTLIB:user32",
      "-DEFAULTLIB:shell32",
    ],
    ":linux": [
      "-lGL",
      "-lX11",
      "-lXrandr",
      "-lXinerama",
      "-lXcursor",
      "-lpthread",
      "-ldl",
    ],
    ":macos": [
      "-framework Cocoa",
      "-framework IOKit",
      "-framework CoreVideo",
      "-framework OpenGL",
    ],
    "//conditions:default": [],
  }),
  visibility = ["//visibility:public"],
)
