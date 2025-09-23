#pragma once

#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

#include "imgui.h"
#include "implot.h"

#include "stats.hpp"

// Shared hover state between plots and the RC overlay
struct HoverSync {
  bool  active = false;  // true if any view hovered this frame
  float radius = 0.0f;   // radius in pixels
};

// Polyfill for ImPlot::PlotVLines if not available
static inline void PlotVLineCompat(const char* label, float x) {
  ImPlotRect lim = ImPlot::GetPlotLimits();
  float xs[2] = {x, x};
  float ys[2] = {(float)lim.Y.Min, (float)lim.Y.Max};
  ImPlot::PlotLine(label, xs, ys, 2);
}

class ImPlotChartRenderer {
private:
  static inline double shared_x_min = 0.0;
  static inline double shared_x_max = 100.0;

  static float clamp_radius_from_plot(const RadialStats& stats, double x) {
    if (stats.radii.empty()) return 0.0f;
    double xmin = 0.0, xmax = (double)stats.radii.back();
    if (x < xmin) x = xmin;
    if (x > xmax) x = xmax;
    return (float)x;
  }

  static void draw_marker_and_tooltip(const RadialStats& stats, float x, bool hovered) {
    // Always draw the marker when sync is active
    ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
    PlotVLineCompat("##hover_x", x);
    ImPlot::PopStyleColor();

    // Tooltip only when this plot is hovered
    if (hovered && !stats.radii.empty()) {
      int r = (int)std::round(x);
      r = std::clamp(r, 0, (int)stats.radii.size() - 1);
      ImGui::BeginTooltip();
      ImGui::Text("r = %d px", r);
      if (r >= 0 && r < (int)stats.mean.size())
        ImGui::Text("mu = %.4f, s = %.4f", stats.mean[r], stats.stddev[r]);
      ImGui::EndTooltip();
    }
  }

public:
  ImPlotChartRenderer() = default;

  // Renders two linked plots and updates/consumes HoverSync:
  // - If either plot is hovered, it sets sync.active=true and updates sync.radius.
  // - Regardless of hover source, both plots draw a vertical marker when sync.active is true.
  // - Tooltip behavior remains contextual: shown only when a given plot is hovered.
  void render(const RadialStats& stats, HoverSync& sync) {
    std::vector<float> stddev_percent(stats.stddev.size(), 0.0f);
    for (size_t i = 0; i < stats.stddev.size(); ++i) {
      if (stats.mean[i] != 0.0f) stddev_percent[i] = (stats.stddev[i] / stats.mean[i]) * 100.0f;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display_size = io.DisplaySize;

    const float panel_width = 520.0f;
    const float panel_height = display_size.y - 20.0f;
    const float padding = 10.0f;

    ImVec2 window_pos(display_size.x - panel_width - padding, padding);
    ImVec2 window_size(panel_width, panel_height);

    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);

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
        // Main plot
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

          // Hover handling on main plot: update shared sync
          bool hovered_plot = ImPlot::IsPlotHovered();
          if (hovered_plot) {
            ImPlotPoint mp = ImPlot::GetPlotMousePos();
            sync.active = true;
            sync.radius = clamp_radius_from_plot(stats, mp.x);
          }
          // Draw marker in this plot if any view activated the sync
          if (sync.active) {
            draw_marker_and_tooltip(stats, sync.radius, hovered_plot);
          }

          ImPlotRect limits = ImPlot::GetPlotLimits();
          shared_x_min = limits.X.Min;
          shared_x_max = limits.X.Max;

          ImPlot::EndPlot();
        }

        // Stddev plot
        if (ImPlot::BeginPlot("##StddevPlot")) {
          ImPlot::SetupAxes("Radius (pixels)", "RSD (%)",
                            ImPlotAxisFlags_AutoFit,
                            ImPlotAxisFlags_AutoFit);

          ImPlot::SetupLegend(ImPlotLocation_NorthEast);
          ImPlot::SetupAxisLimits(ImAxis_X1, shared_x_min, shared_x_max);

          if (!stats.stddev.empty()) {
            // Precompute percentage if needed
            std::vector<float> stddev_percent(stats.stddev.size(), 0.0f);
            for (size_t i = 0; i < stats.stddev.size(); ++i) {
              if (stats.mean[i] != 0.0f) stddev_percent[i] = (stats.stddev[i] / stats.mean[i]) * 100.0f;
            }
            float max_stddev_pct = *std::max_element(stddev_percent.begin(), stddev_percent.end());
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, std::max(1.0f, max_stddev_pct * 1.1f));

            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
            ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), 2.0f);
            ImPlot::PlotLine("s/mu Ratio",
                             stats.radii.data(), stddev_percent.data(),
                             static_cast<int>(stats.radii.size()));
            ImPlot::PopStyleColor();
          }

          // Hover handling on stddev plot: update shared sync
          bool hovered_plot = ImPlot::IsPlotHovered();
          if (hovered_plot) {
            ImPlotPoint mp = ImPlot::GetPlotMousePos();
            sync.active = true;
            sync.radius = clamp_radius_from_plot(stats, mp.x);
          }
          // Draw marker in this plot if any view activated the sync
          if (sync.active) {
            draw_marker_and_tooltip(stats, sync.radius, hovered_plot);
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
