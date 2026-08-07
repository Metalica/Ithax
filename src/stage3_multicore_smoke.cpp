#include "ecs/ecs_world.h"
#include "jobs/job_runtime.h"
#include "runtime/runtime_supervisor.h"
#include "thread_budget.h"
#include "threading/frame_packet_channel.h"
#include "threading/scratch_allocator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t ENTITY_COUNT = 10'000U;
constexpr std::size_t PARALLEL_FOR_COUNT = 10'000U;
constexpr std::uint32_t MAX_SMOKE_WORKERS = 2U;
constexpr std::size_t CHANNEL_CAPACITY = 2U;
constexpr std::chrono::milliseconds CHANNEL_WAIT{500};

class SmokeError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void Require(const bool condition, const char *message) {
  if (!condition) {
    throw SmokeError(message);
  }
}

void TestFrameSlotBounds();
void TestFrameSlotLifecycle();
void TestFrameSlotOwnership();
void TestFrameSlotCloseWakeup();
void TestFrameSlotChannelHandoff();
void TestScratchAllocator();

std::uint64_t AdvanceWorld(ithax::ecs::EcsWorld &world,
                           ithax::jobs::JobRuntime &runtime,
                           const std::uint32_t worker_count) {
  const auto snapshot = world.Snapshot();
  std::vector<ithax::ecs::EcsJournal> journals(worker_count);
  tf::Taskflow taskflow;
  for (std::uint32_t worker = 0U; worker < worker_count; ++worker) {
    taskflow.emplace([&snapshot, &journals, worker, worker_count]() {
      ithax::ecs::BuildTransformJournalInto(
          snapshot, worker, worker_count, journals[worker]);
    });
  }

  auto handle = runtime.Submit(std::move(taskflow));
  handle.Wait();
  world.MergeJournals(journals);
  return world.StateHash();
}

void VerifyMergedState(const ithax::ecs::EcsWorld &world) {
  const auto merged_states = world.Snapshot();
  for (std::size_t index = 0U; index < merged_states.states.size(); ++index) {
    const auto expected = static_cast<std::int64_t>(index) +
                          merged_states.states[index].velocity.delta;
    Require(merged_states.states[index].transform.value == expected,
            "journal merge produced an incorrect transform");
  }
}

void TestEcsDeterminism(ithax::jobs::JobRuntime &runtime,
                        const std::uint32_t worker_count) {
  ithax::ecs::EcsWorld world;
  world.CreateEntities(ENTITY_COUNT);
  Require(world.EntityCount() == ENTITY_COUNT,
          "entity creation count mismatch");
  ithax::ecs::WorldSnapshot reusable_snapshot;
  world.CopySnapshot(reusable_snapshot);
  const auto snapshot_capacity = reusable_snapshot.states.capacity();
  world.CopySnapshot(reusable_snapshot);
  Require(reusable_snapshot.states.size() == ENTITY_COUNT &&
              reusable_snapshot.states.capacity() == snapshot_capacity,
          "reusable ECS snapshot changed its storage unexpectedly");

  const auto first_hash = AdvanceWorld(world, runtime, worker_count);
  VerifyMergedState(world);
  ithax::ecs::EcsWorld repeat_world;
  repeat_world.CreateEntities(ENTITY_COUNT);
  const auto repeat_hash = AdvanceWorld(repeat_world, runtime, worker_count);
  Require(first_hash == repeat_hash, "world merge was not deterministic");
}

void TestParallelFor(ithax::jobs::JobRuntime &runtime) {
  std::vector<std::uint64_t> values(PARALLEL_FOR_COUNT, 0U);
  auto parallel_handle = runtime.ParallelFor(
      0U, values.size(), [&values](const std::size_t index) {
        values[index] = static_cast<std::uint64_t>(index);
      });
  parallel_handle.Wait();
  const auto value_sum = std::accumulate(values.begin(), values.end(), 0ULL);
  const auto expected_sum = static_cast<std::uint64_t>(
      PARALLEL_FOR_COUNT * (PARALLEL_FOR_COUNT - 1U) / 2U);
  Require(value_sum == expected_sum, "parallel-for result was incorrect");
}

void TestCancellation(ithax::jobs::JobRuntime &runtime) {
  std::mutex cancellation_mutex;
  std::condition_variable cancellation_coordination;
  bool cancellation_started = false;
  bool cancellation_release = false;
  tf::Taskflow cancellable_task;
  cancellable_task.emplace([&]() {
    {
      std::lock_guard lock(cancellation_mutex);
      cancellation_started = true;
    }
    cancellation_coordination.notify_all();
    std::unique_lock lock(cancellation_mutex);
    cancellation_coordination.wait(lock,
                                   [&]() { return cancellation_release; });
  });
  auto cancellable_handle = runtime.Submit(std::move(cancellable_task));
  {
    std::unique_lock lock(cancellation_mutex);
    cancellation_coordination.wait_for(lock, CHANNEL_WAIT,
                                       [&]() { return cancellation_started; });
  }
  const auto cancellation_requested = cancellable_handle.Cancel();
  {
    std::lock_guard lock(cancellation_mutex);
    cancellation_release = true;
  }
  cancellation_coordination.notify_all();
  cancellable_handle.Wait();
  Require(cancellation_requested && cancellation_started,
          "cooperative task cancellation was not exercised");
}

