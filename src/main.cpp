#include <cstdio>
#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <iostream>
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

#include "texture.hpp"
#include "scene.hpp"
#include "rc.hpp"

struct RadialStats {
  std::vector<float> radii;
  std::vector<float> mean;
  std::vector<float> stddev;
  std::vector<int> count;
  std::vector<float> ground_truth;
  std::vector<float> stddev_upper;
  std::vector<float> stddev_lower;
};

RadialStats compute_radial_stats(const Texture2D& tex) {
  std::cout << "Computing radial stats..." << std::endl;
  
  int W = tex.width(), H = tex.height();
  glm::vec2 center(float(W) / 2.0f, float(H) / 2.0f);
  int max_radius = int(glm::length(center));

  std::vector<std::vector<float>> lum_by_radius(max_radius + 1);
  std::vector<float> radii;
  std::vector<float> ground_truth(max_radius + 1, 0.0f);

  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      glm::vec2 p(float(x) + 0.5f, float(y) + 0.5f);
      int r = int(glm::length(p - center));
      glm::vec4 c = tex.get({x, y});
      float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
      if (r >= 0 && r <= max_radius) {
        lum_by_radius[r].push_back(lum);
      }
    }
  }

  std::vector<float> mean(max_radius + 1, 0.0f);
  std::vector<float> stddev(max_radius + 1, 0.0f);
  std::vector<float> stddev_upper, stddev_lower;
  std::vector<int> count(max_radius + 1, 0);

  for (int r = 0; r <= max_radius; ++r) {
    radii.push_back(float(r));
    const auto& vals = lum_by_radius[r];
    if (!vals.empty()) {
      float sum = std::accumulate(vals.begin(), vals.end(), 0.0f);
      mean[r] = sum / vals.size();
      float sq_sum = 0.0f;
      for (float v : vals) sq_sum += v * v;
      float var = sq_sum / vals.size() - mean[r] * mean[r];
      stddev[r] = (var > 0.0f) ? std::sqrt(var) : 0.0f;
      count[r] = static_cast<int>(vals.size());
    }
    stddev_upper.push_back(mean[r] + stddev[r]);
    stddev_lower.push_back(std::max(mean[r] - stddev[r], 0.0f));
  }

  // Ground truth calculation
  float disk_radius = 15.0f;
  float peak = 1.0f;
  for (int r = 0; r <= max_radius; ++r) {
    if (r <= int(disk_radius)) {
      ground_truth[r] = peak;
    } else {
      ground_truth[r] = peak * disk_radius * disk_radius / (float(r) * float(r) + 1e-3f);
    }
  }

  std::cout << "Radial stats computed successfully" << std::endl;
  return RadialStats{radii, mean, stddev, count, ground_truth, stddev_upper, stddev_lower};
}

static std::vector<unsigned char> texture_to_rgba(const Texture2D& tex) {
  std::cout << "Converting texture to RGBA..." << std::endl;
  std::vector<unsigned char> buf(tex.width() * tex.height() * 4);
  for (int y = 0; y < tex.height(); ++y) {
    for (int x = 0; x < tex.width(); ++x) {
      glm::vec4 c = tex.get({x, y});
      c = glm::clamp(c, glm::vec4(0.0f), glm::vec4(1.0f));
      size_t idx = (y * tex.width() + x) * 4;
      buf[idx + 0] = (unsigned char)std::lround(c.r * 255.0f);
      buf[idx + 1] = (unsigned char)std::lround(c.g * 255.0f);
      buf[idx + 2] = (unsigned char)std::lround(c.b * 255.0f);
      buf[idx + 3] = (unsigned char)std::lround(c.a * 255.0f);
    }
  }
  std::cout << "Texture converted successfully" << std::endl;
  return buf;
}

class ImPlotChartRenderer {
private:
  static inline double shared_x_min = 0.0;
  static inline double shared_x_max = 100.0;

public:
  ImPlotChartRenderer() {
    std::cout << "ImPlot Chart Renderer initialized" << std::endl;
  }

