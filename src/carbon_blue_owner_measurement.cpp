#include "thread_budget.h"

#include <BlueExposure.h>
#include <Python.h>
#include <Blue.h>
#include <BluePyCpp.h>
#include <IBlueCallbackMan.h>
#include <IBlueOS.h>
#include <IBluePaths.h>
#include <IBlueResMan.h>
#include <IBlueResource.h>
#include <ResourceLoading.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

const char *g_moduleName = "ithax-carbon-blue-owner-measurement";

namespace {

constexpr std::uint32_t BLUE_WORKER_COUNT = 1U;
constexpr std::chrono::milliseconds RESOURCE_TIMEOUT{5'000};
constexpr std::chrono::milliseconds RESOURCE_POLL_INTERVAL{1};
constexpr std::size_t MAX_PATH_LENGTH = 4096U;

class BlueMeasurementError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void Require(const bool condition, const char *message) {
  if (!condition) {
    throw BlueMeasurementError(message);
  }
}

std::filesystem::path RequireEnvironmentPath(const char *name) {
  const char *value = std::getenv(name);
  Require(value != nullptr && value[0] != '\0',
          "required Carbon Blue environment path is missing");
  const std::filesystem::path path(value);
  Require(path.native().size() <= MAX_PATH_LENGTH,
          "Carbon Blue environment path exceeds its bound");
  Require(std::filesystem::exists(path),
          "required Carbon Blue environment path does not exist");
  return path;
}

void ThrowPythonStatus(const PyStatus status, const char *operation) {
  if (PyStatus_Exception(status)) {
    const char *detail = status.err_msg == nullptr ? "unknown Python error"
                                                    : status.err_msg;
    throw BlueMeasurementError(std::string(operation) + ": " + detail);
  }
}

void AppendPythonPath(PyWideStringList &paths,
                      const std::filesystem::path &path) {
  const auto wide_path = path.wstring();
  ThrowPythonStatus(PyWideStringList_Append(&paths, wide_path.c_str()),
                    "PyWideStringList_Append");
}

void InitializePython() {
  const auto prefix = RequireEnvironmentPath("CARBON_BLUE_PREFIX");
  const auto standard_library = RequireEnvironmentPath("PYTHON_STDLIB");

  PyPreConfig pre_config;
  PyPreConfig_InitIsolatedConfig(&pre_config);
  ThrowPythonStatus(Py_PreInitialize(&pre_config), "Py_PreInitialize");

  PyConfig config;
  PyConfig_InitIsolatedConfig(&config);
  AppendPythonPath(config.module_search_paths, std::filesystem::path(L"."));
  AppendPythonPath(config.module_search_paths, prefix / L"bin" / L"python");
  AppendPythonPath(config.module_search_paths, prefix / L"bin");
  AppendPythonPath(config.module_search_paths, prefix / L"lib");
  AppendPythonPath(config.module_search_paths, standard_library);
  config.module_search_paths_set = 1;
  const auto status = Py_InitializeFromConfig(&config);
  PyConfig_Clear(&config);
  ThrowPythonStatus(status, "Py_InitializeFromConfig");

  Require(InstallImportHook(), "Carbon Blue Python import hook failed");
  PyObject *scheduler = PyImport_ImportModule("scheduler");
  if (scheduler == nullptr) {
    PyErr_Print();
    throw BlueMeasurementError("Carbon Scheduler import failed");
  }
  Py_DECREF(scheduler);

  PyObject *carbon_socket = PyImport_ImportModule("_carbonsocket");
  if (carbon_socket == nullptr) {
    PyErr_Print();
    throw BlueMeasurementError("Carbon IO import failed for Blue startup");
  }
  Py_DECREF(carbon_socket);
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

void PrintSnapshot(const std::string_view phase,
                   const ithax::ThreadBudgetSnapshot &snapshot) {
  std::cout << "{\"event\":\"carbon_owner_sample\",\"phase\":";
  PrintJsonString(phase);
  std::cout << ",\"owner\":\"blue-callback-manager\","
            << "\"subsystem\":\"carbon-blue\",\"reservation\":\""
            << ithax::ReservationKindName(snapshot.reservation)
            << "\",\"configured_threads\":"
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

class ResourceNotifications final : public IBlueResManNotifications {
public:
  void OnResourceCreated(void *) override { created = true; }
  void OnResourceFromCache(void *) override { cached = true; }

  bool created = false;
  bool cached = false;
};

struct ResourceObservation {
  bool created = false;
  bool cached = false;
  std::uint32_t main_queue_pumps = 0U;
};

struct ResourceUnlock {
  void operator()(IBlueResource *resource) const noexcept {
    if (resource != nullptr) {
      resource->Unlock();
    }
  }
};

using ResourceLease = std::unique_ptr<IBlueResource, ResourceUnlock>;

struct ResourceLoadResult {
  ResourceObservation observation;
  ResourceLease resource;
};

ResourceObservation WaitForResource(IBlueResource *resource) {
  const auto deadline = std::chrono::steady_clock::now() + RESOURCE_TIMEOUT;
  std::uint32_t main_queue_pumps = 0U;
  for (;;) {
    while (BeResMan->PumpMainThreadQueue()) {
      ++main_queue_pumps;
    }
    if (!resource->IsLoading() && resource->IsPrepared()) {
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      throw BlueMeasurementError("Carbon Blue resource load timed out");
    }
    std::this_thread::sleep_for(RESOURCE_POLL_INTERVAL);
  }

  while (BeResMan->PumpMainThreadQueue()) {
    ++main_queue_pumps;
  }
  Require(resource->IsPrepared() && resource->IsGood(),
          "Carbon Blue resource did not become good");
  return {false, false, main_queue_pumps};
}

ResourceLoadResult LoadResource(const std::string &resource_path,
                                const std::string_view phase,
                                ithax::ThreadBudget &budget) {
  ResourceNotifications notifications;
  auto before = budget.Capture(
      std::string(phase) + "_before", "blue-callback-manager", "carbon-blue",
      BLUE_WORKER_COUNT, ithax::ReservationKind::Unknown);
  PrintSnapshot(std::string(phase) + "_before", before);

  IBlueResource *resource = nullptr;
  try {
    Require(BeResMan->GetResource(resource_path, "", resource,
                                  &notifications),
            "Carbon Blue resource lookup failed");
    auto queued = budget.Capture(
        std::string(phase) + "_queued", "blue-callback-manager", "carbon-blue",
        BLUE_WORKER_COUNT, ithax::ReservationKind::Unknown);
    PrintSnapshot(std::string(phase) + "_queued", queued);
    ResourceLease lease(resource);
    resource = nullptr;
    auto observation = WaitForResource(lease.get());
    observation.created = notifications.created;
    observation.cached = notifications.cached;
    auto drained = budget.Capture(
        std::string(phase) + "_drained", "blue-callback-manager", "carbon-blue",
        BLUE_WORKER_COUNT, ithax::ReservationKind::Unknown);
    PrintSnapshot(std::string(phase) + "_drained", drained);
    return {observation, std::move(lease)};
  } catch (...) {
    if (resource != nullptr) {
      resource->Unlock();
    }
    throw;
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: ithax-carbon-blue-owner-measurement "
                 "<resource-root>\n";
    return 2;
  }

  try {
    const std::filesystem::path resource_root(argv[1]);
    Require(resource_root.native().size() <= MAX_PATH_LENGTH,
            "Carbon Blue resource root exceeds its bound");
    Require(std::filesystem::is_directory(resource_root),
            "Carbon Blue resource root is not a directory");
    const auto resource_file =
        resource_root / L"TestCases" / L"stringattribute.txt";
    Require(std::filesystem::is_regular_file(resource_file),
            "Carbon Blue resource fixture is missing");

    ithax::ThreadBudgetPolicy policy;
    policy.hard_reservations = 1U;
    policy.soft_reservations = 0U;
    policy.headroom = 1U;
    ithax::ThreadBudget budget(policy);
    auto baseline = budget.Capture(
        "blue_before_start", "main-owner", "carbon-blue", 0U,
        ithax::ReservationKind::Hard);
    PrintSnapshot("blue_before_start", baseline);

    InitializePython();
    BlueModuleStartup();
    auto *blue_os = BlueGetBeOS();
    Require(blue_os != nullptr, "Carbon Blue OS interface was not created");
    BlueSetStartupArgs({L"resManThreadCount=1"});
    Require(BlueInitializePaths(L"."),
            "Carbon Blue path initialization failed");
    Require(BlueInitializeResourceLoading(),
            "Carbon Blue resource loading initialization failed");
    auto callback_started = budget.Capture(
        "blue_callback_started", "blue-callback-manager", "carbon-blue",
        BLUE_WORKER_COUNT, ithax::ReservationKind::Unknown);
    PrintSnapshot("blue_callback_started", callback_started);
    Require(blue_os->Startup(0), "Carbon Blue OS startup failed");

    const auto absolute_root = std::filesystem::absolute(resource_root);
    Require(BlueSetSearchPaths({L"res=" + absolute_root.wstring()}),
            "Carbon Blue resource search path setup failed");
    const std::string resource_path = "res:/TestCases/stringattribute.txt";
    auto cold = LoadResource(resource_path, "blue_cold", budget);
    Require(cold.observation.created && !cold.observation.cached,
            "Carbon Blue cold load did not create a resource");
    auto warm = LoadResource(resource_path, "blue_warm", budget);
    Require(warm.observation.cached && !warm.observation.created,
            "Carbon Blue warm load did not use the resource cache");
    Require(BeResMan->GetPendingLoads() == 0U &&
                BeResMan->GetPendingPrepares() == 0U,
            "Carbon Blue retained pending resource work");

    Require(BeCallbackMan != nullptr,
            "Carbon Blue callback manager was unexpectedly cleared");
    BeCallbackMan->Stop();
    auto callback_stopped = budget.Capture(
        "blue_callback_stopped", "blue-callback-manager", "carbon-blue", 0U,
        ithax::ReservationKind::Unknown);
    PrintSnapshot("blue_callback_stopped", callback_stopped);
    std::cout << "{\"event\":\"carbon_blue_owner_summary\","
              << "\"status\":\"pass\",\"cold_queue_pumps\":"
              << cold.observation.main_queue_pumps
              << ",\"warm_queue_pumps\":"
              << warm.observation.main_queue_pumps
              << ",\"shutdown_mode\":\"process-exit\"}\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "carbon Blue owner measurement failed: " << error.what()
              << '\n';
    return 1;
  }
}