void TestTaskFailures(ithax::jobs::JobRuntime &runtime,
                      ithax::ecs::EcsWorld &world) {
  tf::Taskflow failing_task;
  failing_task.emplace(
      []() { throw std::runtime_error("injected task error"); });
  auto failing_handle = runtime.Submit(std::move(failing_task));
  bool execution_error_seen = false;
  try {
    failing_handle.Wait();
  } catch (const ithax::jobs::JobExecutionError &) {
    execution_error_seen = true;
  }
  Require(execution_error_seen, "task failure was not consumed");

  tf::Taskflow ownership_task;
  ownership_task.emplace(
      [&world]() { static_cast<void>(world.EntityCount()); });
  auto ownership_handle = runtime.Submit(std::move(ownership_task));
  bool ownership_error_seen = false;
  try {
    ownership_handle.Wait();
  } catch (const ithax::jobs::JobExecutionError &) {
    ownership_error_seen = true;
  }
  Require(ownership_error_seen, "ECS owner violation was not reported");
}

void TestRuntimeShutdown(ithax::jobs::JobRuntime &runtime) {
  runtime.RequestStop();
  Require(runtime.State() == ithax::jobs::RuntimeState::StopRequested,
          "job runtime did not enter stop-requested state");
  runtime.Drain();
  Require(runtime.State() == ithax::jobs::RuntimeState::Stopped,
          "job runtime did not drain");

  tf::Taskflow rejected_task;
  rejected_task.emplace([]() {});
  bool submission_rejected = false;
  try {
    static_cast<void>(runtime.Submit(std::move(rejected_task)));
  } catch (const ithax::jobs::JobRuntimeError &) {
    submission_rejected = true;
  }
  Require(submission_rejected, "stopped runtime accepted a new task");
}

void TestRuntimeSupervisorLifecycle(ithax::ThreadBudget &budget,
                                    const std::uint32_t worker_count) {
  ithax::threading::FramePacketChannel channel(1U);
  ithax::runtime::RuntimeSupervisor supervisor(budget, worker_count);
  Require(supervisor.State() == ithax::runtime::LifecycleState::Starting,
          "runtime supervisor did not start in the starting state");
  supervisor.Start();
  Require(supervisor.State() == ithax::runtime::LifecycleState::Running,
          "runtime supervisor did not enter the running state");

  std::atomic<bool> task_completed = false;
  tf::Taskflow completed_task;
  completed_task.emplace([&task_completed]() { task_completed = true; });
  const auto waited_task = supervisor.Submit(std::move(completed_task));
  supervisor.Wait(waited_task);
  Require(task_completed.load(), "supervisor task did not complete");
  Require(supervisor.PendingTaskCount() == 0U,
          "supervisor retained a consumed task");

  std::atomic<std::uint32_t> reusable_runs = 0U;
  tf::Taskflow reusable_task;
  reusable_task.emplace([&reusable_runs]() { ++reusable_runs; });
  for (std::uint32_t run = 0U; run < 2U; ++run) {
    const auto reusable_id = supervisor.SubmitReusable(reusable_task);
    supervisor.Wait(reusable_id);
  }
  Require(reusable_runs.load() == 2U,
          "supervisor did not rerun a reusable taskflow");

  tf::Taskflow deferred_task;
  deferred_task.emplace([]() {});
  static_cast<void>(supervisor.Submit(std::move(deferred_task)));
  Require(supervisor.PendingTaskCount() == 1U,
          "supervisor did not retain a deferred task");

  supervisor.RequestStop(channel);
  Require(supervisor.StopToken().stop_requested(),
          "supervisor stop token was not requested");
  Require(channel.IsClosed(), "supervisor did not close the frame channel");
  Require(supervisor.State() == ithax::runtime::LifecycleState::StopRequested,
          "supervisor did not enter the stop-requested state");
  supervisor.Drain();
  Require(supervisor.State() == ithax::runtime::LifecycleState::Stopped,
          "supervisor did not reach the stopped state");
  Require(supervisor.PendingTaskCount() == 0U,
          "supervisor drain retained a task handle");

  bool submission_rejected = false;
  try {
    tf::Taskflow rejected_task;
    rejected_task.emplace([]() {});
    static_cast<void>(supervisor.Submit(std::move(rejected_task)));
  } catch (const ithax::runtime::RuntimeSupervisorError &) {
    submission_rejected = true;
  }
  Require(submission_rejected, "stopped supervisor accepted a new task");
}