  void render(const RadialStats& stats) {
    std::vector<float> stddev_percent(stats.stddev.size(), 0.0f);
    for (size_t i = 0; i < stats.stddev.size(); ++i) {
      if (stats.mean[i] != 0.0f) {
        stddev_percent[i] = (stats.stddev[i] / stats.mean[i]) * 100.0f;
      } else {
        stddev_percent[i] = 0.0f;
      }
    }

    // Get display size for positioning
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display_size = io.DisplaySize;
    
    // Define panel dimensions
    const float panel_width = 520.0f;
    const float panel_height = display_size.y - 20.0f; // Full height minus padding
    const float padding = 10.0f;
    
    // Position at top-right corner
    ImVec2 window_pos(display_size.x - panel_width - padding, padding);
    ImVec2 window_size(panel_width, panel_height);

    // Set window position and size (always)
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);

    // Create window with specific flags to prevent user manipulation
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | 
                     ImGuiWindowFlags_NoResize | 
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar;

    if (ImGui::Begin("##RadianceCascadeAnalysis", nullptr, window_flags)) {
      ImGui::Text("Luminance Falloff Analysis");
      ImGui::Separator();
      
      float available_height = ImGui::GetContentRegionAvail().y - 120.0f;

      if (!stats.radii.empty()) {
        shared_x_max = stats.radii.back();
      }

      static float row_ratios[] = {0.7f, 0.3f};

      if (ImPlot::BeginSubplots("##AlignedPlots", 2, 1, ImVec2(-1, available_height),
                  ImPlotSubplotFlags_LinkCols, row_ratios, nullptr)) {

        if (ImPlot::BeginPlot("##MainPlot")) {
          ImPlot::SetupAxes("", "Luminance",
                  ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines,
                  ImPlotAxisFlags_AutoFit);

          ImPlot::SetupLegend(ImPlotLocation_NorthEast);

          ImPlot::SetupAxisLimits(ImAxis_X1, shared_x_min, shared_x_max);
          ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.1);

          if (!stats.radii.empty() && !stats.stddev_upper.empty() && !stats.stddev_lower.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Fill, ImVec4(0.2f, 0.6f, 1.0f, 0.5f));
            ImPlot::PlotShaded("+/-1s Confidence",
                     stats.radii.data(),
                     stats.stddev_lower.data(),
                     stats.stddev_upper.data(),
                     static_cast<int>(stats.radii.size()));
            ImPlot::PopStyleColor();
          }

          if (!stats.radii.empty() && !stats.mean.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
            ImPlot::SetNextLineStyle(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), 2.0f);
            ImPlot::PlotLine("Mean (mu)",
                     stats.radii.data(),
                     stats.mean.data(),
                     static_cast<int>(stats.radii.size()));
            ImPlot::PopStyleColor();
          }

          if (!stats.radii.empty() && !stats.ground_truth.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), 2.0f);
            ImPlot::PlotLine("Ground Truth",
                     stats.radii.data(),
                     stats.ground_truth.data(),
                     static_cast<int>(stats.radii.size()));
            ImPlot::PopStyleColor();
          }

          ImPlotRect limits = ImPlot::GetPlotLimits();
          shared_x_min = limits.X.Min;
          shared_x_max = limits.X.Max;

          ImPlot::EndPlot();
        }

        if (ImPlot::BeginPlot("##StddevPlot")) {
          ImPlot::SetupAxes("Radius (pixels)", "RSD (%)",
                  ImPlotAxisFlags_AutoFit,
                  ImPlotAxisFlags_AutoFit);

          ImPlot::SetupLegend(ImPlotLocation_NorthEast);

          ImPlot::SetupAxisLimits(ImAxis_X1, shared_x_min, shared_x_max);

          if (!stddev_percent.empty()) {
            float max_stddev_pct = *std::max_element(stddev_percent.begin(), stddev_percent.end());
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, max_stddev_pct * 1.1f);

            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
            ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), 2.0f);
            ImPlot::PlotLine("s/mu Ratio",
                     stats.radii.data(), stddev_percent.data(),
                     static_cast<int>(stats.radii.size()));
            ImPlot::PopStyleColor();
          }

          ImPlotRect limits = ImPlot::GetPlotLimits();
          shared_x_min = limits.X.Min;
          shared_x_max = limits.X.Max;

          ImPlot::EndPlot();
        }

        ImPlot::EndSubplots();
      }

      if (ImGui::CollapsingHeader("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!stats.mean.empty()) {
          float max_val = *std::max_element(stats.mean.begin(), stats.mean.end());
          float avg_val = std::accumulate(stats.mean.begin(), stats.mean.end(), 0.0f) / stats.mean.size();
          float avg_stddev = std::accumulate(stats.stddev.begin(), stats.stddev.end(), 0.0f) / stats.stddev.size();

          ImGui::Text("Peak Luminance (mu_max): %.4f", max_val);
          ImGui::Text("Average Luminance (mu_avg): %.4f", avg_val);
          ImGui::Text("Average Std Dev (s_avg): %.4f", avg_stddev);
          ImGui::Text("Data Points: %zu", stats.radii.size());

          if (!stats.ground_truth.empty() && stats.mean.size() == stats.ground_truth.size()) {
            float mse = 0.0f;
            for (size_t i = 0; i < stats.mean.size(); ++i) {
              float diff = stats.mean[i] - stats.ground_truth[i];
              mse += diff * diff;
            }
            mse /= stats.mean.size();
            ImGui::Text("MSE vs Ground Truth: %.6f", mse);
          }
        }
      }
    }
    ImGui::End();
  }
};

