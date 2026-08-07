#include "thread_budget.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <taskflow/taskflow.hpp>

namespace {

constexpr std::uint32_t DEFAULT_WORKER_COUNT = 4U;
constexpr std::uint32_t MAX_WORKER_COUNT = 64U;
constexpr std::uint32_t MAX_REPETITIONS = 10U;
constexpr std::uint32_t MIN_OPTION_VALUE = 1U;
constexpr std::uint32_t MAX_RESERVATION = 64U;
constexpr std::uint32_t PHASE_TIMEOUT_MILLISECONDS = 5000U;

struct MeasurementOptions {
  std::optional<std::uint32_t> requested_workers;
  std::uint32_t repetitions = 1U;
  ithax::ThreadBudgetPolicy policy;
};

enum class ParseResult {
  Success,
  Help,
  Error,
};

struct WorkerGate {
  std::condition_variable condition;
  std::mutex mutex;
  std::uint32_t active = 0U;
  bool release = false;
};

bool ParseBoundedValue(const std::string_view text, const std::uint32_t minimum,
                       const std::uint32_t maximum, std::uint32_t &value) {
  if (text.empty()) {
    return false;
  }
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
         value >= minimum && value <= maximum;
}

bool SetOption(const std::string_view name, const std::string_view value,
               MeasurementOptions &options) {
  std::uint32_t parsed = 0U;
  if (name == "--workers") {
    if (!ParseBoundedValue(value, MIN_OPTION_VALUE, MAX_WORKER_COUNT, parsed)) {
      return false;
    }
    options.requested_workers = parsed;
    return true;
  }
  if (name == "--repetitions") {
    if (!ParseBoundedValue(value, MIN_OPTION_VALUE, MAX_REPETITIONS, parsed)) {
      return false;
    }
    options.repetitions = parsed;
    return true;
  }
  if (name == "--hard-reserved") {
    if (!ParseBoundedValue(value, 0U, MAX_RESERVATION, parsed)) {
      return false;
    }
    options.policy.hard_reservations = parsed;
    return true;
  }
  if (name == "--soft-reserved") {
    if (!ParseBoundedValue(value, 0U, MAX_RESERVATION, parsed)) {
      return false;
    }
    options.policy.soft_reservations = parsed;
    return true;
  }
  if (name == "--headroom") {
    if (!ParseBoundedValue(value, 0U, MAX_RESERVATION, parsed)) {
      return false;
    }
    options.policy.headroom = parsed;
    return true;
  }
  return false;
}

ParseResult ParseArguments(const int argc, char **argv,
                           MeasurementOptions &options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view name(argv[index]);
    if (name == "--help") {
      return ParseResult::Help;
    }
    if (index + 1 >= argc || !SetOption(name, argv[++index], options)) {
      std::cerr << "invalid thread budget option: " << name << '\n';
      return ParseResult::Error;
    }
  }
  return ParseResult::Success;
}

void PrintUsage() {
  std::cout << "Usage: ithax-thread-budget-measurement [options]\n"
            << "  --workers N       Taskflow workers (1-64)\n"
            << "  --repetitions N   active phases (1-10)\n"
            << "  --hard-reserved N hard CPU reservations (0-64)\n"
            << "  --soft-reserved N soft CPU reservations (0-64)\n"
            << "  --headroom N      required CPU headroom (0-64)\n";
}

void ReleaseWorkers(WorkerGate &gate) {
  {
    std::lock_guard lock(gate.mutex);
    gate.release = true;
  }
  gate.condition.notify_all();
}

ithax::ThreadBudgetSnapshot RunTaskflowPhase(ithax::ThreadBudget &budget,
                                             const std::uint32_t workers,
                                             const std::uint32_t repetition) {
  ithax::ThreadBudgetSnapshot active_snapshot;
  {
    tf::Executor executor(workers);
    tf::Taskflow taskflow;
    WorkerGate gate;
    for (std::uint32_t index = 0U; index < workers; ++index) {
      taskflow.emplace([&gate]() {
        std::unique_lock lock(gate.mutex);
        ++gate.active;
        gate.condition.notify_all();
        gate.condition.wait(lock, [&gate]() { return gate.release; });
      });
    }

    auto completion = executor.run(taskflow);
    {
      std::unique_lock lock(gate.mutex);
      const bool all_workers_active = gate.condition.wait_for(
          lock, std::chrono::milliseconds(PHASE_TIMEOUT_MILLISECONDS),
          [&gate, workers]() { return gate.active == workers; });
      if (!all_workers_active) {
        lock.unlock();
        ReleaseWorkers(gate);
        completion.wait();
        throw ithax::ThreadBudgetError(
            "Taskflow workers did not reach the active phase");
      }
    }

    active_snapshot = budget.Capture(
        "taskflow_active_" + std::to_string(repetition), "taskflow", "executor",
        workers, ithax::ReservationKind::Soft);
    ReleaseWorkers(gate);
    completion.wait();
  }

  return active_snapshot;
}

