#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <entt/entt.hpp>
#include <taskflow/taskflow.hpp>

namespace
{
constexpr std::size_t DEFAULT_TASK_COUNT = 10'000U;
constexpr std::size_t DEFAULT_ENTITY_COUNT = 10'000U;
constexpr std::size_t DEFAULT_REPETITIONS = 5U;
constexpr std::size_t DEFAULT_WORKER_LIMIT = 4U;
constexpr std::size_t MIN_BENCHMARK_VALUE = 1U;
constexpr std::size_t MAX_TASK_COUNT = 1'000'000U;
constexpr std::size_t MAX_ENTITY_COUNT = 1'000'000U;
constexpr std::size_t MAX_REPETITIONS = 100U;
constexpr std::size_t MAX_WORKERS = 64U;
constexpr std::size_t MAX_METADATA_LENGTH = 64U;
constexpr std::size_t P50_PERCENTILE = 50U;
constexpr std::size_t P95_PERCENTILE = 95U;
constexpr std::size_t P99_PERCENTILE = 99U;

struct BenchmarkOptions
{
	std::size_t workers = 1U;
	std::size_t tasks = DEFAULT_TASK_COUNT;
	std::size_t entities = DEFAULT_ENTITY_COUNT;
	std::size_t repetitions = DEFAULT_REPETITIONS;
};

struct MonolithicEntity
{
	std::uint64_t value = 0U;
};

struct EcsComponent
{
	std::uint64_t value = 0U;
};

struct BenchmarkSample
{
	double taskflow_microseconds = 0.0;
	double ecs_microseconds = 0.0;
	double monolithic_microseconds = 0.0;
	std::uint64_t taskflow_checksum = 0U;
	std::uint64_t ecs_checksum = 0U;
	std::uint64_t monolithic_checksum = 0U;
};

struct BenchmarkSummary
{
	std::vector<double> taskflow_microseconds;
	std::vector<double> ecs_microseconds;
	std::vector<double> monolithic_microseconds;
	std::uint64_t taskflow_checksum = 0U;
	std::uint64_t ecs_checksum = 0U;
	std::uint64_t monolithic_checksum = 0U;
};

enum class ParseResult
{
	Success,
	Help,
	Error,
};

std::size_t DefaultWorkerCount()
{
	const auto hardware_threads = static_cast<std::size_t>(
		std::thread::hardware_concurrency());
	if (hardware_threads == 0U)
	{
		return MIN_BENCHMARK_VALUE;
	}
	return std::min(hardware_threads, DEFAULT_WORKER_LIMIT);
}

bool ParseBoundedValue(
	const std::string_view text,
	const std::size_t minimum,
	const std::size_t maximum,
	std::size_t& value)
{
	if (text.empty())
	{
		return false;
	}

	const auto [end, error] = std::from_chars(
		text.data(), text.data() + text.size(), value);
	return error == std::errc{} && end == text.data() + text.size() &&
		value >= minimum && value <= maximum;
}

bool SetOption(
	const std::string_view name,
	const std::string_view value,
	BenchmarkOptions& options)
{
	if (name == "--workers")
	{
		return ParseBoundedValue(
			value, MIN_BENCHMARK_VALUE, MAX_WORKERS, options.workers);
	}
	if (name == "--tasks")
	{
		return ParseBoundedValue(
			value, MIN_BENCHMARK_VALUE, MAX_TASK_COUNT, options.tasks);
	}
	if (name == "--entities")
	{
		return ParseBoundedValue(
			value, MIN_BENCHMARK_VALUE, MAX_ENTITY_COUNT, options.entities);
	}
	if (name == "--repetitions")
	{
		return ParseBoundedValue(
			value, MIN_BENCHMARK_VALUE, MAX_REPETITIONS, options.repetitions);
	}
	return false;
}

ParseResult ParseArguments(
	const int argc,
	char** argv,
	BenchmarkOptions& options)
{
	options.workers = DefaultWorkerCount();
	for (int index = 1; index < argc; ++index)
	{
		const std::string_view name(argv[index]);
		if (name == "--help")
		{
			return ParseResult::Help;
		}
		if (index + 1 >= argc ||
			!SetOption(name, argv[++index], options))
		{
			std::cerr << "invalid benchmark option: " << name << '\n';
			return ParseResult::Error;
		}
	}
	return ParseResult::Success;
}

void PrintUsage()
{
	std::cout << "Usage: ithax-stage2-benchmark [options]\n"
			  << "  --workers N       worker count (1-64)\n"
			  << "  --tasks N         task count (1-1000000)\n"
			  << "  --entities N      entity count (1-1000000)\n"
			  << "  --repetitions N   samples (1-100)\n";
}

template <typename Callable>
double MeasureMicroseconds(Callable&& callable)
{
	const auto start = std::chrono::steady_clock::now();
	std::forward<Callable>(callable)();
	const auto finish = std::chrono::steady_clock::now();
	return std::chrono::duration<double, std::micro>(finish - start).count();
}

std::uint64_t RunTaskflow(const BenchmarkOptions& options)
{
	tf::Executor executor(options.workers);
	tf::Taskflow taskflow;
	std::atomic<std::size_t> completed = 0U;
	for (std::size_t index = 0U; index < options.tasks; ++index)
	{
		taskflow.emplace([&completed]() {
			completed.fetch_add(1U, std::memory_order_relaxed);
		});
	}

	executor.run(taskflow).wait();
	if (completed.load(std::memory_order_relaxed) != options.tasks)
	{
		throw std::runtime_error("Taskflow did not complete every task");
	}
	return static_cast<std::uint64_t>(completed.load());
}

std::uint64_t ExpectedEntityChecksum(const std::size_t count)
{
	const auto unsigned_count = static_cast<std::uint64_t>(count);
	return (unsigned_count * (unsigned_count + 1U)) / 2U;
}

std::uint64_t RunEcs(const BenchmarkOptions& options)
{
	entt::registry registry;
	for (std::size_t index = 0U; index < options.entities; ++index)
	{
		const auto entity = registry.create();
		registry.emplace<EcsComponent>(entity, index);
	}

	std::uint64_t checksum = 0U;
	const auto view = registry.view<EcsComponent>();
	for (const auto entity : view)
	{
		auto& component = view.get<EcsComponent>(entity);
		component.value += 1U;
		checksum += component.value;
	}
	return checksum;
}

std::uint64_t RunMonolithic(const BenchmarkOptions& options)
{
	std::vector<MonolithicEntity> entities(options.entities);
	for (std::size_t index = 0U; index < options.entities; ++index)
	{
		entities[index].value = static_cast<std::uint64_t>(index);
	}

	std::uint64_t checksum = 0U;
	for (auto& entity : entities)
	{
		entity.value += 1U;
		checksum += entity.value;
	}
	return checksum;
}

BenchmarkSample RunSample(const BenchmarkOptions& options)
{
	BenchmarkSample sample;
	sample.taskflow_microseconds = MeasureMicroseconds([&sample, &options]() {
		sample.taskflow_checksum = RunTaskflow(options);
	});
	sample.ecs_microseconds = MeasureMicroseconds([&sample, &options]() {
		sample.ecs_checksum = RunEcs(options);
	});
	sample.monolithic_microseconds = MeasureMicroseconds(
		[&sample, &options]() {
			sample.monolithic_checksum = RunMonolithic(options);
		});
	return sample;
}

BenchmarkSummary RunBenchmark(const BenchmarkOptions& options)
{
	BenchmarkSummary summary;
	summary.taskflow_microseconds.reserve(options.repetitions);
	summary.ecs_microseconds.reserve(options.repetitions);
	summary.monolithic_microseconds.reserve(options.repetitions);
	for (std::size_t index = 0U; index < options.repetitions; ++index)
	{
		const auto sample = RunSample(options);
		if (index == 0U)
		{
			summary.taskflow_checksum = sample.taskflow_checksum;
			summary.ecs_checksum = sample.ecs_checksum;
			summary.monolithic_checksum = sample.monolithic_checksum;
		}
		if (sample.taskflow_checksum != summary.taskflow_checksum ||
			sample.ecs_checksum != summary.ecs_checksum ||
			sample.monolithic_checksum != summary.monolithic_checksum)
		{
			throw std::runtime_error("benchmark checksum was not deterministic");
		}
		summary.taskflow_microseconds.push_back(
			sample.taskflow_microseconds);
		summary.ecs_microseconds.push_back(sample.ecs_microseconds);
		summary.monolithic_microseconds.push_back(
			sample.monolithic_microseconds);
	}
	return summary;
}

double Percentile(std::vector<double> values, const std::size_t percentile)
{
	std::sort(values.begin(), values.end());
	const auto last = values.size() - 1U;
	const auto position = (last * percentile) / 100U;
	return values[position];
}

std::string GetCommitMetadata()
{
	const char* raw_commit = std::getenv("ITHAX_BENCHMARK_COMMIT");
	if (raw_commit == nullptr)
	{
		return "unknown";
	}

	const std::string value(raw_commit);
	if (value.empty() || value.size() > MAX_METADATA_LENGTH)
	{
		return "unknown";
	}
	for (const char character : value)
	{
		const bool is_valid =
			(character >= 'a' && character <= 'z') ||
			(character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '.' ||
			character == '/' || character == '-';
		if (!is_valid)
		{
			return "unknown";
		}
	}
	return value;
}

void PrintTiming(
	const std::string_view name,
	const std::vector<double>& samples)
{
	std::cout << '"' << name << "\":{\"p50_us\":"
			<< std::fixed << std::setprecision(3)
			<< Percentile(samples, P50_PERCENTILE) << ",\"p95_us\":"
			<< Percentile(samples, P95_PERCENTILE) << ",\"p99_us\":"
			<< Percentile(samples, P99_PERCENTILE) << '}';
}

void PrintJson(
	const BenchmarkOptions& options,
	const BenchmarkSummary& summary)
{
	const auto hardware_threads = std::thread::hardware_concurrency();
	std::cout << "{\"benchmark\":\"stage2\",\"commit\":\""
			  << GetCommitMetadata() << "\",\"hardware_threads\":"
			  << hardware_threads << ",\"workers\":" << options.workers
			  << ",\"tasks\":" << options.tasks << ",\"entities\":"
			  << options.entities << ",\"repetitions\":"
			  << options.repetitions << ",";
	PrintTiming("taskflow", summary.taskflow_microseconds);
	std::cout << ',';
	PrintTiming("ecs", summary.ecs_microseconds);
	std::cout << ',';
	PrintTiming("monolithic", summary.monolithic_microseconds);
	std::cout << ",\"checksums\":{\"taskflow\":"
			  << summary.taskflow_checksum << ",\"ecs\":"
			  << summary.ecs_checksum << ",\"monolithic\":"
			  << summary.monolithic_checksum << "}}\n";
}
}

int main(int argc, char** argv)
{
	BenchmarkOptions options;
	const auto parse_result = ParseArguments(argc, argv, options);
	if (parse_result == ParseResult::Help)
	{
		PrintUsage();
		return 0;
	}
	if (parse_result == ParseResult::Error)
	{
		return 2;
	}

	try
	{
		const auto summary = RunBenchmark(options);
		if (summary.ecs_checksum != ExpectedEntityChecksum(options.entities) ||
			summary.monolithic_checksum !=
				ExpectedEntityChecksum(options.entities))
		{
			throw std::runtime_error("entity checksum was incorrect");
		}
		PrintJson(options, summary);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "stage2 benchmark failed: " << error.what() << '\n';
		return 1;
	}
}
