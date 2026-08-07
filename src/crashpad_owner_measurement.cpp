#include "thread_budget.h"

#include <crashpad/client/crash_report_database.h>
#include <crashpad/client/crashpad_client.h>

#include <tlhelp32.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace {

constexpr std::chrono::milliseconds REPORT_TIMEOUT{5'000};
constexpr std::chrono::milliseconds REPORT_POLL_INTERVAL{25};
constexpr std::uint32_t MAX_PATH_LENGTH = 4096U;
constexpr std::uint32_t MAX_HANDLER_PROCESSES = 64U;
constexpr std::uint32_t MAX_HANDLER_THREADS = 4096U;
constexpr wchar_t HANDLER_NAME[] = L"crashpad_handler.exe";

class CrashpadMeasurementError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void Require(const bool condition, const char *message) {
  if (!condition) {
    throw CrashpadMeasurementError(message);
  }
}

struct HandlerObservation {
  std::uint32_t process_count = 0U;
  std::uint32_t thread_count = 0U;
};

class ScopedHandle {
public:
  explicit ScopedHandle(const HANDLE handle) noexcept : m_handle(handle) {}

  ~ScopedHandle() noexcept {
    if (m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr) {
      CloseHandle(m_handle);
    }
  }

  ScopedHandle(const ScopedHandle &) = delete;
  ScopedHandle &operator=(const ScopedHandle &) = delete;

  HANDLE Get() const noexcept { return m_handle; }

private:
  HANDLE m_handle;
};

std::vector<DWORD> CollectHandlerProcessIds() {
  ScopedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0U));
  if (snapshot.Get() == INVALID_HANDLE_VALUE) {
    throw std::system_error(static_cast<int>(GetLastError()),
                            std::system_category(),
                            "CreateToolhelp32Snapshot failed");
  }

  std::vector<DWORD> process_ids;
  process_ids.reserve(MAX_HANDLER_PROCESSES);
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  const bool first_succeeded = Process32FirstW(snapshot.Get(), &entry) != FALSE;
  if (first_succeeded) {
    do {
      if (_wcsicmp(entry.szExeFile, HANDLER_NAME) == 0) {
        if (process_ids.size() >= MAX_HANDLER_PROCESSES) {
          throw CrashpadMeasurementError(
              "Crashpad handler process bound was exceeded");
        }
        process_ids.push_back(entry.th32ProcessID);
      }
    } while (Process32NextW(snapshot.Get(), &entry) != FALSE);
  }
  const auto error = GetLastError();
  if (!first_succeeded && error != ERROR_NO_MORE_FILES) {
    throw std::system_error(static_cast<int>(error), std::system_category(),
                            "Process32FirstW failed");
  }
  if (first_succeeded && error != ERROR_NO_MORE_FILES) {
    throw std::system_error(static_cast<int>(error), std::system_category(),
                            "Process32NextW failed");
  }
  return process_ids;
}

std::uint32_t CountHandlerThreads(const std::vector<DWORD> &process_ids) {
  ScopedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0U));
  if (snapshot.Get() == INVALID_HANDLE_VALUE) {
    throw std::system_error(static_cast<int>(GetLastError()),
                            std::system_category(),
                            "CreateToolhelp32Snapshot for threads failed");
  }
  THREADENTRY32 thread_entry{};
  thread_entry.dwSize = sizeof(thread_entry);
  std::uint32_t thread_count = 0U;
  const bool thread_first_succeeded =
      Thread32First(snapshot.Get(), &thread_entry) != FALSE;
  if (thread_first_succeeded) {
    do {
      const auto process_id = thread_entry.th32OwnerProcessID;
      if (std::find(process_ids.begin(), process_ids.end(), process_id) !=
          process_ids.end()) {
        if (thread_count >= MAX_HANDLER_THREADS) {
          throw CrashpadMeasurementError(
              "Crashpad handler thread bound was exceeded");
        }
        ++thread_count;
      }
    } while (Thread32Next(snapshot.Get(), &thread_entry) != FALSE);
  }
  const auto thread_error = GetLastError();
  if (!thread_first_succeeded && thread_error != ERROR_NO_MORE_FILES) {
    throw std::system_error(static_cast<int>(thread_error),
                            std::system_category(), "Thread32First failed");
  }
  if (thread_first_succeeded && thread_error != ERROR_NO_MORE_FILES) {
    throw std::system_error(static_cast<int>(thread_error),
                            std::system_category(), "Thread32Next failed");
  }
  return thread_count;
}

HandlerObservation ObserveHandlerProcesses() {
  const auto process_ids = CollectHandlerProcessIds();
  return {static_cast<std::uint32_t>(process_ids.size()),
          CountHandlerThreads(process_ids)};
}

void PrintSnapshot(const std::string_view phase,
                   const ithax::ThreadBudgetSnapshot &snapshot,
                   const HandlerObservation observation,
                   const std::uint32_t peak_handler_threads) {
  std::cout << "{\"event\":\"carbon_owner_sample\",\"phase\":\"" << phase
            << "\",\"owner\":\"crashpad-handler-process\","
            << "\"subsystem\":\"crashpad\",\"reservation\":\"unknown\","
            << "\"configured_threads\":null,\"observed_threads\":"
            << observation.thread_count
            << ",\"peak_observed_threads\":" << peak_handler_threads
            << ",\"handler_processes\":" << observation.process_count
            << ",\"budget_process_threads\":" << snapshot.observed_threads
            << "}\n";
}