void PrintJsonString(const std::string &value) {
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
  std::cout << "{\"event\":\"thread_budget_sample\",\"phase\":";
  PrintJsonString(snapshot.phase);
  std::cout << ",\"owner\":";
  PrintJsonString(snapshot.owner);
  std::cout << ",\"subsystem\":";
  PrintJsonString(snapshot.subsystem);
  std::cout << ",\"reservation\":";
  PrintJsonString(ithax::ReservationKindName(snapshot.reservation));
  std::cout << ",\"configured_threads\":" << snapshot.configured_threads
            << ",\"observed_threads\":" << snapshot.observed_threads
            << ",\"peak_observed_threads\":" << snapshot.peak_observed_threads
            << ",\"process_cpu_set_count\":" << snapshot.process_cpu_set_count
            << ",\"cpu_set_source\":";
  PrintJsonString(snapshot.cpu_set_source);
  std::cout << ",\"cpu_set_ids\":[";
  for (std::size_t index = 0U; index < snapshot.cpu_set_ids.size(); ++index) {
    if (index > 0U) {
      std::cout << ',';
    }
    std::cout << snapshot.cpu_set_ids[index];
  }
  std::cout << "],\"process_affinity_mask\":" << snapshot.process_affinity_mask
            << ",\"timestamp_utc\":";
  PrintJsonString(snapshot.timestamp_utc);
  std::cout << "}\n";
}

void PrintSummary(const ithax::ThreadBudget &budget,
                  const std::uint32_t workers, const std::uint32_t repetitions,
                  const std::uint32_t baseline_threads) {
  const std::uint32_t peak_threads = budget.PeakObservedThreads();
  const std::uint32_t measured_workers =
      peak_threads > baseline_threads ? peak_threads - baseline_threads : 0U;
  std::cout << "{\"event\":\"thread_budget_summary\","
            << "\"status\":\"pass\",\"workers\":" << workers
            << ",\"repetitions\":" << repetitions
            << ",\"process_cpu_set_count\":" << budget.ProcessCpuSetCount()
            << ",\"available_workers\":" << budget.AvailableWorkerCount()
            << ",\"baseline_threads\":" << baseline_threads
            << ",\"peak_threads\":" << peak_threads
            << ",\"measured_worker_threads\":" << measured_workers << "}\n";
}

} // namespace

int main(int argc, char **argv) {
  MeasurementOptions options;
  const ParseResult parse_result = ParseArguments(argc, argv, options);
  if (parse_result == ParseResult::Help) {
    PrintUsage();
    return EXIT_SUCCESS;
  }
  if (parse_result == ParseResult::Error) {
    return EXIT_FAILURE;
  }

  try {
    ithax::ThreadBudget budget(options.policy);
    const auto baseline = budget.Capture("baseline", "runtime", "thread_budget",
                                         0U, ithax::ReservationKind::Hard);
    PrintSnapshot(baseline);

    const std::uint32_t available_workers = budget.AvailableWorkerCount();
    const std::uint32_t workers = options.requested_workers.value_or(
        std::min(DEFAULT_WORKER_COUNT, available_workers));
    if (!budget.CanReserveWorkers(workers)) {
      throw ithax::ThreadBudgetError(
          "requested Taskflow workers exceed measured budget headroom");
    }

    for (std::uint32_t repetition = 1U; repetition <= options.repetitions;
         ++repetition) {
      const auto active = RunTaskflowPhase(budget, workers, repetition);
      PrintSnapshot(active);
      const auto drained = budget.Capture(
          "after_taskflow_" + std::to_string(repetition), "taskflow",
          "executor", workers, ithax::ReservationKind::Soft);
      PrintSnapshot(drained);
    }

    PrintSummary(budget, workers, options.repetitions,
                 baseline.observed_threads);
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "thread budget measurement failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