void TestRuntimeSupervisorFailures(ithax::ThreadBudget &budget,
                                   const std::uint32_t worker_count) {
  ithax::runtime::RuntimeSupervisor job_supervisor(budget, worker_count);
  job_supervisor.Start();
  tf::Taskflow failing_task;
  failing_task.emplace(
      []() { throw std::runtime_error("supervisor job failure"); });
  static_cast<void>(job_supervisor.Submit(std::move(failing_task)));
  job_supervisor.Drain();
  const auto job_failure = job_supervisor.Failure();
  Require(job_supervisor.State() == ithax::runtime::LifecycleState::Failed,
          "job failure did not reach the failed state");
  Require(job_failure.has_value() &&
              job_failure->source == ithax::runtime::FailureSource::Job,
          "job failure source was not retained");
  bool job_failure_rethrown = false;
  try {
    job_supervisor.RethrowFailure();
  } catch (const ithax::jobs::JobExecutionError &) {
    job_failure_rethrown = true;
  }
  Require(job_failure_rethrown, "job failure was not rethrown");

  ithax::runtime::RuntimeSupervisor owner_supervisor(budget, worker_count);
  owner_supervisor.Start();
  owner_supervisor.ReportFailure(
      ithax::runtime::FailureSource::Owner,
      std::make_exception_ptr(SmokeError("owner failure")));
  Require(owner_supervisor.State() ==
              ithax::runtime::LifecycleState::StopRequested,
          "owner failure did not request a stop");
  owner_supervisor.Drain();
  const auto owner_failure = owner_supervisor.Failure();
  Require(owner_supervisor.State() == ithax::runtime::LifecycleState::Failed,
          "owner failure did not reach the failed state");
  Require(owner_failure.has_value() &&
              owner_failure->source == ithax::runtime::FailureSource::Owner,
          "owner failure source was not retained");

  std::mutex cancellation_mutex;
  std::condition_variable cancellation_coordination;
  bool cancellation_started = false;
  ithax::runtime::RuntimeSupervisor cancellation_supervisor(budget,
                                                            worker_count);
  cancellation_supervisor.Start();
  const auto stop_token = cancellation_supervisor.StopToken();
  tf::Taskflow cancellation_task;
  cancellation_task.emplace([&]() {
    std::unique_lock lock(cancellation_mutex);
    cancellation_started = true;
    cancellation_coordination.notify_all();
    while (!stop_token.stop_requested()) {
      cancellation_coordination.wait_for(lock, CHANNEL_WAIT);
    }
  });
  static_cast<void>(
      cancellation_supervisor.Submit(std::move(cancellation_task)));
  {
    std::unique_lock lock(cancellation_mutex);
    const bool started = cancellation_coordination.wait_for(
        lock, CHANNEL_WAIT, [&]() { return cancellation_started; });
    Require(started, "cooperative supervisor task did not start");
  }
  cancellation_supervisor.ReportFailure(
      ithax::runtime::FailureSource::Owner,
      std::make_exception_ptr(SmokeError("cooperative stop")));
  cancellation_supervisor.Drain();
  Require(cancellation_supervisor.State() ==
              ithax::runtime::LifecycleState::Failed,
          "cooperative stop did not drain to failure");
}

void TestRuntimeSupervisorPendingBound(ithax::ThreadBudget &budget,
                                       const std::uint32_t worker_count) {
  ithax::runtime::RuntimeSupervisor supervisor(budget, worker_count);
  supervisor.Start();
  for (std::size_t index = 0U;
       index < ithax::runtime::MAX_PENDING_RUNTIME_TASKS; ++index) {
    tf::Taskflow taskflow;
    taskflow.emplace([]() {});
    static_cast<void>(supervisor.Submit(std::move(taskflow)));
  }
  Require(supervisor.PendingTaskCount() ==
              ithax::runtime::MAX_PENDING_RUNTIME_TASKS,
          "supervisor pending-task count exceeded its expected bound");

  bool overflow_rejected = false;
  try {
    tf::Taskflow overflow_task;
    overflow_task.emplace([]() {});
    static_cast<void>(supervisor.Submit(std::move(overflow_task)));
  } catch (const ithax::runtime::RuntimeSupervisorError &) {
    overflow_rejected = true;
  }
  Require(overflow_rejected, "supervisor accepted an over-capacity task");
  supervisor.Drain();
  Require(supervisor.State() == ithax::runtime::LifecycleState::Stopped,
          "bounded supervisor did not drain successfully");
}

void TestJobAndEcsBoundaries(ithax::ThreadBudget &budget) {
  const auto worker_count =
      std::min(MAX_SMOKE_WORKERS, budget.AvailableWorkerCount());
  Require(worker_count > 0U, "measured budget has no smoke-test workers");

  {
    ithax::jobs::JobRuntime runtime(budget, worker_count);
    TestEcsDeterminism(runtime, worker_count);
    TestParallelFor(runtime);
    TestCancellation(runtime);
    ithax::ecs::EcsWorld owner_world;
    owner_world.CreateEntities(1U);
    TestTaskFailures(runtime, owner_world);
    TestRuntimeShutdown(runtime);
  }
  TestRuntimeSupervisorLifecycle(budget, worker_count);
  TestRuntimeSupervisorFailures(budget, worker_count);
  TestRuntimeSupervisorPendingBound(budget, worker_count);
  TestFrameSlotBounds();
  TestFrameSlotLifecycle();
  TestFrameSlotOwnership();
  TestFrameSlotCloseWakeup();
  TestFrameSlotChannelHandoff();
  TestScratchAllocator();
}

void RethrowThreadError(const std::exception_ptr &error) {
  if (error) {
    std::rethrow_exception(error);
  }
}

template <typename Error, typename Callable>
void RequireFrameSlotError(Callable callable, const char *message) {
  bool error_seen = false;
  try {
    callable();
  } catch (const Error &) {
    error_seen = true;
  }
  Require(error_seen, message);
}

template <typename Error, typename Callable>
void RequireScratchAllocatorError(Callable callable, const char *message) {
  bool error_seen = false;
  try {
    callable();
  } catch (const Error &) {
    error_seen = true;
  }
  Require(error_seen, message);
}

