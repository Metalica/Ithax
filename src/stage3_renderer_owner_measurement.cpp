#include "thread_budget.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include <StdAfx.h>
#include <Tr2PrimaryRenderContextAL.h>
#include <Tr2TextureAL.h>
#include <Tr2AdapterStructures.h>
#include <Tr2VideoAdapterInfoAL.h>
#include <ALLog.h>

extern "C" __declspec(dllimport) int
    AmdPowerXpressRequestHighPerformance;
const char *g_moduleName = "ithax-stage3-renderer";
namespace {

constexpr std::uint32_t TARGET_WIDTH = 640U;
constexpr std::uint32_t TARGET_HEIGHT = 480U;
constexpr std::uint32_t GRAPHICS_FLUSH_COUNT = 16U;
constexpr std::uint32_t TEXTURE_COUNT = 4U;
constexpr std::uint32_t TEXTURE_EDGE = 64U;
constexpr std::uint32_t PIXEL_FORMAT_BC7 = 98U;
constexpr std::uint32_t PIXEL_FORMAT_BGRA8 = 87U;
constexpr std::chrono::milliseconds WINDOW_MSG_WAIT{100};
constexpr std::chrono::milliseconds DRAIN_SETTLE_WAIT{25};

class RendererError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

void Require(const bool condition, const char *message) {
  if (!condition) {
    throw RendererError(message);
  }
}

void PrintJsonString(const std::string_view value) {
  std::cout << '"';
  for (const char character : value) {
    if (character == '\\' || character == '"') {
      std::cout << '\\';
    }
    std::cout << character;
  }
  std::cout << '"';
}

void PrintSnapshot(const ithax::ThreadBudgetSnapshot &snapshot) {
  std::cout << "{\"event\":\"stage3_renderer_sample\",\"phase\":";
  PrintJsonString(snapshot.phase);
  std::cout << ",\"owner\":";
  PrintJsonString(snapshot.owner);
  std::cout << ",\"subsystem\":";
  PrintJsonString(snapshot.subsystem);
  std::cout << ",\"reservation\":";
  PrintJsonString(ithax::ReservationKindName(snapshot.reservation));
  std::cout << ",\"configured_threads\":"
            << snapshot.configured_threads
            << ",\"observed_threads\":" << snapshot.observed_threads
            << ",\"peak_observed_threads\":"
            << snapshot.peak_observed_threads
            << ",\"process_cpu_set_count\":"
            << snapshot.process_cpu_set_count << ",\"cpu_set_source\":";
  PrintJsonString(snapshot.cpu_set_source);
  std::cout << ",\"timestamp_utc\":";
  PrintJsonString(snapshot.timestamp_utc);
  std::cout << "}\n";
}

HWND CreateHiddenWindow() {
  static const char *const windowClass = "ithax-stage3-renderer-window";
  static bool classRegistered = false;
  if (!classRegistered) {
    WNDCLASSA windowClassInfo{};
    windowClassInfo.style = CS_HREDRAW | CS_VREDRAW;
    windowClassInfo.lpfnWndProc = DefWindowProcA;
    windowClassInfo.hInstance = GetModuleHandleA(nullptr);
    windowClassInfo.lpszClassName = windowClass;
    Require(RegisterClassA(&windowClassInfo) != 0,
            "Failed to register the hidden renderer window class");
    classRegistered = true;
  }
  const HWND window = CreateWindowExA(
      0, windowClass, "ithax stage3 renderer",
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
      static_cast<int>(TARGET_WIDTH), static_cast<int>(TARGET_HEIGHT),
      nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
  Require(window != nullptr, "Failed to create the hidden renderer window");
  return window;
}

Tr2PresentParametersAL MakePresentParameters(const HWND window,
                                             const bool software) {
  Tr2PresentParametersAL parameters{};
  parameters.mode.width = TARGET_WIDTH;
  parameters.mode.height = TARGET_HEIGHT;
  parameters.mode.format =
      static_cast<Tr2RenderContextEnum::PixelFormat>(PIXEL_FORMAT_BGRA8);
  parameters.mode.refreshRateNumerator = 0U;
  parameters.mode.refreshRateDenominator = 0U;
  parameters.mode.scanlineOrdering =
      Tr2RenderContextEnum::SCANLINE_ORDER_UNSPECIFIED;
  parameters.mode.scaling =
      Tr2RenderContextEnum::DISPLAY_SCALING_UNSPECIFIED;
  parameters.backBufferCount = 1U;
  parameters.msaaType = 1U;
  parameters.msaaQuality = 0U;
  parameters.swapEffect = Tr2RenderContextEnum::SWAP_EFFECT_DISCARD;
  parameters.outputWindow = window;
  parameters.windowed = true;
  parameters.software = software;
  parameters.presentInterval =
      Tr2RenderContextEnum::PRESENT_INTERVAL_IMMEDIATE;
  parameters.variableRefreshRateSupported = false;
  return parameters;
}

std::uint32_t CountHardwareAdapters() {
  std::uint32_t count = 0U;
  Tr2VideoAdapterInfo::GetAdapterCount(count);
  return count;
}

void RunDeviceWorkload(Tr2PrimaryRenderContextAL &renderContext,
                       const std::string_view laneName) {
  Tr2BitmapDimensions dimensions(
      TEXTURE_EDGE, TEXTURE_EDGE, 1U,
      static_cast<Tr2RenderContextEnum::PixelFormat>(PIXEL_FORMAT_BC7));
  for (std::uint32_t index = 0U; index < TEXTURE_COUNT; ++index) {
    {
      Tr2TextureAL texture;
      const ALResult create_result = texture.Create(
          dimensions, Tr2GpuUsage::SHADER_RESOURCE,
          Tr2CpuUsage::WRITE, renderContext);
      Require(SUCCEEDED(create_result),
              "Trinity AL texture creation failed");
      Require(texture.IsValid(),
              "Trinity AL texture is invalid after create");
      for (std::uint32_t flush = 0U; flush < GRAPHICS_FLUSH_COUNT;
           ++flush) {
        renderContext.m_context->Flush();
      }
    }
  }
  std::cout << "{\"event\":\"stage3_renderer_workload\",\"lane\":";
  PrintJsonString(laneName);
  std::cout << ",\"status\":\"pass\",\"textures\":" << TEXTURE_COUNT
            << ",\"flushes\":" << (TEXTURE_COUNT * GRAPHICS_FLUSH_COUNT)
            << "}\n";
}

struct RendererLaneResult {
  bool passed = false;
  std::string_view detail;
  std::uint32_t adapters = 0U;
};

RendererLaneResult RunRendererLane(
    ithax::ThreadBudget &budget, const std::string_view laneName,
    const bool software, const HWND window,
    const std::uint32_t adapterIndex) {
  const auto before = budget.Capture(
      std::string("renderer_before_") + std::string(laneName),
      "trinity-renderer", "trinityal-dx11", 0U,
      ithax::ReservationKind::Unknown);
  PrintSnapshot(before);

  Tr2PrimaryRenderContextAL renderContext;
  Require(!renderContext.IsValid(),
          "Trinity AL render context was valid before CreateDevice");
  const Tr2PresentParametersAL parameters =
      MakePresentParameters(window, software);
  const ALResult create_result = renderContext.CreateDevice(
      adapterIndex, window, parameters);
  const bool device_created = SUCCEEDED(create_result);
  const auto active = budget.Capture(
      std::string("renderer_active_") + std::string(laneName),
      "trinity-renderer", "trinityal-dx11",
      device_created ? 1U : 0U, ithax::ReservationKind::Unknown);
  PrintSnapshot(active);
  if (!device_created) {
    const auto failed = budget.Capture(
        std::string("renderer_failed_") + std::string(laneName),
        "trinity-renderer", "trinityal-dx11", 0U,
        ithax::ReservationKind::Unknown);
    PrintSnapshot(failed);
    return {false, "create-failed", CountHardwareAdapters()};
  }
  Require(renderContext.IsValid(),
          "Trinity AL render context was invalid after CreateDevice");
  RunDeviceWorkload(renderContext, laneName);
  renderContext.Destroy();
  Require(!renderContext.IsValid(),
          "Trinity AL render context survived its bounded teardown");
  std::this_thread::sleep_for(DRAIN_SETTLE_WAIT);
  const auto drained = budget.Capture(
      std::string("renderer_drained_") + std::string(laneName),
      "trinity-renderer", "trinityal-dx11", 0U,
      ithax::ReservationKind::Unknown);
  PrintSnapshot(drained);
  return {true, "pass", CountHardwareAdapters()};
}

}  // namespace

int main() {
  try {
    ithax::ThreadBudgetPolicy policy;
    policy.hard_reservations = 1U;
    policy.soft_reservations = 0U;
    policy.headroom = 1U;
    ithax::ThreadBudget budget(policy);
    const auto baseline = budget.Capture(
        "renderer_measurement_baseline", "main-owner", "stage3-renderer",
        0U, ithax::ReservationKind::Hard);
    PrintSnapshot(baseline);

    const HWND window = CreateHiddenWindow();
    bool warp_passed = false;
    bool hardware_passed = false;
    std::string_view hardware_detail = "not-attempted";
    std::uint32_t adapterCount = 0U;

    const RendererLaneResult warp =
        RunRendererLane(budget, "trinity-dx11-windowless-warp",
                        true, nullptr, 0U);
    warp_passed = warp.passed;
    adapterCount = warp.adapters;

    if (adapterCount > 0U) {
      const RendererLaneResult hardware = RunRendererLane(
          budget, "trinity-dx11-hardware-windowed", false, window, 0U);
      hardware_passed = hardware.passed;
      hardware_detail = hardware.detail;
    } else {
      hardware_detail = "no-hardware-adapter";
    }

    DestroyWindow(window);

    const bool required_passed = warp_passed;
    const bool all_passed = warp_passed &&
                            (adapterCount == 0U || hardware_passed);
    std::cout << "{\"event\":\"trinity_renderer_owner_summary\","
              << "\"status\":\""
              << (all_passed ? "pass" : "fail") << "\",\"gate_status\":\""
              << (required_passed ? "pass" : "open") << "\","
              << "\"measurement_class\":\"real-provider\","
              << "\"provider\":\"TrinityAL_dx11\","
              << "\"hardware_adapters\":" << adapterCount << ","
              << "\"windowless_warp\":\""
              << (warp_passed ? "pass" : "fail") << "\","
              << "\"hardware_windowed\":\""
              << (hardware_passed ? "pass" : "fail") << "\","
              << "\"hardware_detail\":";
    PrintJsonString(hardware_detail);
    std::cout << ",\"workload\":\"bounded-create-device-teardown\"}\n";
    return required_passed ? EXIT_SUCCESS : EXIT_FAILURE;
  } catch (const std::exception &error) {
    std::cerr << "Stage 3 renderer measurement failed: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
