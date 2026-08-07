#include "ecs/ecs_world.h"
#include "runtime/runtime_supervisor.h"
#include "thread_budget.h"
#include "threading/frame_packet_channel.h"
#include "threading/frame_slot.h"
#include "threading/scratch_allocator.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t INTEGRATION_ENTITY_COUNT = 10'000U;
constexpr std::uint32_t INTEGRATION_TICK_COUNT = 1'000U;
constexpr std::uint32_t MAX_INTEGRATION_WORKERS = 4U;
constexpr std::uint32_t WORKER_SCHEDULE_SEED = 0x5EED'0301U;
constexpr std::size_t FRAME_SLOT_COUNT = 2U;
constexpr std::size_t FRAME_PAYLOAD_BYTES = 16U;
constexpr std::size_t FRAME_CHANNEL_CAPACITY = 2U;
constexpr std::size_t TICK_SCRATCH_CAPACITY = 256U;
constexpr std::chrono::milliseconds FRAME_WAIT{2'000};

class IntegrationError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void Require(const bool condition, const char *message) {
  if (!condition) {
    throw IntegrationError(message);
  }
}

void RethrowThreadError(const std::exception_ptr &error) {
  if (error) {
    std::rethrow_exception(error);
  }
}

struct FrameRecord {
  std::uint64_t tick = 0U;
  std::uint64_t world_hash = 0U;
};

static_assert(sizeof(FrameRecord) == FRAME_PAYLOAD_BYTES);

struct FrameCoordination {
  std::condition_variable condition;
  std::mutex mutex;
  std::optional<FrameRecord> pending;
  std::exception_ptr error;
  bool in_flight = false;
  bool stop_requested = false;
};

class FrameExchange {
public:
  FrameExchange(const std::uint64_t first_tick,
                const std::uint32_t expected_frames)
      : m_slots(FRAME_SLOT_COUNT, FRAME_PAYLOAD_BYTES),
        m_channel(FRAME_CHANNEL_CAPACITY),
        m_first_tick(first_tick),
        m_expected_frames(expected_frames) {
    try {
      m_consumer = std::thread([this]() { ConsumeFrames(); });
      m_producer = std::thread([this]() { ProduceFrames(); });
    } catch (...) {
      m_channel.Close();
      m_slots.Close();
      if (m_producer.joinable()) {
        m_producer.join();
      }
      if (m_consumer.joinable()) {
        m_consumer.join();
      }
      throw;
    }
  }

  ~FrameExchange() noexcept { StopNoexcept(); }

  FrameExchange(const FrameExchange &) = delete;
  FrameExchange &operator=(const FrameExchange &) = delete;

  void Publish(const FrameRecord record) {
    std::unique_lock lock(m_coordination.mutex);
    if (m_stopped) {
      throw IntegrationError("frame exchange was already stopped");
    }
    if (m_coordination.error) {
      const auto error = m_coordination.error;
      lock.unlock();
      std::rethrow_exception(error);
    }
    if (m_coordination.pending || m_coordination.in_flight) {
      throw IntegrationError("frame exchange accepted overlapping frames");
    }

    m_coordination.pending = record;
    m_coordination.in_flight = true;
    lock.unlock();
    m_coordination.condition.notify_all();

    lock.lock();
    const bool completed = m_coordination.condition.wait_for(
        lock, FRAME_WAIT, [this]() {
          return !m_coordination.in_flight || m_coordination.error != nullptr;
        });
    if (!completed) {
      const auto error = std::make_exception_ptr(
          IntegrationError("frame exchange did not complete in time"));
      if (!m_coordination.error) {
        m_coordination.error = error;
      }
      const auto stored_error = m_coordination.error;
      m_coordination.pending.reset();
      m_coordination.in_flight = false;
      m_coordination.stop_requested = true;
      lock.unlock();
      m_channel.Close();
      m_slots.Close();
      m_coordination.condition.notify_all();
      std::rethrow_exception(stored_error);
    }

    const auto error = m_coordination.error;
    lock.unlock();
    if (error) {
      std::rethrow_exception(error);
    }
  }

  void Stop() {
    RequestStop();
    JoinThreads();
    {
      std::lock_guard lock(m_coordination.mutex);
      m_stopped = true;
    }
    Require(m_channel.IsClosed(), "frame channel did not close");
    Require(m_channel.Size() == 0U,
            "frame channel retained a packet after shutdown");
    Require(m_slots.IsQuiescent(), "frame slots were not quiescent");
    RethrowStoredError();
  }

  void StopNoexcept() noexcept {
    if (m_stopped) {
      return;
    }
    RequestStop();
    JoinThreads();
    m_stopped = true;
    if (!m_slots.IsQuiescent()) {
      std::terminate();
    }
  }

private:
  void RequestStop() noexcept {
    {
      std::lock_guard lock(m_coordination.mutex);
      m_coordination.stop_requested = true;
      m_coordination.pending.reset();
      m_coordination.in_flight = false;
    }
    m_channel.Close();
    m_slots.Close();
    m_coordination.condition.notify_all();
  }

  void JoinThreads() noexcept {
    if (m_producer.joinable()) {
      m_producer.join();
    }
    if (m_consumer.joinable()) {
      m_consumer.join();
    }
  }

  void RecordError(const std::exception_ptr error) noexcept {
    {
      std::lock_guard lock(m_coordination.mutex);
      if (!m_coordination.error) {
        m_coordination.error = error;
      }
      m_coordination.stop_requested = true;
      m_coordination.pending.reset();
      m_coordination.in_flight = false;
    }
    m_coordination.condition.notify_all();
  }

  void RethrowStoredError() {
    std::exception_ptr error;
    {
      std::lock_guard lock(m_coordination.mutex);
      error = m_coordination.error;
    }
    RethrowThreadError(error);
  }

  void ProduceFrames() noexcept {
    try {
      for (;;) {
        FrameRecord record;
        {
          std::unique_lock lock(m_coordination.mutex);
          m_coordination.condition.wait(lock, [this]() {
            return m_coordination.pending.has_value() ||
                   m_coordination.stop_requested;
          });
          if (m_coordination.stop_requested &&
              !m_coordination.pending.has_value()) {
            break;
          }
          record = *m_coordination.pending;
          m_coordination.pending.reset();
        }

        auto writer = m_slots.AcquireWrite();
        if (!writer.has_value()) {
          throw IntegrationError("frame producer could not acquire a slot");
        }
        const auto token = writer->Token();
        const auto payload = writer->Payload();
        std::memcpy(payload.data(), &record, sizeof(record));
        writer->SetPayloadSize(sizeof(record));

        m_channel.Publish({record.tick, record.world_hash, token});
        writer->Publish();
      }
      m_channel.Close();
    } catch (...) {
      RecordError(std::current_exception());
      m_channel.Close();
      m_slots.Close();
    }
  }

  void ConsumeFrames() noexcept {
    try {
      std::uint64_t expected_tick = m_first_tick;
      for (;;) {
        const auto packet = m_channel.Consume();
        if (!packet.has_value()) {
          break;
        }
        Require(packet->slot.has_value(),
                "frame packet did not carry a slot token");
        auto reader = m_slots.AcquireRead(packet->slot.value());
        Require(reader.has_value(), "frame consumer could not acquire a slot");
        Require(reader->PayloadSize() == sizeof(FrameRecord),
                "frame payload size was incorrect");
        FrameRecord record;
        std::memcpy(&record, reader->Payload().data(), sizeof(record));
        Require(record.tick == expected_tick,
                "frame consumer received an out-of-order tick");
        Require(record.tick == packet->generation,
                "frame packet generation did not match its payload");
        Require(record.world_hash == packet->world_hash,
                "frame packet hash did not match its payload");
        reader->Release();
        {
          std::lock_guard lock(m_coordination.mutex);
          m_coordination.in_flight = false;
        }
        m_coordination.condition.notify_all();
        ++expected_tick;
      }
      Require(expected_tick ==
                  m_first_tick + static_cast<std::uint64_t>(m_expected_frames),
              "frame consumer did not receive every integration tick");
    } catch (...) {
      RecordError(std::current_exception());
      m_channel.Close();
      m_slots.Close();
    }
  }

  ithax::threading::FrameSlotPool m_slots;
  ithax::threading::FramePacketChannel m_channel;
  FrameCoordination m_coordination;
  std::thread m_producer;
  std::thread m_consumer;
  std::uint64_t m_first_tick;
  std::uint32_t m_expected_frames;
  bool m_stopped = false;
};

struct ScratchTickRecord {
  std::uint64_t tick = 0U;
  std::uint64_t entity_count = 0U;
  std::uint32_t worker_index = 0U;
};

void UseWorkerScratch(const std::uint64_t tick,
                      const std::size_t entity_count,
                      const std::uint32_t worker_index,
                      std::vector<std::uint64_t> &scratch_checksums) {
  thread_local std::unique_ptr<ithax::threading::ScratchAllocator> scratch;
  if (!scratch) {
    scratch = std::make_unique<ithax::threading::ScratchAllocator>(
        TICK_SCRATCH_CAPACITY);
  }
  scratch->Reset();
  const auto storage = scratch->Allocate(
      sizeof(ScratchTickRecord), alignof(ScratchTickRecord));
  const ScratchTickRecord expected{
      tick, static_cast<std::uint64_t>(entity_count), worker_index};
  std::memcpy(storage.data(), &expected, sizeof(expected));
  ScratchTickRecord observed;
  std::memcpy(&observed, storage.data(), sizeof(observed));
  Require(observed.tick == tick &&
              observed.entity_count == static_cast<std::uint64_t>(
                                           entity_count) &&
              observed.worker_index == worker_index,
          "worker scratch data was not preserved");
  scratch_checksums[worker_index] =
      observed.tick ^ observed.entity_count ^ observed.worker_index;
  scratch->Reset();
}

tf::Taskflow BuildTickTasks(
    const ithax::ecs::WorldSnapshot &snapshot,
    const std::uint64_t tick,
    const std::uint32_t worker_count,
    std::vector<ithax::ecs::EcsJournal> &journals,
    std::vector<std::uint64_t> &scratch_checksums) {
  tf::Taskflow taskflow;
  for (std::uint32_t worker = 0U; worker < worker_count; ++worker) {
    taskflow.emplace([&snapshot, tick, worker, worker_count, &journals,
                      &scratch_checksums]() {
      UseWorkerScratch(tick, snapshot.states.size(), worker, scratch_checksums);
      journals[worker] = ithax::ecs::BuildTransformJournal(
          snapshot, worker, worker_count);
    });
  }
  return taskflow;
}

void VerifyScratchResults(const std::uint64_t tick,
                          const std::uint32_t worker_count,
                          const std::vector<std::uint64_t> &checksums) {
  for (std::uint32_t worker = 0U; worker < worker_count; ++worker) {
    const auto expected = tick ^ static_cast<std::uint64_t>(
                                    INTEGRATION_ENTITY_COUNT) ^ worker;
    Require(checksums[worker] == expected,
            "a worker did not complete its scratch phase");
  }
}

void VerifyWorldTick(const ithax::ecs::WorldSnapshot &snapshot,
                     const std::uint64_t tick) {
  Require(snapshot.states.size() == INTEGRATION_ENTITY_COUNT,
          "integration world entity count changed");
  for (std::size_t index = 0U; index < snapshot.states.size(); ++index) {
    const auto expected = static_cast<std::int64_t>(index) +
                          static_cast<std::int64_t>(tick);
    Require(snapshot.states[index].transform.value == expected,
            "integration world state diverged");
    Require(snapshot.states[index].velocity.delta == 1,
            "integration velocity state changed");
  }
}

std::uint64_t RunTick(ithax::ecs::EcsWorld &world,
                      ithax::runtime::RuntimeSupervisor &supervisor,
                      const std::uint32_t worker_count,
                      const std::uint64_t tick) {
  const auto snapshot = world.Snapshot();
  std::vector<ithax::ecs::EcsJournal> journals(worker_count);
  std::vector<std::uint64_t> scratch_checksums(worker_count, 0U);
  auto taskflow = BuildTickTasks(
      snapshot, tick, worker_count, journals, scratch_checksums);
  const auto task_id = supervisor.Submit(std::move(taskflow));
  supervisor.Wait(task_id);
  VerifyScratchResults(tick, worker_count, scratch_checksums);
  world.MergeJournals(journals);
  const auto merged_snapshot = world.Snapshot();
  VerifyWorldTick(merged_snapshot, tick);
  return world.StateHash();
}

void RunWorkerPhase(ithax::ecs::EcsWorld &world, FrameExchange &frames,
                    ithax::ThreadBudget &budget,
                    const std::uint32_t worker_count,
                    const std::uint64_t first_tick,
                    const std::uint32_t tick_count,
                    std::uint64_t &final_hash) {
  ithax::runtime::RuntimeSupervisor supervisor(budget, worker_count);
  supervisor.Start();
  try {
    const auto end_tick =
        first_tick + static_cast<std::uint64_t>(tick_count);
    for (std::uint64_t tick = first_tick; tick < end_tick; ++tick) {
      final_hash = RunTick(world, supervisor, worker_count, tick);
      frames.Publish({tick, final_hash});
    }
    supervisor.RequestStop();
    supervisor.Drain();
  } catch (...) {
    supervisor.RequestStop();
    supervisor.Drain();
    throw;
  }
  Require(supervisor.State() == ithax::runtime::LifecycleState::Stopped,
          "integration supervisor did not stop cleanly");
}

std::uint64_t RunIntegration(
    ithax::ThreadBudget &budget,
    const std::vector<std::uint32_t> &worker_schedule) {
  ithax::ecs::EcsWorld world;
  world.CreateEntities(INTEGRATION_ENTITY_COUNT);
  FrameExchange frames(1U, INTEGRATION_TICK_COUNT);
  std::uint64_t final_hash = 0U;

  try {
    const auto base_ticks = INTEGRATION_TICK_COUNT /
                            static_cast<std::uint32_t>(
                                worker_schedule.size());
    const auto extra_ticks = INTEGRATION_TICK_COUNT %
                              static_cast<std::uint32_t>(
                                  worker_schedule.size());
    std::uint64_t next_tick = 1U;
    for (std::size_t phase = 0U; phase < worker_schedule.size(); ++phase) {
      const auto phase_ticks = base_ticks +
                                (phase < extra_ticks ? 1U : 0U);
      RunWorkerPhase(world, frames, budget, worker_schedule[phase], next_tick,
                     phase_ticks, final_hash);
      next_tick += phase_ticks;
    }
    frames.Stop();
    Require(next_tick == static_cast<std::uint64_t>(
                              INTEGRATION_TICK_COUNT) + 1U,
            "integration did not execute the expected tick count");
  } catch (...) {
    frames.StopNoexcept();
    throw;
  }
  return final_hash;
}

void TestExternalOwnerFailure(ithax::ThreadBudget &budget,
                              const std::uint32_t worker_count) {
  ithax::runtime::RuntimeSupervisor supervisor(budget, worker_count);
  supervisor.Start();
  std::exception_ptr owner_thread_error;
  std::thread owner([&]() {
    try {
      supervisor.ReportFailure(
          ithax::runtime::FailureSource::Owner,
          std::make_exception_ptr(
              IntegrationError("injected external owner failure")));
    } catch (...) {
      owner_thread_error = std::current_exception();
    }
  });
  owner.join();
  RethrowThreadError(owner_thread_error);
  supervisor.Drain();
  const auto failure = supervisor.Failure();
  Require(supervisor.State() == ithax::runtime::LifecycleState::Failed,
          "external owner failure did not fail the supervisor");
  Require(failure.has_value() &&
              failure->source == ithax::runtime::FailureSource::Owner,
          "external owner failure source was not retained");
}

std::vector<std::uint32_t> BuildWorkerSchedule(
    const std::uint32_t available_workers) {
  const auto worker_limit =
      std::min(MAX_INTEGRATION_WORKERS, available_workers);
  Require(worker_limit > 0U, "thread budget has no integration workers");
  std::vector<std::uint32_t> schedule;
  schedule.reserve(worker_limit);
  for (std::uint32_t worker = 1U; worker <= worker_limit; ++worker) {
    schedule.push_back(worker);
  }
  std::mt19937 generator(WORKER_SCHEDULE_SEED);
  std::shuffle(schedule.begin(), schedule.end(), generator);
  return schedule;
}

} // namespace

int main() {
  try {
    ithax::ThreadBudgetPolicy policy;
    policy.hard_reservations = 1U;
    policy.soft_reservations = 0U;
    policy.headroom = 1U;
    ithax::ThreadBudget budget(policy);
    budget.Capture("stage3_integration_baseline", "main-owner",
                   "native-runtime", 1U, ithax::ReservationKind::Hard);
    const auto schedule = BuildWorkerSchedule(budget.AvailableWorkerCount());

    const auto final_hash = RunIntegration(budget, schedule);
    TestExternalOwnerFailure(budget, schedule.front());
    std::cout << "stage3_multicore_integration passed ticks="
              << INTEGRATION_TICK_COUNT
              << " worker_runs=" << schedule.size()
              << " final_hash=" << final_hash << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "stage3_multicore_integration failed: " << error.what()
              << '\n';
    return 1;
  }
}