void TestScratchAllocator() {
  constexpr std::size_t SCRATCH_CAPACITY = 32U;
  constexpr std::size_t FIRST_ALLOCATION_SIZE = 3U;
  constexpr std::size_t ALIGNED_ALLOCATION_SIZE = 8U;
  constexpr std::size_t ALIGNED_OFFSET = 16U;
  constexpr std::size_t INVALID_ALIGNMENT =
      ithax::threading::SCRATCH_ALLOCATOR_BASE_ALIGNMENT * 2U;

  static_assert(
      !std::is_copy_constructible_v<ithax::threading::ScratchAllocator>);
  static_assert(
      !std::is_move_constructible_v<ithax::threading::ScratchAllocator>);
  RequireScratchAllocatorError<ithax::threading::ScratchAllocatorCapacityError>(
      []() { ithax::threading::ScratchAllocator allocator(0U); },
      "zero scratch capacity was accepted");
  RequireScratchAllocatorError<ithax::threading::ScratchAllocatorCapacityError>(
      []() {
        ithax::threading::ScratchAllocator allocator(
            ithax::threading::MAX_SCRATCH_ALLOCATOR_CAPACITY + 1U);
      },
      "oversized scratch capacity was accepted");

  ithax::threading::ScratchAllocator allocator(SCRATCH_CAPACITY);
  Require(allocator.IsOwnerThread(), "scratch owner was not recorded");
  Require(allocator.Capacity() == SCRATCH_CAPACITY,
          "scratch capacity was not retained");
  const auto first = allocator.Allocate(FIRST_ALLOCATION_SIZE, 1U);
  Require(first.size() == FIRST_ALLOCATION_SIZE,
          "scratch allocation size was incorrect");
  first[0] = std::byte{0xA1U};
  const auto aligned =
      allocator.Allocate(ALIGNED_ALLOCATION_SIZE, alignof(std::uint64_t));
  Require(reinterpret_cast<std::uintptr_t>(aligned.data()) %
                  alignof(std::uint64_t) ==
              0U,
          "scratch allocation alignment was incorrect");
  Require(allocator.Used() == ALIGNED_OFFSET,
          "scratch alignment padding was incorrect");
  Require(allocator.Remaining() == SCRATCH_CAPACITY - ALIGNED_OFFSET,
          "scratch remaining capacity was incorrect");

  RequireScratchAllocatorError<
      ithax::threading::ScratchAllocatorAlignmentError>(
      [&]() { static_cast<void>(allocator.Allocate(1U, 0U)); },
      "zero scratch alignment was accepted");
  RequireScratchAllocatorError<
      ithax::threading::ScratchAllocatorAlignmentError>(
      [&]() { static_cast<void>(allocator.Allocate(1U, 3U)); },
      "non-power-of-two scratch alignment was accepted");
  RequireScratchAllocatorError<
      ithax::threading::ScratchAllocatorAlignmentError>(
      [&]() { static_cast<void>(allocator.Allocate(1U, INVALID_ALIGNMENT)); },
      "over-aligned scratch allocation was accepted");

  ithax::threading::ScratchAllocator overflow_allocator(8U);
  static_cast<void>(overflow_allocator.Allocate(3U, 1U));
  RequireScratchAllocatorError<ithax::threading::ScratchAllocatorOverflowError>(
      [&]() { static_cast<void>(overflow_allocator.Allocate(4U, 8U)); },
      "scratch allocation overflow was accepted");
  Require(overflow_allocator.Used() == 3U,
          "failed scratch allocation changed the offset");

  allocator.Reset();
  const auto old_allocation = allocator.Allocate(4U, 4U);
  allocator.Reset();
  Require(allocator.Used() == 0U, "scratch reset retained the used offset");
  Require(allocator.Remaining() == allocator.Capacity(),
          "scratch reset retained the remaining capacity");
  const auto reused_allocation = allocator.Allocate(4U, 4U);
  Require(reused_allocation.data() == old_allocation.data(),
          "scratch reset did not reuse the arena start");

  bool wrong_thread_rejected = false;
  std::exception_ptr wrong_thread_error;
  std::thread wrong_thread([&]() {
    try {
      static_cast<void>(allocator.Allocate(1U, 1U));
    } catch (const ithax::threading::ScratchAllocatorOwnershipError &) {
      wrong_thread_rejected = true;
    } catch (...) {
      wrong_thread_error = std::current_exception();
    }
  });
  wrong_thread.join();
  RethrowThreadError(wrong_thread_error);
  Require(wrong_thread_rejected, "non-owner scratch allocation was accepted");
}

void TestFrameSlotBounds() {
  RequireFrameSlotError<ithax::threading::FrameSlotCapacityError>(
      []() { ithax::threading::FrameSlotPool pool(0U, 1U); },
      "zero frame slots were accepted");
  RequireFrameSlotError<ithax::threading::FrameSlotCapacityError>(
      []() {
        ithax::threading::FrameSlotPool pool(
            ithax::threading::MAX_FRAME_SLOT_COUNT + 1U, 1U);
      },
      "frame slot count exceeded its bound");
  RequireFrameSlotError<ithax::threading::FrameSlotCapacityError>(
      []() { ithax::threading::FrameSlotPool pool(1U, 0U); },
      "zero frame payload capacity was accepted");
  RequireFrameSlotError<ithax::threading::FrameSlotCapacityError>(
      []() {
        ithax::threading::FrameSlotPool pool(
            1U, ithax::threading::MAX_FRAME_SLOT_PAYLOAD_BYTES + 1U);
      },
      "frame payload capacity exceeded its bound");
  RequireFrameSlotError<ithax::threading::FrameSlotCapacityError>(
      []() {
        ithax::threading::FrameSlotPool pool(
            ithax::threading::MAX_FRAME_SLOT_COUNT,
            ithax::threading::MAX_FRAME_SLOT_PAYLOAD_BYTES);
      },
      "aggregate frame slot storage exceeded its bound");
}