int main() {
  std::cout << "Starting RC Linear application with GPU acceleration..." << std::endl;

  try {
    constexpr int NUM_CASCADES = 8;
    const int baseProbeSize = 1;
    const float baseIntervalLength = 0.2f;

    // Dynamic sizing - will be updated based on window
    int RC_WIDTH = 512;
    int RC_HEIGHT = 512;
    
    // Initialize GLFW first
    if (!glfwInit()) {
      std::cerr << "Failed to initialize GLFW\n";
      return 1;
    }

    std::cout << "GLFW initialized successfully" << std::endl;

    // Request OpenGL 4.3 for compute shaders with compatibility profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);  // Allow resizing

    // Create window with temporary title
    std::string window_title = "RC Linear (OpenGL)";
    GLFWwindow* window = glfwCreateWindow(1280, 768, window_title.c_str(), nullptr, nullptr);
    if (!window) {
      std::cerr << "Failed to create GLFW window\n";
      glfwTerminate();
      return 1;
    }

    std::cout << "GLFW window created successfully" << std::endl;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Using the OpenGL context, check actual version
    const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::cout << "Requesting OpenGL 4.3 context, got version: " << gl_version << std::endl;

    // Update window title with actual OpenGL version
    window_title = "RC Linear (OpenGLv";
    window_title += gl_version;
    window_title += ")";
    glfwSetWindowTitle(window, window_title.c_str());

    // Initialize GLEW
    if (glewInit() != GLEW_OK) {
      std::cerr << "Failed to initialize GLEW" << std::endl;
      return 1;
    }

    std::cout << "GLEW initialized successfully" << std::endl;

    // Initialize GPU renderer
    std::cout << "Initializing GPU renderer..." << std::endl;
    g_gpu_renderer.initialize();

    // Function to regenerate RC scene when window size changes
    auto regenerate_rc_data = [&](int width, int height) {
      RC_WIDTH = width;
      RC_HEIGHT = height;
      glm::vec2 resolution(RC_WIDTH, RC_HEIGHT);

      std::cout << "Regenerating RC scene for " << width << "x" << height << std::endl;

      // Create scene with new dimensions
      Texture2D scene(RC_WIDTH, RC_HEIGHT);
      render_scene(scene, resolution);

      // Initialize cascades
      Texture2D cascadeA(RC_WIDTH, RC_HEIGHT), cascadeB(RC_WIDTH, RC_HEIGHT);
      cascadeA.clear(glm::vec4(0.0f));
      cascadeB.clear(glm::vec4(0.0f));

      Texture2D output(RC_WIDTH, RC_HEIGHT);
      output.clear(glm::vec4(0.0f));

      // Run RC passes
      Texture2D* readTex = &cascadeA;
      Texture2D* writeTex = &cascadeB;
      for (int i = NUM_CASCADES - 1; i >= 0; --i) {
        Texture2D& dst = (i == 0) ? output : *writeTex;
        run_rc_pass(
          baseProbeSize,
          baseIntervalLength,
          i,
          resolution,
          scene,
          *readTex,
          dst
        );
        if (i != 0) std::swap(readTex, writeTex);
      }

      return std::make_tuple(std::move(output), std::move(scene));
    };

    // Initial generation
    auto [output, scene] = regenerate_rc_data(512, 512);

    std::cout << "Initial RC generation complete, initializing ImGui/ImPlot..." << std::endl;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    std::cout << "ImGui/ImPlot initialized successfully" << std::endl;

    // Create OpenGL texture
    GLuint texID = 0;
    glGenTextures(1, &texID);

    auto upload_texture = [&](const Texture2D& tex) {
      std::vector<unsigned char> rgba = texture_to_rgba(tex);
      glBindTexture(GL_TEXTURE_2D, texID);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width(), tex.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    };

    upload_texture(output);

    RadialStats stats = compute_radial_stats(output);
    ImPlotChartRenderer chartRenderer;

    std::cout << "Entering main loop..." << std::endl;

    // Track window size for dynamic updates
    int last_window_width = 0, last_window_height = 0;
    bool needs_regeneration = false;
    float regeneration_timer = 0.0f;
    const float REGENERATION_DELAY = 0.5f; // Wait 0.5s after resize before regenerating

    // Define ImGui panel width and dimensions
    const int IMGUI_PANEL_WIDTH = 520;
    const int MIN_RC_WIDTH = 200;
    const int MIN_RC_HEIGHT = 200;
    const int PADDING = 10;

    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      // Check for window size changes
      int window_width, window_height;
      glfwGetWindowSize(window, &window_width, &window_height);
      
      if (window_width != last_window_width || window_height != last_window_height) {
        last_window_width = window_width;
        last_window_height = window_height;
        needs_regeneration = true;
        regeneration_timer = REGENERATION_DELAY;
        std::cout << "Window resized to " << window_width << "x" << window_height << std::endl;
      }

      // Handle delayed regeneration
      if (needs_regeneration) {
        regeneration_timer -= io.DeltaTime;
        if (regeneration_timer <= 0.0f) {
          // Calculate RC resolution based on available area (full window minus ImGui panel)
          int available_width = std::max(MIN_RC_WIDTH, window_width - IMGUI_PANEL_WIDTH - (PADDING * 3));
          int available_height = std::max(MIN_RC_HEIGHT, window_height - (PADDING * 2));
          
          // Scale down for performance if too large
          int rc_width = std::min(available_width, 1024);  // Max 1024 width
          int rc_height = std::min(available_height, 768); // Max 768 height
          
          std::tie(output, scene) = regenerate_rc_data(rc_width, rc_height);
          upload_texture(output);
          stats = compute_radial_stats(output);
          needs_regeneration = false;
        }
      }

      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      glClearColor(0.1f, 0.1f, 0.1f, 1.0f);  // Dark gray background
      glClear(GL_COLOR_BUFFER_BIT);

      int display_w, display_h;
      glfwGetFramebufferSize(window, &display_w, &display_h);

      // Calculate RC display area (spans available area, accounting for anchored panel)
      int rc_display_width = display_w - IMGUI_PANEL_WIDTH - (PADDING * 3); // Account for panel and padding
      int rc_display_height = display_h - (PADDING * 2); // Top and bottom padding
      
      int rc_x_offset = PADDING;
      int rc_y_offset = PADDING;

      // Ensure minimum size
      rc_display_width = std::max(rc_display_width, MIN_RC_WIDTH);
      rc_display_height = std::max(rc_display_height, MIN_RC_HEIGHT);

      // Set viewport for RC display (full rectangle)
      glViewport(rc_x_offset, rc_y_offset, rc_display_width, rc_display_height);
      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      glOrtho(0, 1, 0, 1, -1, 1); // Normalized coordinates
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();

      // Draw RC output texture (normalized coordinates)
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, texID);
      glColor3f(1.0f, 1.0f, 1.0f);
      glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
      glEnd();
      glDisable(GL_TEXTURE_2D);

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
        glVertex2f(rc_x_offset, rc_y_offset);
        glVertex2f(rc_x_offset + rc_display_width, rc_y_offset);
        glVertex2f(rc_x_offset + rc_display_width, rc_y_offset + rc_display_height);
        glVertex2f(rc_x_offset, rc_y_offset + rc_display_height);
      glEnd();

      // Show status indicator when regenerating
      if (needs_regeneration) {
        glColor3f(1.0f, 1.0f, 0.0f);
        glPointSize(10.0f);
        glBegin(GL_POINTS);
          glVertex2f(rc_x_offset + 20, rc_y_offset + rc_display_height - 20);
        glEnd();
      }

      chartRenderer.render(stats);

      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

      glfwSwapBuffers(window);
    }

    std::cout << "Cleaning up..." << std::endl;

    glDeleteTextures(1, &texID);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "Application finished successfully" << std::endl;

  } catch (const std::exception& e) {
    std::cerr << "Exception caught: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Unknown exception caught" << std::endl;
    return 1;
  }

  return 0;
}
