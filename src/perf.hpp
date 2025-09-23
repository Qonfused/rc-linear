#pragma once

#include <cstdint>
#include <chrono>
#include <cstdio>

#define GLEW_STATIC
#include <GL/glew.h>

#include "imgui.h"

struct CpuTimer {
  std::chrono::high_resolution_clock::time_point t0;
  inline void start() { t0 = std::chrono::high_resolution_clock::now(); }
  inline double stop_ms() const {
    auto t1 = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
  }
};

struct QueryPair {
  GLuint id[2] = {0, 0};
  int    write = 0;      // index used for glBeginQuery this frame
  bool   primed = false;
};

class Perf {
public:
  // CPU timings (ms)
  double cpu_frame_ms = 0.0;
  double cpu_rc_ms    = 0.0;
  double cpu_copy_ms  = 0.0;
  double cpu_stats_ms = 0.0;

  // GPU timings (ms) - resolved from previous frame's queries (non-blocking)
  double gpu_rc_ms    = 0.0;
  double gpu_copy_ms  = 0.0;
  double gpu_stats_ms = 0.0;

  // FPS (EMA)
  double fps = 0.0;

  // Queries
  QueryPair q_rc;
  QueryPair q_copy;
  QueryPair q_stats;

  // CPU timers
  CpuTimer frame_timer;
  CpuTimer rc_timer;
  CpuTimer copy_timer;
  CpuTimer stats_timer;

public:
  inline void init() {
    glGenQueries(2, q_rc.id);
    glGenQueries(2, q_copy.id);
    glGenQueries(2, q_stats.id);
  }

  inline void shutdown() {
    glDeleteQueries(2, q_rc.id);
    glDeleteQueries(2, q_copy.id);
    glDeleteQueries(2, q_stats.id);
  }

  // FPS smoothing, call once per frame with delta time
  inline void beginFrame(double dt) {
    frame_timer.start();
    double inst = dt > 0.0 ? (1.0 / dt) : 0.0;
    fps = (fps == 0.0) ? inst : (0.9 * fps + 0.1 * inst);
  }
  inline void endFrame() { cpu_frame_ms = frame_timer.stop_ms(); }

  // CPU section helpers
  inline void beginCpuRC()   { rc_timer.start(); }
  inline void endCpuRC()     { cpu_rc_ms   = rc_timer.stop_ms(); }
  inline void beginCpuCopy() { copy_timer.start(); }
  inline void endCpuCopy()   { cpu_copy_ms = copy_timer.stop_ms(); }
  inline void beginCpuStats(){ stats_timer.start(); }
  inline void endCpuStats()  { cpu_stats_ms= stats_timer.stop_ms(); }

  // GPU query helpers (generic + section-specific)
  static inline void beginGpu(QueryPair& qp) {
    glBeginQuery(GL_TIME_ELAPSED, qp.id[qp.write]);
  }
  static inline void endGpu(QueryPair& qp) {
    glEndQuery(GL_TIME_ELAPSED);
    qp.primed = true;
    qp.write ^= 1; // flip buffer
  }

  inline void beginGpuRC()    { beginGpu(q_rc); }
  inline void endGpuRC()      { endGpu(q_rc); }
  inline void beginGpuCopy()  { beginGpu(q_copy); }
  inline void endGpuCopy()    { endGpu(q_copy); }
  inline void beginGpuStats() { beginGpu(q_stats); }
  inline void endGpuStats()   { endGpu(q_stats); }

  // Resolve available GPU timings without stalling
  inline void resolveAll() {
    resolveOne(q_rc,    gpu_rc_ms);
    resolveOne(q_copy,  gpu_copy_ms);
    resolveOne(q_stats, gpu_stats_ms);
  }

  // Overlay drawer anchored to the RC viewport (top-left), using ImGui foreground list.
  // Parameters:
  // - display_h: framebuffer height in pixels
  // - rc_x, rc_y: bottom-left position of RC viewport in framebuffer space
  // - rc_w, rc_h: RC viewport size
  // - frame_counter: running frame index
  // - clamp_to_rc: if true, clips overlay inside RC bounds
  inline void drawOverlay(int display_h,
                          int rc_x, int rc_y, int rc_w, int rc_h,
                          uint64_t frame_counter,
                          bool clamp_to_rc = true) const
  {
    const float rc_left = (float)rc_x;
    const float rc_top  = (float)display_h - (float)(rc_y + rc_h);

    char lines[256];
    std::snprintf(lines, sizeof(lines),
                  "Frame: %llu\nFPS: %.1f\n"
                  "CPU frame: %.2f ms\nCPU rc/copy/stats: %.2f / %.2f / %.2f ms\n"
                  "GPU rc/copy/stats: %.2f / %.2f / %.2f ms",
                  (unsigned long long)frame_counter,
                  fps,
                  cpu_frame_ms, cpu_rc_ms, cpu_copy_ms, cpu_stats_ms,
                  gpu_rc_ms, gpu_copy_ms, gpu_stats_ms);

    ImVec2 text_pos(rc_left + 8.0f, rc_top + 8.0f);
    ImVec2 text_size = ImGui::CalcTextSize(lines);
    ImVec2 pad(8.0f, 6.0f);
    ImVec2 rect_min(text_pos.x - pad.x, text_pos.y - pad.y);
    ImVec2 rect_max(text_pos.x + text_size.x + pad.x, text_pos.y + text_size.y + pad.y);

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    if (clamp_to_rc) {
      ImVec2 clip_min((float)rc_x, rc_top);
      ImVec2 clip_max((float)(rc_x + rc_w), rc_top + rc_h);
      fg->PushClipRect(clip_min, clip_max, true);
      fg->AddRectFilled(rect_min, rect_max, IM_COL32(0, 0, 0, 140), 4.0f);
      fg->AddText(text_pos, IM_COL32(255, 255, 255, 255), lines);
      fg->PopClipRect();
    } else {
      fg->AddRectFilled(rect_min, rect_max, IM_COL32(0, 0, 0, 140), 4.0f);
      fg->AddText(text_pos, IM_COL32(255, 255, 255, 255), lines);
    }
  }

private:
  static inline void resolveOne(QueryPair& qp, double& out_ms) {
    if (!qp.primed) return;
    GLuint prev = qp.id[qp.write ^ 1]; // previous (most recently ended)
    GLuint available = 0;
    glGetQueryObjectuiv(prev, GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) return;
    GLuint64 ns = 0;
    glGetQueryObjectui64v(prev, GL_QUERY_RESULT, &ns);
    const double ms = double(ns) / 1.0e6;
    out_ms = (out_ms == 0.0) ? ms : (0.8 * out_ms + 0.2 * ms);
  }
};