void TestFrameSlotLifecycle() {
  ithax::threading::FrameSlotPool pool(2U, 8U);
  Require(pool.SlotCount() == 2U, "frame slot count was not retained");
  Require(pool.PayloadCapacity() == 8U,
          "frame slot payload capacity was not retained");

  auto writer = pool.AcquireWrite();
  Require(writer.has_value(), "frame slot writer was not acquired");
  const auto first_token = writer->Token();
  const auto writable_payload = writer->Payload();
  writable_payload[0] = std::byte{0x41U};
  writable_payload[1] = std::byte{0x42U};
  writer->SetPayloadSize(2U);
  Require(writer->PayloadSize() == 2U, "frame payload size was not retained");
  RequireFrameSlotError<ithax::threading::FrameSlotCapacityError>(
      [&]() { writer->SetPayloadSize(pool.PayloadCapacity() + 1U); },
      "oversized frame payload was accepted");
  writer->Publish();

  auto reader = pool.AcquireRead(first_token);
  Require(reader.has_value(), "published frame slot was not readable");
  Require(reader->PayloadSize() == 2U, "read payload size was incorrect");
  const auto readable_payload = reader->Payload();
  Require(readable_payload[0] == std::byte{0x41U} &&
              readable_payload[1] == std::byte{0x42U},
          "frame payload bytes were not preserved");
  reader->Release();
  Require(pool.IsQuiescent(), "released frame slot was not free");

  auto next_writer = pool.AcquireWrite();
  Require(next_writer.has_value(), "reused frame slot was not acquired");
  const auto next_token = next_writer->Token();
  Require(next_token.generation == first_token.generation + 1U,
          "frame slot generation did not advance");
  RequireFrameSlotError<ithax::threading::FrameSlotGenerationError>(
      [&]() { static_cast<void>(pool.TryAcquireRead(first_token)); },
      "stale frame slot token was accepted");
  RequireFrameSlotError<ithax::threading::FrameSlotGenerationError>(
      [&]() { static_cast<void>(pool.TryAcquireRead({2U, 1U})); },
      "invalid frame slot ID was accepted");
  RequireFrameSlotError<ithax::threading::FrameSlotGenerationError>(
      [&]() { static_cast<void>(pool.TryAcquireRead({0U, 0U})); },
      "zero frame slot generation was accepted");
  next_writer->Cancel();

  ithax::threading::FrameSlotPool cancelled_pool(1U, 8U);
  auto cancelled_writer = cancelled_pool.AcquireWrite();
  Require(cancelled_writer.has_value(),
          "cancel fixture writer was not acquired");
  const auto cancelled_token = cancelled_writer->Token();
  Require(!cancelled_pool.TryAcquireRead(cancelled_token).has_value(),
          "writing frame slot was synchronously readable");
  cancelled_writer->Cancel();
  Require(!cancelled_pool.TryAcquireRead(cancelled_token).has_value(),
          "cancelled frame slot did not report cancellation");

  ithax::threading::FrameSlotPool other_pool(1U, 8U);
  auto other_writer = other_pool.AcquireWrite();
  Require(other_writer.has_value(), "pool identity writer was not acquired");
  const auto other_token = other_writer->Token();
  RequireFrameSlotError<ithax::threading::FrameSlotGenerationError>(
      [&]() { static_cast<void>(cancelled_pool.TryAcquireRead(other_token)); },
      "cross-pool frame slot token was accepted");
  other_writer->Cancel();

  ithax::threading::FrameSlotPool discard_pool(1U, 8U);
  auto discard_writer = discard_pool.AcquireWrite();
  Require(discard_writer.has_value(),
          "discard fixture writer was not acquired");
  const auto discard_token = discard_writer->Token();
  discard_writer->Publish();
  discard_pool.Close();
  discard_pool.DiscardPublished(discard_token);
  Require(discard_pool.IsQuiescent(), "discarded frame slot was not free");
}

void TestFrameSlotOwnership() {
  ithax::threading::FrameSlotPool write_pool(1U, 8U);
  auto writer = write_pool.AcquireWrite();
  Require(writer.has_value(), "ownership fixture writer was not acquired");
  bool second_producer_rejected = false;
  bool wrong_payload_owner_rejected = false;
  std::exception_ptr producer_error;
  std::exception_ptr payload_error;
  std::thread wrong_producer([&]() {
    try {
      static_cast<void>(write_pool.TryAcquireWrite());
    } catch (const ithax::threading::FrameSlotOwnershipError &) {
      second_producer_rejected = true;
    } catch (...) {
      producer_error = std::current_exception();
    }
  });
  std::thread wrong_payload_owner([&]() {
    try {
      static_cast<void>(writer->Payload());
    } catch (const ithax::threading::FrameSlotOwnershipError &) {
      wrong_payload_owner_rejected = true;
    } catch (...) {
      payload_error = std::current_exception();
    }
  });
  wrong_producer.join();
  wrong_payload_owner.join();
  RethrowThreadError(producer_error);
  RethrowThreadError(payload_error);
  Require(second_producer_rejected, "second frame producer was accepted");
  Require(wrong_payload_owner_rejected,
          "wrong-thread frame payload access was accepted");
  writer->Cancel();

  ithax::threading::FrameSlotPool read_pool(1U, 8U);
  auto read_writer = read_pool.AcquireWrite();
  Require(read_writer.has_value(), "read ownership writer was not acquired");
  const auto token = read_writer->Token();
  read_writer->Publish();
  auto reader = read_pool.AcquireRead(token);
  Require(reader.has_value(), "read ownership reader was not acquired");
  bool second_consumer_rejected = false;
  std::exception_ptr consumer_error;
  std::thread wrong_consumer([&]() {
    try {
      static_cast<void>(read_pool.TryAcquireRead(token));
    } catch (const ithax::threading::FrameSlotOwnershipError &) {
      second_consumer_rejected = true;
    } catch (...) {
      consumer_error = std::current_exception();
    }
  });
  wrong_consumer.join();
  RethrowThreadError(consumer_error);
  Require(second_consumer_rejected, "second frame consumer was accepted");
  reader->Release();
}

