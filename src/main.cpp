#include <cstdio>
#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#ifdef _WIN32
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef APIENTRY
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include "rc.hpp"
#include "stats.hpp"
#include "plotting.hpp"
#include "perf.hpp"

int main() {
  try {
    // Initialize GLFW
    if (!glfwInit()) {
      std::cerr << "Failed to initialize GLFW\n";
      return 1;
    }

    // Request OpenGL 4.3 (compute)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    std::string window_title = "RC Linear (OpenGL)";
    GLFWwindow* window = glfwCreateWindow(1280, 768, window_title.c_str(), nullptr, nullptr);
    if (!window) {
      std::cerr << "Failed to create GLFW window\n";
      glfwTerminate();
      return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

    window_title = "RC Linear (OpenGLv";
    window_title += gl_version;
    window_title += ")";
    glfwSetWindowTitle(window, window_title.c_str());

    // Initialize GLEW
    if (glewInit() != GLEW_OK) {
      std::cerr << "Failed to initialize GLEW" << std::endl;
      return 1;
    }

    // Initialize GPU renderer
    RCGPURenderer g_gpu_renderer;
    g_gpu_renderer.initialize();

    // ImGui/ImPlot init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    // Parameters
    constexpr int NUM_CASCADES = 8;
    const int baseProbeSize = 1;
    const float baseIntervalLength = 0.2f;

    // Dynamic sizing
    int RC_WIDTH = 512, RC_HEIGHT = 512;

    // Stats
    RadialStats stats;
    double last_stats_time = -1.0;
    const double STATS_INTERVAL = 0.25; // seconds between stat updates
    
    // Initialize async stats manager
    static AsyncStatsManager g_stats_manager;

    // Perf instrumentation
    Perf perf;
    perf.init();
    static uint64_t frame_counter = 0;

    // Window resize debouncing
    static double last_resize_time = 0.0;
    const double RESIZE_DEBOUNCE = 0.1; // 100ms debounce

    // Async RC execution - no fences needed
    auto kick_rc = [&](int w, int h) {
      g_gpu_renderer.run_full_rc(baseProbeSize, baseIntervalLength, NUM_CASCADES,
                                 glm::ivec2(w, h), &perf);
      
      // Initialize and launch async stats computation (no blocking)
      const int max_radius = int(glm::length(glm::vec2(float(w), float(h)) * 0.5f));
      g_stats_manager.init(max_radius);
      
      GLuint outTex = g_gpu_renderer.resultTex();
      g_stats_manager.dispatch_async(outTex, w, h);
    };

    // Initial RC run
    kick_rc(RC_WIDTH, RC_HEIGHT);

    ImPlotChartRenderer chartRenderer;

    // Track window size for dynamic updates
    int last_window_width = 0, last_window_height = 0;

    // Define ImGui panel width and dimensions
    const int IMGUI_PANEL_WIDTH = 520;
    const int MIN_RC_WIDTH = 200;
    const int MIN_RC_HEIGHT = 200;
    const int PADDING = 10;

    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      perf.beginFrame(io.DeltaTime);
      frame_counter++;

      // Check for window size changes with debouncing
      int window_width, window_height;
      glfwGetWindowSize(window, &window_width, &window_height);
      if (window_width != last_window_width || window_height != last_window_height) {
        last_window_width = window_width;
        last_window_height = window_height;
        last_resize_time = ImGui::GetTime();
        
        // Don't kick RC immediately, wait for debounce
      }

      // Handle debounced resize
      if (last_resize_time > 0.0 && (ImGui::GetTime() - last_resize_time) >= RESIZE_DEBOUNCE) {
        int rc_width = std::max(MIN_RC_WIDTH, last_window_width - IMGUI_PANEL_WIDTH - (PADDING * 3));
        int rc_height = std::max(MIN_RC_HEIGHT, last_window_height - (PADDING * 2));
        
        if (RC_WIDTH != rc_width || RC_HEIGHT != rc_height) {
          RC_WIDTH = rc_width;
          RC_HEIGHT = rc_height;
          kick_rc(RC_WIDTH, RC_HEIGHT);
        }
        last_resize_time = 0.0; // Reset debounce
      }

      // Try to read previous frame's stats (non-blocking, async)
      double now = ImGui::GetTime();
      if (last_stats_time < 0.0 || (now - last_stats_time) >= STATS_INTERVAL) {
        if (g_stats_manager.try_read_stats(stats, RC_WIDTH, RC_HEIGHT)) {
          last_stats_time = now;
        }
      }

      // Resolve GPU queries from previous frame(s) without blocking
      perf.resolveAll();

      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      int display_w, display_h;
      glfwGetFramebufferSize(window, &display_w, &display_h);

      // Calculate RC display area (screen-space, bottom-left origin)
      int rc_display_width  = display_w - IMGUI_PANEL_WIDTH - (PADDING * 3);
      int rc_display_height = display_h - (PADDING * 2);
      int rc_x_offset = PADDING;
      int rc_y_offset = PADDING;

      rc_display_width  = std::max(rc_display_width,  MIN_RC_WIDTH);
      rc_display_height = std::max(rc_display_height, MIN_RC_HEIGHT);

      // Set viewport for RC display (normalized coordinates [0,1]^2)
      glViewport(rc_x_offset, rc_y_offset, rc_display_width, rc_display_height);
      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      glOrtho(0, 1, 0, 1, -1, 1);
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();

      // Shared hover sync state (reset each frame)
      HoverSync sync{};

      // Draw the renderer's RGBA8 display texture
      {
        GLuint disp = g_gpu_renderer.displayTex();
        if (disp != 0) {
          glEnable(GL_TEXTURE_2D);
          glBindTexture(GL_TEXTURE_2D, disp);
          glColor3f(1.0f, 1.0f, 1.0f);
          glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
            glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
            glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
          glEnd();
          glDisable(GL_TEXTURE_2D);
        }
      }

      // RC hover detection and overlay circle
      {
        double mx_win, my_win;
        glfwGetCursorPos(window, &mx_win, &my_win);
        float mouse_x = (float)mx_win;
        float mouse_y_bl = (float)display_h - (float)my_win; // bottom-left origin

        bool hover_rc = mouse_x >= (float)rc_x_offset &&
                        mouse_x <= (float)(rc_x_offset + rc_display_width) &&
                        mouse_y_bl >= (float)rc_y_offset &&
                        mouse_y_bl <= (float)(rc_y_offset + rc_display_height);

        if (hover_rc && RC_WIDTH > 0 && RC_HEIGHT > 0) {
          float u = (mouse_x - (float)rc_x_offset) / (float)rc_display_width;
          float v = (mouse_y_bl - (float)rc_y_offset) / (float)rc_display_height;

          float px = u * RC_WIDTH;
          float py = v * RC_HEIGHT;

          float cx = 0.5f * RC_WIDTH;
          float cy = 0.5f * RC_HEIGHT;

          float rp = std::sqrt((px - cx)*(px - cx) + (py - cy)*(py - cy));
          float max_r = std::sqrt(cx*cx + cy*cy);

          sync.active = true;
          sync.radius = std::clamp(rp, 0.0f, max_r);

          // Tooltip near the mouse with the pixel radius
          ImVec2 mouse_win = ImGui::GetIO().MousePos; // top-left origin
          ImGui::SetNextWindowPos(ImVec2(mouse_win.x + 14.0f, mouse_win.y + 18.0f), ImGuiCond_Always);
          ImGui::BeginTooltip();
          ImGui::Text("r = %.1f px", sync.radius);
          ImGui::EndTooltip();
        }

        // Draw overlay circle if active (ellipse in normalized space to reflect pixel radius)
        if (sync.active && RC_WIDTH > 0 && RC_HEIGHT > 0) {
          const int segments = 256;
          const float cx_n = 0.5f, cy_n = 0.5f;
          const float rx_n = sync.radius / (float)RC_WIDTH;
          const float ry_n = sync.radius / (float)RC_HEIGHT;

          glColor4f(1.0f, 0.8f, 0.2f, 1.0f);
          glLineWidth(1.5f);
          glBegin(GL_LINE_LOOP);
          for (int i = 0; i < segments; ++i) {
            float t = (float)i * (2.0f * glm::pi<float>() / (float)segments);
            float x = cx_n + rx_n * std::cos(t);
            float y = cy_n + ry_n * std::sin(t);
            glVertex2f(x, y);
          }
          glEnd();
        }
      }

      // Reset viewport for UI elements
      glViewport(0, 0, display_w, display_h);

      // Draw border around RC display
      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      glOrtho(0, display_w, 0, display_h, -1, 1);
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();

      glColor4f(63.0f/255.0f, 63.0f/255.0f, 72.0f/255.0f, 1.0f); // #3f3f48
      glLineWidth(1.0f);
      glBegin(GL_LINE_LOOP);
        glVertex2f((float)PADDING, (float)PADDING);
        glVertex2f((float)(PADDING + rc_display_width), (float)PADDING);
        glVertex2f((float)(PADDING + rc_display_width), (float)(PADDING + rc_display_height));
        glVertex2f((float)PADDING, (float)(PADDING + rc_display_height));
      glEnd();

      // Perf overlay (frame counter + timing) in RC viewport top-left (screen-space)
      perf.drawOverlay(display_h,
                       PADDING, PADDING, rc_display_width, rc_display_height,
                       frame_counter, true);

      // Render charts from last computed stats, synchronized with RC hover/markers
      chartRenderer.render(stats, sync);

      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

      perf.endFrame();
      glfwSwapBuffers(window);
    }

    // Cleanup async stats manager
    g_stats_manager.cleanup();

    perf.shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

  } catch (const std::exception& e) {
    std::cerr << "Exception caught: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Unknown exception caught" << std::endl;
    return 1;
  }

  return 0;
}
