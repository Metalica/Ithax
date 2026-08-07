#include "ecs/ecs_world.h"
#include "runtime/runtime_supervisor.h"
#include "thread_budget.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <BlueExposure.h>
#include <Python.h>
#include <Blue.h>
#include <BluePyCpp.h>
#include <IBlueCallbackMan.h>
#include <IBlueOS.h>
#include <IBluePaths.h>
#include <IBluePython.h>
#include <IBlueResMan.h>
#include <ResourceLoading.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <tracy/Tracy.hpp>

const char *g_moduleName = "ithax-stage3-carbon-host-integration";

namespace {

constexpr std::size_t HOST_ENTITY_COUNT = 10'000U;
constexpr std::uint32_t HOST_TICK_COUNT = 1'000U;
constexpr std::uint32_t HOST_MAX_WORKERS = 4U;
constexpr std::uint32_t HOST_DEFAULT_WORKERS = 1U;
constexpr std::uint32_t HOST_BLUE_WORKERS = 1U;
constexpr std::uint32_t HOST_TRACY_WORKERS = 1U;
constexpr std::uint32_t HOST_P50_PERCENTILE = 50U;
constexpr std::uint32_t HOST_P95_PERCENTILE = 95U;
constexpr std::uint32_t HOST_DEADLINE_PERCENTILE = 99U;
constexpr double HOST_TICK_DEADLINE_MILLISECONDS = 16.667;
constexpr std::uint32_t MAX_DRAIN_PUMPS = 10'000U;
constexpr std::chrono::milliseconds DRAIN_WAIT{1};

class HostIntegrationError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

void Require(const bool condition, const char *message) {
  if (!condition) {
    throw HostIntegrationError(message);
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

void PrintBudgetSample(const ithax::ThreadBudgetSnapshot &snapshot) {
  std::cout << "{\"event\":\"carbon_host_budget_sample\",\"phase\":";
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
  std::cout << "}\n" << std::flush;
}

void PrintPhase(const std::string_view phase) {
  std::cout << "{\"event\":\"carbon_host_phase\",\"phase\":";
  PrintJsonString(phase);
  std::cout << "}\n" << std::flush;
}

std::filesystem::path RequireEnvironmentPath(const char *name) {
  const char *value = std::getenv(name);
  Require(value != nullptr && value[0] != '\0',
          "required Carbon host environment path is missing");
  const std::filesystem::path path(value);
  Require(path.native().size() <= 4096U,
          "Carbon host environment path exceeds its bound");
  Require(std::filesystem::exists(path),
          "required Carbon host environment path does not exist");
  return path;
}

void ThrowPythonStatus(const PyStatus status, const char *operation) {
  if (PyStatus_Exception(status)) {
    const char *detail = status.err_msg == nullptr ? "unknown Python error"
                                                    : status.err_msg;
    throw HostIntegrationError(std::string(operation) + ": " + detail);
  }
}

void AppendPythonPath(PyWideStringList &paths,
                      const std::filesystem::path &path) {
  const auto wide_path = path.wstring();
  ThrowPythonStatus(PyWideStringList_Append(&paths, wide_path.c_str()),
                    "PyWideStringList_Append");
}

void ImportRequiredPythonModule(const char *name) {
  PyObject *module = PyImport_ImportModule(name);
  if (module == nullptr) {
    PyErr_Clear();
    throw HostIntegrationError(std::string("Python import failed: ") + name);
  }
  Py_DECREF(module);
}

void ImportBluePythonExtension() {
  PyObject *blue_module = PyImport_ImportModule("blue_debug");
  if (blue_module == nullptr) {
    PyErr_Clear();
    throw HostIntegrationError("Python import failed: blue_debug");
  }
  PyObject *modules = PyImport_GetModuleDict();
  if (PyDict_SetItemString(modules, "blue", blue_module) != 0) {
    Py_DECREF(blue_module);
    PyErr_Clear();
    throw HostIntegrationError("Python module alias failed: blue");
  }
  Py_DECREF(blue_module);
}

void InitializePython() {
  const auto prefix = RequireEnvironmentPath("CARBON_HOST_PREFIX");
  const auto standard_library = RequireEnvironmentPath("PYTHON_STDLIB");

  PyPreConfig pre_config;
  PyPreConfig_InitIsolatedConfig(&pre_config);
  ThrowPythonStatus(Py_PreInitialize(&pre_config), "Py_PreInitialize");

  PyConfig config;
  PyConfig_InitIsolatedConfig(&config);
  AppendPythonPath(config.module_search_paths, std::filesystem::path(L"."));
  AppendPythonPath(config.module_search_paths, prefix / L"bin" / L"python");
  AppendPythonPath(config.module_search_paths, prefix / L"bin");
  AppendPythonPath(config.module_search_paths, prefix / L"bin" / L"python");
  AppendPythonPath(config.module_search_paths, prefix / L"lib");
  AppendPythonPath(config.module_search_paths, standard_library);
  config.module_search_paths_set = 1;
  const auto status = Py_InitializeFromConfig(&config);
  PyConfig_Clear(&config);
  ThrowPythonStatus(status, "Py_InitializeFromConfig");

  Require(InstallImportHook(), "Carbon Blue Python import hook failed");
  ImportRequiredPythonModule("scheduler");
  ImportRequiredPythonModule("_carbonsocket");
}

void ExercisePythonBridge() {
  Require(Py_IsInitialized() != 0, "Python interpreter is not initialized");
  PrintPhase("blue_debug_import_start");
  ImportBluePythonExtension();
  PrintPhase("blue_debug_import_done");
  PrintPhase("bluepycore_import_start");
  ImportRequiredPythonModule("bluepycore");
  PrintPhase("bluepycore_import_done");
}

std::size_t PercentileIndex(const std::size_t count,
                            const std::size_t percentile) {
  return ((count - 1U) * percentile) / 100U;
}

struct HostTimingSummary {
  double p50_milliseconds = 0.0;
  double p95_milliseconds = 0.0;
  double p99_milliseconds = 0.0;
  double headroom_milliseconds = 0.0;
  std::uint32_t missed_deadlines = 0U;
};

HostTimingSummary SummarizeTimings(std::vector<double> timings) {
  Require(!timings.empty(), "Carbon host produced no tick timings");
  std::sort(timings.begin(), timings.end());
  HostTimingSummary summary;
  summary.p50_milliseconds = timings[PercentileIndex(
      timings.size(), HOST_P50_PERCENTILE)];
  summary.p95_milliseconds = timings[PercentileIndex(
      timings.size(), HOST_P95_PERCENTILE)];
  summary.p99_milliseconds = timings[PercentileIndex(
      timings.size(), HOST_DEADLINE_PERCENTILE)];
  summary.headroom_milliseconds =
      HOST_TICK_DEADLINE_MILLISECONDS - summary.p99_milliseconds;
  for (const double timing : timings) {
    if (timing > HOST_TICK_DEADLINE_MILLISECONDS) {
      ++summary.missed_deadlines;
    }
  }
  return summary;
}

tf::Taskflow BuildTickTasks(const ithax::ecs::WorldSnapshot &snapshot,
                            const std::uint32_t workers,
                            std::vector<ithax::ecs::EcsJournal> &journals) {
  tf::Taskflow taskflow;
  for (std::uint32_t worker = 0U; worker < workers; ++worker) {
    taskflow.emplace([&snapshot, workers, worker, &journals]() {
      ithax::ecs::BuildTransformJournalInto(
          snapshot, worker, workers, journals[worker]);
    });
  }
  return taskflow;
}

void ApplyJournalsToSnapshot(
    ithax::ecs::WorldSnapshot &snapshot,
    const std::vector<ithax::ecs::EcsJournal> &journals,
    const std::uint64_t tick) {
  std::size_t offset = 0U;
  for (const auto &journal : journals) {
    for (std::size_t index = 0U;
         index < journal.transforms.size(); ++index) {
      Require(offset < snapshot.states.size(),
              "Carbon host journal exceeded the snapshot");
      const auto &update = journal.transforms[index];
      auto &state = snapshot.states[offset];
      const auto expected_previous =
          static_cast<std::int64_t>(offset) +
          static_cast<std::int64_t>(tick - 1U);
      const auto expected_current = static_cast<std::int64_t>(offset) +
                                    static_cast<std::int64_t>(tick);
      Require(update.sequence == index && update.entity == state.entity,
              "Carbon host journal order changed");
      Require(state.transform.value == expected_previous &&
                  state.velocity.delta == 1,
              "Carbon host snapshot state diverged");
      Require(update.value == expected_current,
              "Carbon host journal value diverged");
      state.transform.value = update.value;
      ++offset;
    }
  }
  Require(offset == snapshot.states.size(),
          "Carbon host journals did not cover the snapshot");
}

void VerifyWorld(const ithax::ecs::WorldSnapshot &snapshot,
                 const std::uint64_t tick) {
  Require(snapshot.states.size() == HOST_ENTITY_COUNT,
          "Carbon host entity count changed");
  for (std::size_t index = 0U; index < snapshot.states.size(); ++index) {
    const auto expected = static_cast<std::int64_t>(index) +
                          static_cast<std::int64_t>(tick);
    Require(snapshot.states[index].transform.value == expected,
            "Carbon host world state diverged");
    Require(snapshot.states[index].velocity.delta == 1,
            "Carbon host velocity state changed");
  }
}

class CarbonHost;

HostTimingSummary RunTicks(
    CarbonHost &host, ithax::ecs::EcsWorld &world,
    ithax::runtime::RuntimeSupervisor &supervisor,
    std::uint32_t workers);

class CarbonHost {
 public:
  CarbonHost() = default;
  CarbonHost(const CarbonHost &) = delete;
  CarbonHost &operator=(const CarbonHost &) = delete;

  bool IsStarted() const noexcept { return m_started; }

  void Start() {
    PrintPhase("python_initialize_start");
    InitializePython();
    PrintPhase("python_initialize_done");
    BlueModuleStartup();
    PrintPhase("blue_module_startup_done");
    BlueSetStartupArgs({L"resManThreadCount=1"});
    Require(BlueInitializePaths(L"."),
            "Carbon Blue path initialization failed");
    ExercisePythonBridge();
    Require(BlueGetBeOS() != nullptr, "Carbon Blue OS interface is missing");
    m_started = true;
    PrintPhase("blue_startup_done");
    PrintPhase("python_bridge_done");
  }

  void MeasureCarbonDb(ithax::ThreadBudget &budget) {
    const auto before = budget.Capture(
        "carbon_db_before_import", "carbon-db", "carbon-db", 0U,
        ithax::ReservationKind::Unknown);
    PrintBudgetSample(before);
    m_db_module = PyImport_ImportModule("_db_debug");
    if (m_db_module == nullptr) {
      PyErr_Clear();
      throw HostIntegrationError("Carbon DB Python extension import failed");
    }
    const auto ready = budget.Capture(
        "carbon_db_extension_ready", "carbon-db", "carbon-db", 0U,
        ithax::ReservationKind::Unknown);
    PrintBudgetSample(ready);
    std::cout << "{\"event\":\"carbon_db_owner_summary\","
              << "\"status\":\"not_configured\","
              << "\"gate_status\":\"open\","
              << "\"measurement_class\":\"extension-import\","
              << "\"workload\":\"extension-import-only\","
              << "\"database_provider\":\"not-configured\","
              << "\"reservation\":\"unknown\"}\n";
  }

  void Pump() {
    Require(m_started && BeOS != nullptr, "Carbon host is not running");
    BeOS->PumpOS();
  }

  void PrepareShutdown() {
    Require(m_started, "Carbon host shutdown was requested before startup");
    PrintPhase("prepare_shutdown_start");
    ReleasePythonReferences();
    DrainResourceQueue();
    if (BeCallbackMan != nullptr) {
      BeCallbackMan->Stop();
    }
    Require(BeResMan == nullptr ||
                (BeResMan->GetPendingLoads() == 0U &&
                 BeResMan->GetPendingPrepares() == 0U),
            "Carbon host retained pending resource work");
    if (PyOS != nullptr) {
      PyOS->Shutdown(1);
    }
    m_prepared = true;
    PrintPhase("prepare_shutdown_done");
  }

  [[noreturn]] void FinalizeShutdown(const int exit_code) {
    Require(m_prepared, "Carbon host finalization skipped preparation");
    PrintPhase("carbon_terminate_start");
    if (BeOS != nullptr) {
      BlueTerminate(exit_code);
    }
    std::quick_exit(exit_code);
  }

  [[noreturn]] void Abort(const int exit_code) noexcept {
    if (BeOS != nullptr) {
      BlueTerminate(exit_code);
    }
    std::quick_exit(exit_code);
  }

 private:
  void ReleasePythonReferences() noexcept {
    if (m_db_module != nullptr && Py_IsInitialized() != 0) {
      Py_DECREF(m_db_module);
      m_db_module = nullptr;
    }
  }

  void DrainResourceQueue() {
    if (BeResMan == nullptr) {
      return;
    }
    for (std::uint32_t pump = 0U; pump < MAX_DRAIN_PUMPS; ++pump) {
      while (BeResMan->PumpMainThreadQueue()) {
      }
      if (BeResMan->GetPendingLoads() == 0U &&
          BeResMan->GetPendingPrepares() == 0U) {
        return;
      }
      std::this_thread::sleep_for(DRAIN_WAIT);
    }
    throw HostIntegrationError("Carbon host resource drain timed out");
  }

  bool m_started = false;
  bool m_prepared = false;
  PyObject *m_db_module = nullptr;
};

HostTimingSummary RunTicks(
    CarbonHost &host, ithax::ecs::EcsWorld &world,
    ithax::runtime::RuntimeSupervisor &supervisor,
    const std::uint32_t workers) {
  std::vector<double> timings;
  timings.reserve(HOST_TICK_COUNT);
  ithax::ecs::WorldSnapshot snapshot;
  std::vector<ithax::ecs::EcsJournal> journals(workers);
  world.CopySnapshot(snapshot);
  auto taskflow = BuildTickTasks(snapshot, workers, journals);
  for (std::uint64_t tick = 1U; tick <= HOST_TICK_COUNT; ++tick) {
    ZoneScopedN("carbon_host_tick");
    const auto start = std::chrono::steady_clock::now();
    const auto task_id = supervisor.SubmitReusable(taskflow);
    supervisor.Wait(task_id);
    world.MergeJournals(journals);
    ApplyJournalsToSnapshot(snapshot, journals, tick);
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start);
    timings.push_back(elapsed.count());
    host.Pump();
    FrameMark;
  }
  return SummarizeTimings(std::move(timings));
}

void PrintHostSummary(const ithax::ThreadBudget &budget,
                      const std::uint32_t workers,
                      const HostTimingSummary &timing,
                      const std::uint64_t final_hash) {
  const bool deadline_passed =
      timing.p99_milliseconds <= HOST_TICK_DEADLINE_MILLISECONDS;
  const char *const gate_status = deadline_passed ? "pass" : "open";
  std::cout << std::fixed << std::setprecision(3)
            << "{\"event\":\"stage3_carbon_host_summary\","
            << "\"status\":\"" << gate_status << '\"'
            << ",\"gate_status\":\"" << gate_status << '\"'
            << ",\"measurement_class\":\"functional-host\","
            << "\"ticks\":" << HOST_TICK_COUNT
            << ",\"entities\":" << HOST_ENTITY_COUNT
            << ",\"workers\":" << workers
            << ",\"final_hash\":" << final_hash
             << ",\"hard_reservations\":"
             << budget.Policy().hard_reservations
             << ",\"soft_reservations\":"
             << budget.Policy().soft_reservations
             << ",\"headroom\":" << budget.Policy().headroom
             << ",\"available_workers\":" << budget.AvailableWorkerCount()
            << ",\"profiler\":\"Tracy\""
            << ",\"deadline_ms\":" << HOST_TICK_DEADLINE_MILLISECONDS
            << ",\"deadline_basis\":\"p99\""
             << ",\"timing_scope\":\"owned-simulation-before-blue-pump\""
             << ",\"deadline_status\":\""
             << (deadline_passed ? "pass" : "missed") << '"'
             << ",\"p50_ms\":" << timing.p50_milliseconds
            << ",\"p95_ms\":" << timing.p95_milliseconds
             << ",\"p99_ms\":" << timing.p99_milliseconds
             << ",\"headroom_ms\":" << timing.headroom_milliseconds
             << ",\"missed_deadlines\":" << timing.missed_deadlines
             << ",\"shutdown_mode\":\"carbon-terminate\"}\n";
}

}  // namespace

int main(const int argc, char **argv) {
  bool require_deadline = false;
  std::uint32_t requested_workers = 0U;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--fail-on-deadline" && !require_deadline) {
      require_deadline = true;
      continue;
    }
    if (argument == "--workers" && index + 1 < argc &&
        requested_workers == 0U) {
      const std::string_view value(argv[++index]);
      const auto parsed = std::from_chars(
          value.data(), value.data() + value.size(), requested_workers);
      if (parsed.ec == std::errc{} &&
          parsed.ptr == value.data() + value.size() &&
          requested_workers > 0U && requested_workers <= HOST_MAX_WORKERS) {
        continue;
      }
    }
    std::cerr << "usage: ithax-stage3-carbon-host-integration "
                 "[--fail-on-deadline] [--workers 1-4]\n";
    return EXIT_FAILURE;
  }

  std::unique_ptr<CarbonHost> host;
  try {
    ithax::ThreadBudgetPolicy policy;
    policy.hard_reservations = 1U;
    policy.soft_reservations = HOST_BLUE_WORKERS + HOST_TRACY_WORKERS;
    policy.headroom = 1U;
    ithax::ThreadBudget budget(policy);
    const auto baseline = budget.Capture(
        "carbon_host_baseline", "main-owner", "carbon-host", 1U,
        ithax::ReservationKind::Hard);
    PrintBudgetSample(baseline);

    tracy::StartupProfiler();
    const auto profiler = budget.Capture(
        "carbon_host_profiler_started", "tracy", "profiler",
        HOST_TRACY_WORKERS, ithax::ReservationKind::Soft);
    PrintBudgetSample(profiler);

    host = std::make_unique<CarbonHost>();
    host->Start();
    const auto blue = budget.Capture(
        "carbon_host_blue_started", "blue-python", "carbon-blue",
        HOST_BLUE_WORKERS, ithax::ReservationKind::Soft);
    PrintBudgetSample(blue);
    PrintPhase("carbon_db_import_start");
    host->MeasureCarbonDb(budget);
    PrintPhase("carbon_db_import_done");

    const auto available_workers = budget.AvailableWorkerCount();
    const auto workers = requested_workers == 0U
                              ? std::min(
                                    HOST_DEFAULT_WORKERS, available_workers)
                              : requested_workers;
    Require(workers > 0U, "Carbon host has no measured Taskflow headroom");
    Require(workers <= available_workers,
            "requested Carbon host workers exceed measured headroom");
    ithax::ecs::EcsWorld world;
    world.CreateEntities(HOST_ENTITY_COUNT);
    ithax::runtime::RuntimeSupervisor supervisor(budget, workers);
    supervisor.Start();
    const auto timing = RunTicks(*host, world, supervisor, workers);
    supervisor.RequestStop();
    supervisor.Drain();
    Require(supervisor.State() == ithax::runtime::LifecycleState::Stopped,
            "Carbon host supervisor did not stop cleanly");

    const auto final_snapshot = world.Snapshot();
    VerifyWorld(final_snapshot, HOST_TICK_COUNT);
    const auto final_hash = world.StateHash();
    host->PrepareShutdown();
    if (tracy::IsProfilerStarted()) {
      tracy::ShutdownProfiler();
    }
    PrintHostSummary(budget, workers, timing, final_hash);
    std::cout.flush();
    const bool deadline_passed =
        timing.p99_milliseconds <= HOST_TICK_DEADLINE_MILLISECONDS;
    const int exit_code = require_deadline && !deadline_passed
                              ? EXIT_FAILURE
                              : EXIT_SUCCESS;
    host->FinalizeShutdown(exit_code);
  } catch (const std::exception &error) {
    std::cerr << "Carbon host integration failed: " << error.what() << '\n';
    if (host != nullptr && host->IsStarted()) {
      host->Abort(EXIT_FAILURE);
    }
    return EXIT_FAILURE;
  }
}