void TestFrameSlotCloseWakeup() {
  ithax::threading::FrameSlotPool writer_pool(1U, 8U);
  std::mutex writer_mutex;
  std::condition_variable writer_coordination;
  bool first_published = false;
  bool second_acquire_started = false;
  bool second_acquire_returned = false;
  bool second_acquire_succeeded = false;
  bool first_ready_in_time = false;
  bool second_started_in_time = false;
  bool second_was_blocked = false;
  ithax::threading::FrameSlotToken published_token;
  std::exception_ptr writer_error;
  std::thread blocked_writer([&]() {
    try {
      auto first_writer = writer_pool.AcquireWrite();
      Require(first_writer.has_value(), "close writer was not acquired");
      published_token = first_writer->Token();
      first_writer->Publish();
      {
        std::lock_guard lock(writer_mutex);
        first_published = true;
      }
      writer_coordination.notify_all();
      {
        std::lock_guard lock(writer_mutex);
        second_acquire_started = true;
      }
      writer_coordination.notify_all();
      auto second_writer = writer_pool.AcquireWrite();
      second_acquire_succeeded = second_writer.has_value();
      if (second_writer.has_value()) {
        second_writer->Cancel();
      }
      {
        std::lock_guard lock(writer_mutex);
        second_acquire_returned = true;
      }
      writer_coordination.notify_all();
    } catch (...) {
      writer_error = std::current_exception();
    }
  });

  {
    std::unique_lock lock(writer_mutex);
    first_ready_in_time = writer_coordination.wait_for(
        lock, CHANNEL_WAIT, [&]() { return first_published; });
    second_started_in_time = writer_coordination.wait_for(
        lock, CHANNEL_WAIT, [&]() { return second_acquire_started; });
    if (second_started_in_time) {
      second_was_blocked = !writer_coordination.wait_for(
          lock, CHANNEL_WAIT, [&]() { return second_acquire_returned; });
    }
  }
  writer_pool.Close();
  blocked_writer.join();
  if (first_ready_in_time) {
    auto published_reader = writer_pool.AcquireRead(published_token);
    Require(published_reader.has_value(),
            "published slot was not drainable after close");
    published_reader->Release();
  }
  RethrowThreadError(writer_error);
  Require(first_ready_in_time, "close writer did not publish in time");
  Require(second_started_in_time,
          "close writer did not reach its blocked acquire");
  Require(second_was_blocked, "close writer was not blocked before close");
  Require(second_acquire_returned, "close writer did not wake");
  Require(!second_acquire_succeeded, "closed writer acquired a slot");
  Require(writer_pool.IsQuiescent(), "closed writer pool was not quiescent");

  ithax::threading::FrameSlotPool commit_pool(1U, 8U);
  auto commit_writer = commit_pool.AcquireWrite();
  const bool commit_acquired = commit_writer.has_value();
  bool commit_rejected = false;
  std::exception_ptr commit_error;
  if (commit_acquired) {
    commit_pool.Close();
    try {
      commit_writer->Publish();
    } catch (const ithax::threading::FrameSlotStateError &) {
      commit_rejected = true;
    } catch (...) {
      commit_error = std::current_exception();
    }
    commit_writer->Cancel();
  }
  RethrowThreadError(commit_error);
  Require(commit_acquired, "close commit writer was not acquired");
  Require(commit_rejected, "closed frame slot accepted a publication");
  Require(commit_pool.IsQuiescent(), "closed commit pool was not quiescent");

  ithax::threading::FrameSlotPool reader_pool(1U, 8U);
  std::mutex reader_mutex;
  std::condition_variable reader_coordination;
  bool reader_token_ready = false;
  bool reader_acquire_started = false;
  bool reader_acquire_returned = false;
  bool allow_writer_cancel = false;
  bool reader_woke_empty = false;
  bool reader_ready_in_time = false;
  bool reader_started_in_time = false;
  bool reader_was_blocked = false;
  std::exception_ptr reader_error;
  std::exception_ptr reader_thread_error;
  ithax::threading::FrameSlotToken writing_token;
  std::thread writing_thread([&]() {
    try {
      auto writing_lease = reader_pool.AcquireWrite();
      Require(writing_lease.has_value(),
              "close reader writer was not acquired");
      writing_token = writing_lease->Token();
      {
        std::lock_guard lock(reader_mutex);
        reader_token_ready = true;
      }
      reader_coordination.notify_all();
      std::unique_lock lock(reader_mutex);
      reader_coordination.wait(lock, [&]() { return allow_writer_cancel; });
      lock.unlock();
      writing_lease->Cancel();
    } catch (...) {
      reader_error = std::current_exception();
    }
  });
  std::thread blocked_reader([&]() {
    try {
      {
        std::unique_lock lock(reader_mutex);
        const bool ready = reader_coordination.wait_for(
            lock, CHANNEL_WAIT, [&]() { return reader_token_ready; });
        Require(ready, "close reader token was not ready");
        reader_acquire_started = true;
      }
      reader_coordination.notify_all();
      const auto result = reader_pool.AcquireRead(writing_token);
      reader_woke_empty = !result.has_value();
      {
        std::lock_guard lock(reader_mutex);
        reader_acquire_returned = true;
      }
      reader_coordination.notify_all();
    } catch (...) {
      reader_thread_error = std::current_exception();
    }
  });

  {
    std::unique_lock lock(reader_mutex);
    reader_ready_in_time =
        reader_coordination.wait_for(lock, CHANNEL_WAIT, [&]() {
          return reader_token_ready && reader_acquire_started;
        });
    if (reader_ready_in_time) {
      reader_started_in_time = !reader_coordination.wait_for(
          lock, CHANNEL_WAIT, [&]() { return reader_acquire_returned; });
      reader_was_blocked = reader_started_in_time;
    }
  }
  reader_pool.Close();
  {
    std::lock_guard lock(reader_mutex);
    allow_writer_cancel = true;
  }
  reader_coordination.notify_all();
  blocked_reader.join();
  writing_thread.join();
  RethrowThreadError(reader_error);
  RethrowThreadError(reader_thread_error);
  Require(reader_ready_in_time,
          "close reader did not reach its blocked acquire");
  Require(reader_started_in_time, "close reader was not blocked before close");
  Require(reader_was_blocked, "close reader did not wait for publication");
  Require(reader_woke_empty, "closed reader did not wake empty");
  Require(reader_pool.IsQuiescent(), "closed reader pool was not quiescent");
}