void WaitForPendingReport(crashpad::CrashReportDatabase &database) {
  const auto deadline = std::chrono::steady_clock::now() + REPORT_TIMEOUT;
  for (;;) {
    std::vector<crashpad::CrashReportDatabase::Report> reports;
    const auto status = database.GetPendingReports(&reports);
    if (status == crashpad::CrashReportDatabase::kNoError && !reports.empty()) {
      return;
    }
    Require(status == crashpad::CrashReportDatabase::kNoError ||
                status == crashpad::CrashReportDatabase::kBusyError,
            "Crashpad pending-report query failed");
    if (std::chrono::steady_clock::now() >= deadline) {
      throw CrashpadMeasurementError(
          "Crashpad did not create a pending non-crash report");
    }
    std::this_thread::sleep_for(REPORT_POLL_INTERVAL);
  }
}

void RemoveTemporaryRoot(const std::filesystem::path &root) {
  std::error_code error;
  std::filesystem::remove_all(root, error);
  if (error) {
    throw CrashpadMeasurementError(
        "Crashpad temporary-directory cleanup failed: " + error.message());
  }
}

HandlerObservation CaptureOwnerSample(const std::string_view phase,
                                      ithax::ThreadBudget &budget,
                                      std::uint32_t &peak_handler_threads) {
  const auto snapshot =
      budget.Capture(std::string(phase), "crashpad-handler-process", "crashpad",
                     0U, ithax::ReservationKind::Unknown);
  const auto observation = ObserveHandlerProcesses();
  peak_handler_threads =
      std::max(peak_handler_threads, observation.thread_count);
  PrintSnapshot(phase, snapshot, observation, peak_handler_threads);
  return observation;
}

void RunWorkload(const std::filesystem::path &handler_path,
                 const std::filesystem::path &database_path,
                 const std::filesystem::path &metrics_path,
                 ithax::ThreadBudget &budget) {
  std::uint32_t peak_handler_threads = 0U;
  const auto baseline_handlers =
      CaptureOwnerSample("crashpad_before_start", budget, peak_handler_threads);

  const auto database_file_path = base::FilePath(database_path.wstring());
  auto database = crashpad::CrashReportDatabase::Initialize(database_file_path);
  Require(database != nullptr, "Crashpad database initialization failed");
  crashpad::CrashpadClient client;
  const std::map<std::string, std::string> annotations = {
      {"workload", "ithax-crashpad-owner-measurement"},
  };
  const auto started = client.StartHandler(
      base::FilePath(handler_path.wstring()), database_file_path,
      base::FilePath(metrics_path.wstring()), "", annotations, {}, false,
      false);
  Require(started, "Crashpad handler startup failed");
  const auto active_handlers = CaptureOwnerSample("crashpad_handler_started",
                                                  budget, peak_handler_threads);
  Require(active_handlers.process_count > baseline_handlers.process_count,
          "Crashpad handler process was not observed");

  CONTEXT context{};
  RtlCaptureContext(&context);
  crashpad::CrashpadClient::DumpWithoutCrash(context);
  static_cast<void>(CaptureOwnerSample("crashpad_dump_requested", budget,
                                       peak_handler_threads));
  WaitForPendingReport(*database);
  static_cast<void>(CaptureOwnerSample("crashpad_report_pending", budget,
                                       peak_handler_threads));
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: ithax-crashpad-owner-measurement "
                 "<handler-path>\n";
    return 2;
  }

  const std::filesystem::path handler_path(argv[1]);
  std::error_code filesystem_error;
  const auto absolute_handler =
      std::filesystem::absolute(handler_path, filesystem_error);
  if (filesystem_error || absolute_handler.native().size() > MAX_PATH_LENGTH ||
      !std::filesystem::is_regular_file(absolute_handler, filesystem_error)) {
    std::cerr << "Crashpad handler path is invalid\n";
    return 1;
  }

  std::filesystem::path root;
  try {
    const auto process_id = GetCurrentProcessId();
    root = std::filesystem::temp_directory_path() /
           ("ithax-crashpad-" + std::to_string(process_id));
    const auto database_path = root / "database";
    const auto metrics_path = root / "metrics";
    std::filesystem::create_directories(database_path);
    std::filesystem::create_directories(metrics_path);

    ithax::ThreadBudgetPolicy policy;
    policy.hard_reservations = 1U;
    policy.soft_reservations = 0U;
    policy.headroom = 1U;
    ithax::ThreadBudget budget(policy);
    RunWorkload(absolute_handler, database_path, metrics_path, budget);
    RemoveTemporaryRoot(root);
    std::cout << "{\"event\":\"crashpad_owner_summary\","
              << "\"status\":\"pass\",\"handler_process\":true,"
              << "\"workload\":\"dump-without-crash\"}\n";
    return 0;
  } catch (const std::exception &error) {
    std::error_code cleanup_error;
    if (!root.empty()) {
      std::filesystem::remove_all(root, cleanup_error);
    }
    std::cerr << "Crashpad owner measurement failed: " << error.what() << '\n';
    if (cleanup_error) {
      std::cerr << "Crashpad temporary-directory cleanup failed: "
                << cleanup_error.message() << '\n';
    }
    return 1;
  }
}
