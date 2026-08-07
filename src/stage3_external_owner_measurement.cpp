#include "thread_budget.h"

#include <array>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace {

constexpr std::uint32_t DEFAULT_REPETITIONS = 1U;
constexpr std::uint32_t MAX_REPETITIONS = 10U;
constexpr std::uint32_t DEFAULT_OWNER_COUNT = 2U;
constexpr std::uint32_t MAX_OWNER_COUNT = 4U;
constexpr std::uint32_t MIN_OPTION_VALUE = 1U;
constexpr std::chrono::milliseconds OWNER_WAIT{2'000};

struct MeasurementOptions {
  std::uint32_t repetitions = DEFAULT_REPETITIONS;
  std::uint32_t owners = DEFAULT_OWNER_COUNT;
};

struct OwnerDefinition {
  std::string_view owner;
  std::string_view subsystem;
};

constexpr std::array<OwnerDefinition, MAX_OWNER_COUNT> OWNER_DEFINITIONS = {{
    {"synthetic-renderer-owner", "renderer-validation"},
    {"synthetic-io-owner", "io-validation"},
    {"synthetic-audio-owner", "audio-validation"},
    {"synthetic-crashpad-owner", "crashpad-validation"},
}};

struct OwnerGate {
  std::condition_variable condition;
  std::mutex mutex;
  bool active = false;
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
  if (name == "--repetitions") {
    if (!ParseBoundedValue(value, MIN_OPTION_VALUE, MAX_REPETITIONS, parsed)) {
      return false;
    }
    options.repetitions = parsed;
    return true;
  }
  if (name == "--owners") {
    if (!ParseBoundedValue(value, MIN_OPTION_VALUE, MAX_OWNER_COUNT, parsed)) {
      return false;
    }
    options.owners = parsed;
    return true;
  }
  return false;
}

enum class ParseResult {
  Success,
  Help,
  Error,
};

ParseResult ParseArguments(const int argc, char **argv,
                           MeasurementOptions &options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view name(argv[index]);
    if (name == "--help") {
      return ParseResult::Help;
    }
    if (index + 1 >= argc || !SetOption(name, argv[++index], options)) {
      std::cerr << "invalid external-owner option: " << name << '\n';
      return ParseResult::Error;
    }
  }
  return ParseResult::Success;
}

void PrintUsage() {
  std::cout << "Usage: ithax-stage3-external-owner-measurement [options]\n"
            << "  --repetitions N owner phases (1-10)\n"
            << "  --owners N      synthetic owners (1-4)\n";
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
  std::cout << "{\"event\":\"external_owner_sample\","
               "\"measurement_class\":\"synthetic\",\"phase\":";
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

void ReleaseOwner(OwnerGate &gate) {
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

void RunOwnerPhase(ithax::ThreadBudget &budget,
                   const OwnerDefinition &definition) {
  const auto before = budget.Capture(
      "external_before", std::string(definition.owner),
      std::string(definition.subsystem), 0U, ithax::ReservationKind::Unknown);
  PrintSnapshot(before);

  OwnerGate gate;
  std::exception_ptr owner_error;
  std::thread owner([&]() {
    try {
      {
        std::lock_guard lock(gate.mutex);
        gate.active = true;
      }
      gate.condition.notify_all();
      std::unique_lock lock(gate.mutex);
      gate.condition.wait(lock, [&]() { return gate.release; });
    } catch (...) {
      owner_error = std::current_exception();
    }
  });

  std::exception_ptr phase_error;
  try {
    std::unique_lock lock(gate.mutex);
    const bool active = gate.condition.wait_for(
        lock, OWNER_WAIT, [&]() { return gate.active; });
    if (!active) {
      throw ithax::ThreadBudgetError(
          "external owner did not reach its active phase");
    }
    const auto active_snapshot = budget.Capture(
        "external_active", std::string(definition.owner),
        std::string(definition.subsystem), 1U, ithax::ReservationKind::Unknown);
    PrintSnapshot(active_snapshot);
    if (active_snapshot.observed_threads <= before.observed_threads) {
      throw ithax::ThreadBudgetError(
          "external owner did not increase the process thread count");
    }
  } catch (...) {
    phase_error = std::current_exception();
  }

  ReleaseOwner(gate);
  owner.join();
  RethrowThreadError(owner_error);
  RethrowThreadError(phase_error);

  const auto drained = budget.Capture(
      "external_drained", std::string(definition.owner),
      std::string(definition.subsystem), 0U, ithax::ReservationKind::Unknown);
  PrintSnapshot(drained);
  if (drained.observed_threads > before.observed_threads) {
    throw ithax::ThreadBudgetError(
        "synthetic external owner did not return to its baseline");
  }
}

void PrintSummary(const ithax::ThreadBudget &budget,
                  const MeasurementOptions &options) {
  std::cout << "{\"event\":\"external_owner_summary\","
            << "\"status\":\"pass\",\"gate_status\":\"pass\","
            << "\"measurement_class\":\"synthetic\","
            << "\"provider\":\"synthetic\",\"repetitions\":"
            << options.repetitions << ",\"owners\":" << options.owners
            << ",\"process_cpu_set_count\":"
            << budget.ProcessCpuSetCount()
            << ",\"peak_observed_threads\":"
            << budget.PeakObservedThreads() << "}\n";
}

} // namespace

int main(int argc, char **argv) {
  MeasurementOptions options;
  const auto parse_result = ParseArguments(argc, argv, options);
  if (parse_result == ParseResult::Help) {
    PrintUsage();
    return EXIT_SUCCESS;
  }
  if (parse_result == ParseResult::Error) {
    return EXIT_FAILURE;
  }

  try {
    ithax::ThreadBudgetPolicy policy;
    policy.hard_reservations = 1U;
    policy.soft_reservations = 0U;
    policy.headroom = 0U;
    ithax::ThreadBudget budget(policy);
    for (std::uint32_t repetition = 1U;
         repetition <= options.repetitions; ++repetition) {
      for (std::uint32_t owner = 0U; owner < options.owners; ++owner) {
        RunOwnerPhase(budget, OWNER_DEFINITIONS[owner]);
      }
    }
    PrintSummary(budget, options);
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "external owner measurement failed: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