void TestFrameSlotChannelHandoff() {
  ithax::threading::FrameSlotPool slots(2U, 16U);
  ithax::threading::FramePacketChannel channel(1U);
  auto writer = slots.AcquireWrite();
  Require(writer.has_value(), "handoff writer was not acquired");
  const auto token = writer->Token();
  writer->Payload()[0] = std::byte{0x5AU};
  writer->SetPayloadSize(1U);
  channel.Publish({17U, 0x1234U, token});
  writer->Publish();

  const auto packet = channel.Consume();
  Require(packet.has_value(), "handoff packet was not consumed");
  Require(packet->slot.has_value(), "handoff packet lost its slot token");
  Require(packet->slot.value() == token,
          "handoff packet carried the wrong slot token");
  auto reader = slots.AcquireRead(packet->slot.value());
  Require(reader.has_value(), "handoff slot was not readable");
  Require(reader->PayloadSize() == 1U, "handoff payload size was incorrect");
  Require(reader->Payload()[0] == std::byte{0x5AU},
          "handoff payload byte was incorrect");
  reader->Release();
  channel.Close();
  slots.Close();
  Require(slots.IsQuiescent(), "handoff slots were not quiescent");
}

void TestFramePacketChannel() {
  ithax::threading::FramePacketChannel channel(CHANNEL_CAPACITY);
  std::mutex coordination_mutex;
  std::condition_variable coordination;
  bool publish_started = false;
  bool allow_consume = false;
  bool producer_failed = false;
  bool producer_done = false;
  bool consumer_failed = false;
  bool consumer_done = false;
  bool full_rejected = false;
  bool empty_observed = false;
  bool consumed_in_order = false;
  std::exception_ptr producer_error;
  std::exception_ptr consumer_error;

  std::thread consumer([&]() {
    try {
      std::unique_lock lock(coordination_mutex);
      coordination.wait(lock,
                        [&]() { return allow_consume || producer_failed; });
      if (producer_failed) {
        lock.unlock();
        {
          std::lock_guard done_lock(coordination_mutex);
          consumer_done = true;
        }
        coordination.notify_all();
        return;
      }
      lock.unlock();

      const auto first = channel.Consume();
      Require(first.has_value(), "channel closed before first packet");
      consumed_in_order = first->generation == 1U;

      lock.lock();
      coordination.wait(lock, [&]() { return producer_done; });
      lock.unlock();
      const auto second = channel.TryConsume();
      const auto third = channel.TryConsume();
      empty_observed = !channel.TryConsume().has_value();
      Require(second.has_value() && third.has_value(),
              "channel did not retain queued packets");
      Require(second->generation == 2U && third->generation == 3U,
              "channel packet order changed");
    } catch (...) {
      {
        std::lock_guard error_lock(coordination_mutex);
        consumer_error = std::current_exception();
        consumer_failed = true;
        consumer_done = true;
      }
      coordination.notify_all();
      return;
    }
    {
      std::lock_guard done_lock(coordination_mutex);
      consumer_done = true;
    }
    coordination.notify_all();
  });

  std::thread producer([&]() {
    try {
      Require(channel.TryPublish({1U, 11U}), "first packet was not published");
      Require(channel.TryPublish({2U, 22U}), "second packet was not published");
      full_rejected = !channel.TryPublish({99U, 99U});
      {
        std::lock_guard lock(coordination_mutex);
        publish_started = true;
      }
      coordination.notify_all();
      channel.Publish({3U, 33U});
      {
        std::lock_guard lock(coordination_mutex);
        producer_done = true;
      }
      coordination.notify_all();
    } catch (...) {
      {
        std::lock_guard lock(coordination_mutex);
        producer_error = std::current_exception();
        producer_failed = true;
        producer_done = true;
      }
      coordination.notify_all();
    }
  });

  bool started_in_time = false;
  bool producer_blocked = false;
  {
    std::unique_lock lock(coordination_mutex);
    started_in_time = coordination.wait_for(lock, CHANNEL_WAIT, [&]() {
      return publish_started || producer_failed;
    });
    if (started_in_time && !producer_failed) {
      producer_blocked = !coordination.wait_for(lock, CHANNEL_WAIT, [&]() {
        return producer_done || consumer_failed;
      });
    }
    allow_consume = true;
  }
  coordination.notify_all();
  bool close_before_join = false;
  {
    std::unique_lock lock(coordination_mutex);
    coordination.wait_for(lock, CHANNEL_WAIT,
                          [&]() { return producer_done || consumer_done; });
    close_before_join = !producer_done;
  }
  if (close_before_join) {
    channel.Close();
  }
  producer.join();
  consumer.join();
  channel.Close();

  RethrowThreadError(consumer_error);
  RethrowThreadError(producer_error);
  Require(started_in_time, "channel producer did not start in time");
  Require(producer_blocked, "full channel did not apply backpressure");
  Require(full_rejected, "full channel did not reject try-publish");
  Require(consumed_in_order,
          "channel consumer received the wrong first packet");
  Require(empty_observed, "channel empty state was not observable");
  Require(channel.Size() == 0U, "channel retained packets after draining");
  Require(channel.Capacity() == CHANNEL_CAPACITY, "channel capacity changed");

  bool ownership_rejected = false;
  try {
    static_cast<void>(channel.TryPublish({4U, 44U}));
  } catch (const ithax::threading::FramePacketChannelError &) {
    ownership_rejected = true;
  }
  Require(ownership_rejected, "channel accepted a second producer");

  ithax::threading::FramePacketChannel closing_channel(1U);
  std::mutex close_mutex;
  std::condition_variable close_coordination;
  bool close_consumer_started = false;
  bool close_consumer_done = false;
  bool close_wakeup_seen = false;
  std::exception_ptr close_consumer_error;
  std::thread blocked_consumer([&]() {
    try {
      {
        std::lock_guard lock(close_mutex);
        close_consumer_started = true;
      }
      close_coordination.notify_all();
      const auto packet = closing_channel.Consume();
      close_wakeup_seen = !packet.has_value();
    } catch (...) {
      close_consumer_error = std::current_exception();
    }
    {
      std::lock_guard lock(close_mutex);
      close_consumer_done = true;
    }
    close_coordination.notify_all();
  });

  bool close_started_in_time = false;
  {
    std::unique_lock lock(close_mutex);
    close_started_in_time = close_coordination.wait_for(
        lock, CHANNEL_WAIT, [&]() { return close_consumer_started; });
  }
  closing_channel.Close();
  blocked_consumer.join();
  RethrowThreadError(close_consumer_error);
  Require(close_started_in_time, "close-wakeup consumer did not start");
  Require(close_consumer_done, "close-wakeup consumer did not finish");
  Require(close_wakeup_seen, "channel close did not wake the consumer");
  Require(closing_channel.IsClosed(), "channel close state was not retained");

  ithax::threading::FramePacketChannel closing_producer_channel(1U);
  std::mutex producer_close_mutex;
  std::condition_variable producer_close_coordination;
  bool producer_close_started = false;
  bool producer_close_rejected = false;
  std::exception_ptr producer_close_error;
  std::thread blocked_producer([&]() {
    try {
      Require(closing_producer_channel.TryPublish({1U, 11U}),
              "producer close fixture did not publish its first packet");
      {
        std::lock_guard lock(producer_close_mutex);
        producer_close_started = true;
      }
      producer_close_coordination.notify_all();
      closing_producer_channel.Publish({2U, 22U});
    } catch (const ithax::threading::FramePacketChannelError &) {
      producer_close_rejected = true;
    } catch (...) {
      producer_close_error = std::current_exception();
    }
  });

  bool producer_started_in_time = false;
  {
    std::unique_lock lock(producer_close_mutex);
    producer_started_in_time = producer_close_coordination.wait_for(
        lock, CHANNEL_WAIT, [&]() { return producer_close_started; });
  }
  closing_producer_channel.Close();
  blocked_producer.join();
  RethrowThreadError(producer_close_error);
  Require(producer_started_in_time,
          "close-wakeup producer did not start in time");
  Require(producer_close_rejected,
          "channel close did not wake a blocked producer");
}

} // namespace

int main() {
  try {
    ithax::ThreadBudgetPolicy policy;
    policy.hard_reservations = 1U;
    policy.soft_reservations = 0U;
    policy.headroom = 1U;
    ithax::ThreadBudget budget(policy);
    budget.Capture("stage3-baseline", "main-owner", "platform", 1U,
                   ithax::ReservationKind::Hard);

    TestJobAndEcsBoundaries(budget);
    TestFramePacketChannel();
    std::cout << "stage3_multicore_smoke passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "stage3_multicore_smoke failed: " << error.what() << '\n';
    return 1;
  }
}
