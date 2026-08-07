#include "thread_budget.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_arena.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include <tracy/Tracy.hpp>

extern "C" __declspec(dllimport) int
    AmdPowerXpressRequestHighPerformance;

namespace {

constexpr std::size_t TBB_ITEM_COUNT = 512U;
constexpr std::size_t TBB_GRAIN_SIZE = 1U;
constexpr std::uint32_t MAX_OWNER_WORKERS = 4U;
constexpr std::uint32_t TRACY_MARK_COUNT = 64U;
constexpr std::uint32_t GRAPHICS_FLUSH_COUNT = 16U;
constexpr std::chrono::milliseconds OWNER_WAIT{5'000};
constexpr std::chrono::milliseconds TRACY_SETTLE_WAIT{50};

class OwnerMeasurementError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

void Require(const bool condition, const char *message) {
  if (!condition) {
    throw OwnerMeasurementError(message);
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
  std::cout << "{\"event\":\"stage3_owner_sample\",\"phase\":";
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

struct TbbGate {
  std::condition_variable condition;
  std::mutex mutex;
  std::size_t active = 0U;
  bool failed = false;
  bool release = false;
};

void ReleaseTbbGate(TbbGate &gate) {
  {
    std::lock_guard lock(gate.mutex);
    gate.release = true;
  }
  gate.condition.notify_all();
}

void RethrowThreadError(const std::exception_ptr &error) {
  if (error) {
    std::rethrow_exception(error);
  }
}

void RunTbbMeasurement(ithax::ThreadBudget &budget) {
  const auto before = budget.Capture(
      "one_tbb_before", "trinity-oneTBB", "oneTBB", 0U,
      ithax::ReservationKind::Unknown);
  PrintSnapshot(before);
  const auto worker_count = std::min(
      MAX_OWNER_WORKERS, budget.AvailableWorkerCount());
  Require(worker_count > 0U, "oneTBB measurement has no worker headroom");

  TbbGate gate;
  std::vector<std::uint64_t> values(TBB_ITEM_COUNT, 0U);
  std::exception_ptr worker_error;
  std::thread launcher([&]() {
    try {
      oneapi::tbb::global_control control(
          oneapi::tbb::global_control::max_allowed_parallelism,
          worker_count);
      oneapi::tbb::task_arena arena(worker_count);
      arena.execute([&]() {
        oneapi::tbb::parallel_for(
            oneapi::tbb::blocked_range<std::size_t>(
                0U, TBB_ITEM_COUNT, TBB_GRAIN_SIZE),
            [&](const oneapi::tbb::blocked_range<std::size_t> &range) {
              {
                std::unique_lock lock(gate.mutex);
                ++gate.active;
                gate.condition.notify_all();
                gate.condition.wait(lock, [&]() { return gate.release; });
              }
              for (std::size_t index = range.begin(); index < range.end();
                   ++index) {
                values[index] = static_cast<std::uint64_t>(index) ^
                                static_cast<std::uint64_t>(worker_count);
              }
            });
      });
    } catch (...) {
      {
        std::lock_guard lock(gate.mutex);
        worker_error = std::current_exception();
        gate.failed = true;
        gate.release = true;
      }
      gate.condition.notify_all();
    }
  });

  std::exception_ptr phase_error;
  try {
    std::unique_lock lock(gate.mutex);
    const bool active = gate.condition.wait_for(
        lock, OWNER_WAIT, [&]() { return gate.active > 0U || gate.failed; });
    if (!active) {
      throw OwnerMeasurementError("oneTBB did not reach its active phase");
    }
    if (gate.failed) {
      throw OwnerMeasurementError("oneTBB worker phase failed");
    }
    const auto active_snapshot = budget.Capture(
        "one_tbb_active", "trinity-oneTBB", "oneTBB", worker_count,
        ithax::ReservationKind::Unknown);
    PrintSnapshot(active_snapshot);
    Require(active_snapshot.observed_threads > before.observed_threads,
            "oneTBB did not create an observable worker phase");
  } catch (...) {
    phase_error = std::current_exception();
  }

  ReleaseTbbGate(gate);
  launcher.join();
  RethrowThreadError(worker_error);
  RethrowThreadError(phase_error);
  for (std::size_t index = 0U; index < values.size(); ++index) {
    const auto expected = static_cast<std::uint64_t>(index) ^ worker_count;
    Require(values[index] == expected, "oneTBB result validation failed");
  }
  const auto drained = budget.Capture(
      "one_tbb_drained", "trinity-oneTBB", "oneTBB", worker_count,
      ithax::ReservationKind::Unknown);
  PrintSnapshot(drained);
  std::cout << "{\"event\":\"one_tbb_owner_summary\","
            << "\"status\":\"pass\",\"gate_status\":\"pass\","
            << "\"measurement_class\":\"runtime-workload\","
            << "\"configured_workers\":" << worker_count
            << ",\"workload\":\"parallel_for\"}\n";
}

void RunTracyMeasurement(ithax::ThreadBudget &budget) {
  const auto before = budget.Capture(
      "tracy_before", "tracy", "profiler", 0U,
      ithax::ReservationKind::Unknown);
  PrintSnapshot(before);
  tracy::StartupProfiler();
  for (std::uint32_t mark = 0U; mark < TRACY_MARK_COUNT; ++mark) {
    ZoneScopedN("stage3_owner_profiled_work");
    FrameMark;
  }
  std::this_thread::sleep_for(TRACY_SETTLE_WAIT);
  const auto active = budget.Capture(
      "tracy_active", "tracy", "profiler", 1U,
      ithax::ReservationKind::Unknown);
  PrintSnapshot(active);
  const bool started = tracy::IsProfilerStarted();
  if (started) {
    tracy::ShutdownProfiler();
  }
  const auto drained = budget.Capture(
      "tracy_drained", "tracy", "profiler", 0U,
      ithax::ReservationKind::Unknown);
  PrintSnapshot(drained);
  std::cout << "{\"event\":\"tracy_owner_summary\","
            << "\"status\":\"" << (started ? "pass" : "unavailable")
            << "\",\"gate_status\":\"" << (started ? "pass" : "open")
            << "\",\"measurement_class\":\"profiler-lifecycle\","
            << "\"profiler_started\":"
            << (started ? "true" : "false") << "}\n";
}

void RunTrinityStubMeasurement(ithax::ThreadBudget &budget) {
  const auto before = budget.Capture(
      "trinity_stub_before", "trinity-renderer", "trinity-stub", 0U,
      ithax::ReservationKind::Unknown);
  PrintSnapshot(before);
  Require(AmdPowerXpressRequestHighPerformance == 1,
          "Trinity stub did not load");
  const auto active = budget.Capture(
      "trinity_stub_active", "trinity-renderer", "trinity-stub", 0U,
      ithax::ReservationKind::Unknown);
  PrintSnapshot(active);
  const auto drained = budget.Capture(
      "trinity_stub_drained", "trinity-renderer", "trinity-stub", 0U,
      ithax::ReservationKind::Unknown);
  PrintSnapshot(drained);
  std::cout << "{\"event\":\"trinity_owner_summary\","
            << "\"status\":\"load_only\",\"gate_status\":\"open\","
            << "\"measurement_class\":\"stub-load\","
            << "\"workload\":\"stub-no-device\","
            << "\"render_driver_threads\":\"not-exercised\"}\n";
}

void PrintGraphicsUnavailable(const char *phase, const HRESULT error) {
  std::cout << "{\"event\":\"graphics_driver_summary\","
            << "\"status\":\"unavailable\",\"gate_status\":\"open\","
            << "\"measurement_class\":\"device-create\",\"phase\":";
  PrintJsonString(phase);
  std::cout << ",\"hresult\":" << static_cast<std::uint32_t>(error)
            << "}\n";
}

void RunGraphicsDriverMeasurement(ithax::ThreadBudget &budget) {
  using Microsoft::WRL::ComPtr;
  const auto before = budget.Capture(
      "graphics_driver_before", "graphics-driver", "d3d11-dxgi", 0U,
      ithax::ReservationKind::Unknown);
  PrintSnapshot(before);

  ComPtr<IDXGIFactory1> factory;
  const HRESULT factory_result =
      CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
  if (FAILED(factory_result)) {
    PrintGraphicsUnavailable("factory", factory_result);
    return;
  }

  ComPtr<IDXGIAdapter1> adapter;
  std::uint32_t adapter_count = 0U;
  for (UINT index = 0U;; ++index) {
    ComPtr<IDXGIAdapter1> candidate;
    const HRESULT result = factory->EnumAdapters1(index, &candidate);
    if (result == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    if (FAILED(result)) {
      PrintGraphicsUnavailable("adapter-enumeration", result);
      return;
    }
    ++adapter_count;
    DXGI_ADAPTER_DESC1 description{};
    if (SUCCEEDED(candidate->GetDesc1(&description)) &&
        (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0U &&
        adapter.Get() == nullptr) {
      adapter = candidate;
    }
  }

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL feature_level{};
  const auto driver_type = adapter.Get() == nullptr ? D3D_DRIVER_TYPE_WARP
                                              : D3D_DRIVER_TYPE_UNKNOWN;
  const HRESULT device_result = D3D11CreateDevice(
      adapter.Get(), driver_type, nullptr, 0U, nullptr, 0U,
      D3D11_SDK_VERSION, &device, &feature_level, &context);
  if (FAILED(device_result)) {
    PrintGraphicsUnavailable("device-creation", device_result);
    return;
  }

  const auto active = budget.Capture(
      "graphics_driver_active", "graphics-driver", "d3d11-dxgi", 1U,
      ithax::ReservationKind::Unknown);
  PrintSnapshot(active);
  for (std::uint32_t flush = 0U; flush < GRAPHICS_FLUSH_COUNT; ++flush) {
    context->ClearState();
    context->Flush();
  }
  const auto drained = budget.Capture(
      "graphics_driver_drained", "graphics-driver", "d3d11-dxgi", 0U,
      ithax::ReservationKind::Unknown);
  PrintSnapshot(drained);
  std::cout << "{\"event\":\"graphics_driver_summary\","
            << "\"status\":\"measured\",\"gate_status\":\"open\","
            << "\"measurement_class\":\"device-create\","
            << "\"adapters\":"
            << adapter_count << ",\"software_fallback\":"
            << (adapter.Get() == nullptr ? "true" : "false")
            << ",\"feature_level\":"
            << static_cast<std::uint32_t>(feature_level) << "}\n";
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
        "owner_measurement_baseline", "main-owner", "stage3-owners", 0U,
        ithax::ReservationKind::Hard);
    PrintSnapshot(baseline);
    RunTbbMeasurement(budget);
    RunTracyMeasurement(budget);
    RunTrinityStubMeasurement(budget);
    RunGraphicsDriverMeasurement(budget);
    std::cout << "{\"event\":\"stage3_owner_measurement_summary\","
              << "\"status\":\"open\",\"functional_status\":\"pass\","
              << "\"gate_status\":\"open\","
              << "\"measured_lanes\":[\"oneTBB\",\"Tracy\"],"
              << "\"device_probe_lanes\":[\"d3d11-dxgi\"],"
              << "\"load_only_lanes\":[\"trinity-stub\"],"
              << "\"provider_bound_lanes\":[\"shader-compiler\","
              << "\"wwise\",\"vendor-sdk\"],"
              << "\"open_gates\":[\"trinity-renderer\","
              << "\"shader-compiler\",\"wwise\",\"vendor-sdk\"]}"
              << '\n';
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "Stage 3 owner measurement failed: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
