#include <cstdio>
#include <GLFW/glfw3.h>
#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

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
#include "imgui_impl_opengl2.h"
#include "implot.h"

// #include "texture.hpp"
#include "texture_simd.hpp"
#include "scene.hpp"
// #include "rc.hpp"
#include "rc_simd.hpp"

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
  
  float disk_radius = 7.5f;
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
    
    ImGui::SetNextWindowPos(ImVec2(520, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, 512), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Luminance Falloff Analysis", nullptr, ImGuiWindowFlags_NoCollapse)) {
      
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
  std::cout << "Starting RC Linear application with ImPlot..." << std::endl;
  
  try {
    constexpr int NUM_CASCADES = 8;
    const int baseProbeSize = 1;
    const float baseIntervalLength = 0.2f;

    const int W = 512;
    const int H = 512;
    glm::vec2 resolution(W, H);

    std::cout << "Initializing scene..." << std::endl;
    
    Texture2D scene(W, H);
    render_scene(scene, resolution);

    std::cout << "Scene rendered, initializing cascades..." << std::endl;

    Texture2D cascadeA(W, H), cascadeB(W, H);
    cascadeA.clear(glm::vec4(0.0f));
    cascadeB.clear(glm::vec4(0.0f));

    Texture2D output(W, H);
    output.clear(glm::vec4(0.0f));

    std::cout << "Running radiance cascades..." << std::endl;

    Texture2D* readTex = &cascadeA;
    Texture2D* writeTex = &cascadeB;
    for (int i = NUM_CASCADES - 1; i >= 0; --i) {
      std::cout << "Processing cascade " << i << std::endl;
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

    std::cout << "Radiance cascades complete, initializing GLFW..." << std::endl;

    if (!glfwInit()) {
      std::cerr << "Failed to initialize GLFW\n";
      return 1;
    }
    
    std::cout << "GLFW initialized successfully" << std::endl;
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    
    GLFWwindow* window = glfwCreateWindow(1024, 512, "RC Linear Viewer", nullptr, nullptr);
    if (!window) {
      std::cerr << "Failed to create GLFW window\n";
      glfwTerminate();
      return 1;
    }
    
    std::cout << "GLFW window created successfully" << std::endl;
    
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    std::cout << "Initializing ImGui/ImPlot..." << std::endl;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    
    std::cout << "Using default font" << std::endl;
    
    ImGui::StyleColorsDark();
    
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    std::cout << "ImGui/ImPlot initialized successfully" << std::endl;

    std::cout << "Preparing textures..." << std::endl;

    std::vector<unsigned char> rgba = texture_to_rgba(output);

    GLuint texID = 0;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    std::cout << "Computing statistics..." << std::endl;

    RadialStats stats = compute_radial_stats(output);
    
    ImPlotChartRenderer chartRenderer;
    
    std::cout << "Entering main loop..." << std::endl;

    int frame_count = 0;
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      ImGui_ImplOpenGL2_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      
      int display_w, display_h;
      glfwGetFramebufferSize(window, &display_w, &display_h);
      
      glViewport(0, 0, W, H);
      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      glOrtho(0, W, 0, H, -1, 1);
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();

      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, texID);
      glColor3f(1.0f, 1.0f, 1.0f);
      glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(float(W), 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(float(W), float(H));
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, float(H));
      glEnd();
      glDisable(GL_TEXTURE_2D);

      glViewport(0, 0, display_w, display_h);

      chartRenderer.render(stats);

      ImGui::Render();
      ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

      glfwSwapBuffers(window);
      frame_count++;
    }

    std::cout << "Cleaning up..." << std::endl;

    glDeleteTextures(1, &texID);
    
    ImGui_ImplOpenGL2_Shutdown();
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
