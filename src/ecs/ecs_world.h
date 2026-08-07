#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

#include <entt/entity/registry.hpp>

namespace ithax::ecs {

constexpr std::size_t MAX_WORLD_ENTITIES = 1'000'000U;
constexpr std::size_t MAX_WORLD_WORKERS = 64U;
constexpr std::size_t MAX_JOURNAL_UPDATES = 1'000'000U;

struct TransformComponent {
  std::int64_t value = 0;
};

struct VelocityComponent {
  std::int64_t delta = 0;
};

struct EntityState {
  entt::entity entity = entt::null;
  TransformComponent transform;
  VelocityComponent velocity;
};

struct WorldSnapshot {
  std::vector<EntityState> states;
};

struct TransformUpdate {
  entt::entity entity = entt::null;
  std::int64_t value = 0;
  std::size_t sequence = 0U;
};

struct EcsJournal {
  std::size_t worker_index = 0U;
  std::vector<TransformUpdate> transforms;
};

class EcsError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class EcsOwnershipError : public EcsError {
public:
  using EcsError::EcsError;
};

class EcsWorld {
public:
  EcsWorld();

  EcsWorld(const EcsWorld &) = delete;
  EcsWorld &operator=(const EcsWorld &) = delete;
  EcsWorld(EcsWorld &&) = delete;
  EcsWorld &operator=(EcsWorld &&) = delete;

  void CreateEntities(std::size_t count);
  WorldSnapshot Snapshot() const;
  void CopySnapshot(WorldSnapshot &snapshot) const;
  void MergeJournals(std::vector<EcsJournal> &journals);

  std::size_t EntityCount() const;
  std::uint64_t StateHash() const;

private:
  void AssertOwner() const;

  entt::registry m_registry;
  std::vector<entt::entity> m_entity_order;
  std::thread::id m_owner_thread;
  std::size_t m_entity_count = 0U;
};

EcsJournal BuildTransformJournal(
    const WorldSnapshot &snapshot,
    std::size_t worker_index,
    std::size_t worker_count);

void BuildTransformJournalInto(
    const WorldSnapshot &snapshot,
    std::size_t worker_index,
    std::size_t worker_count,
    EcsJournal &journal);

} // namespace ithax::ecs
