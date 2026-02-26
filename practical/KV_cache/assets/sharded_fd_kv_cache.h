#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <utility>
#include <vector>

#include "detail/flat_index_map.h"
#include "fd_token.h"

namespace kvcache {

template <typename Key,
          typename Value,
          typename Hash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
class ShardedFdKVCache {
public:
    using key_type = Key;
    using mapped_type = Value;
    using handle_type = FdToken::raw_type;

    static constexpr std::uint32_t kShardBits = 8;
    static constexpr std::uint32_t kLocalBits = FdToken::kPositionBits - kShardBits;
    static constexpr std::uint32_t kMaxShards = (1u << kShardBits);
    static constexpr std::uint32_t kLocalMask = (1u << kLocalBits) - 1u;

    // 并发版本：
    // - position 拆分为 [shard_id | local_index]
    // - 每个 shard 拥有独立的锁/索引/freelist
    // - 关键缓冲区全部预分配（锁内不扩容）
    explicit ShardedFdKVCache(std::size_t shard_count = DefaultShardCount(),
                              std::size_t reserve_hint = 0)
        : shard_count_(NormalizeShardCount(shard_count)),
          per_shard_capacity_(ComputePerShardCapacity(shard_count_, reserve_hint)),
          shards_(std::make_unique<Shard[]>(shard_count_)) {
        for (std::size_t i = 0; i < shard_count_; ++i) {
            Shard& shard = shards_[i];
            shard.slots.assign(per_shard_capacity_, Slot{});
            shard.free_positions.reserve(per_shard_capacity_);
            shard.key_to_local.Init(per_shard_capacity_);
            shard.next_unused = 0;
        }
    }

    std::size_t size() const noexcept { return size_.load(std::memory_order_relaxed); }

    bool empty() const noexcept { return size() == 0; }

    // 按 key 插入/更新，成功返回token。
    // 当目标 shard 容量满时返回 kNull。
    handle_type Insert(std::uint8_t type, const Key& key, const Value& value) {
        return InsertImpl(type, key, value, false);
    }

    handle_type InsertOrAssign(std::uint8_t type, const Key& key, const Value& value) {
        return InsertImpl(type, key, value, true);
    }

    // 便捷读接口：按 handle 读取到 out_value。
    bool Get(handle_type handle, Value* out_value) const {
        if (out_value == nullptr) {
            return false;
        }
        return Read(handle, [&](const Value& v) { *out_value = v; });
    }

    // 按 handle 读取，并在共享锁内执行调用方 reader。
    // reader 应尽量轻量，避免延长共享锁持有时间。
    template <typename Reader>
    bool Read(handle_type handle, Reader&& reader) const {
        const auto [shard_id, local] = DecodePosition(handle);
        if (!ValidShardId(shard_id) || local >= per_shard_capacity_) {
            return false;
        }

        const Shard& shard = shards_[shard_id];
        std::shared_lock<std::shared_mutex> lock(shard.mutex);
        const Slot& slot = shard.slots[local];
        if (!ValidateSlot(slot, handle)) {
            return false;
        }
        std::forward<Reader>(reader)(slot.value);
        return true;
    }

    // 按 handle 写入，并在独占锁内执行调用方 writer。
    template <typename Writer>
    bool Write(handle_type handle, Writer&& writer) {
        const auto [shard_id, local] = DecodePosition(handle);
        if (!ValidShardId(shard_id) || local >= per_shard_capacity_) {
            return false;
        }

        Shard& shard = shards_[shard_id];
        std::unique_lock<std::shared_mutex> lock(shard.mutex);
        Slot& slot = shard.slots[local];
        if (!ValidateSlot(slot, handle)) {
            return false;
        }
        std::forward<Writer>(writer)(slot.value);
        return true;
    }

    bool Update(handle_type handle, const Value& value) {
        return Write(handle, [&](Value& v) { v = value; });
    }

    bool Add(handle_type handle, const Value& delta) {
        return Write(handle, [&](Value& v) { v += delta; });
    }

    // 按 handle 删除并递增 generation，使旧 fd token 失效。
    bool Erase(handle_type handle) {
        const auto [shard_id, local] = DecodePosition(handle);
        if (!ValidShardId(shard_id) || local >= per_shard_capacity_) {
            return false;
        }

        Shard& shard = shards_[shard_id];
        std::unique_lock<std::shared_mutex> lock(shard.mutex);
        Slot& slot = shard.slots[local];
        if (!ValidateSlot(slot, handle)) {
            return false;
        }

        if (!shard.key_to_local.Erase(slot.key)) {
            return false;
        }
        slot.occupied = 0;
        slot.type = 0;
        slot.generation = NextGeneration(slot.generation);
        shard.free_positions.push_back(local);
        size_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    // 按 key 查找 handle（路径：先定位 shard，再做平铺哈希探测）。
    handle_type FindHandle(const Key& key) const {
        const std::uint32_t shard_id = ShardForKey(key);
        const Shard& shard = shards_[shard_id];
        std::shared_lock<std::shared_mutex> lock(shard.mutex);

        std::uint32_t local = 0;
        if (!shard.key_to_local.Find(key, &local)) {
            return FdToken::kNull;
        }
        const Slot& slot = shard.slots[local];
        return BuildHandle(slot.type, slot.generation, shard_id, local);
    }

    static std::size_t DefaultShardCount() noexcept {
        const auto hc = std::thread::hardware_concurrency();
        return hc == 0 ? 4u : static_cast<std::size_t>(hc);
    }

private:
    static constexpr std::uint32_t kInvalidPosition = 0xffffffffu;
    static constexpr std::uint32_t kMaxGeneration =
        (1u << FdToken::kGenerationBits) - 1u;

    // 每个 shard 内部的 slot 布局与 FdKVCache 保持一致。
    struct Slot {
        Key key{};
        Value value{};
        std::uint32_t generation{1};
        std::uint8_t type{0};
        std::uint8_t occupied{0};
        std::uint16_t reserved{0};
    };

    // alignas(64) 让可变 shard 元数据尽量隔离到不同缓存行，
    // 降低混合负载下跨 shard 的伪共享。
    struct alignas(64) Shard {
        mutable std::shared_mutex mutex;
        std::vector<Slot> slots;
        std::vector<std::uint32_t> free_positions;
        detail::FlatIndexMap<Key, Hash, KeyEqual> key_to_local;
        std::uint32_t next_unused{0};
    };

    std::size_t shard_count_{1};
    std::size_t per_shard_capacity_{1};
    std::unique_ptr<Shard[]> shards_;
    Hash hasher_{};
    std::atomic<std::size_t> size_{0};

    // 将 shard 数量限制在可编码的 id 空间内。
    static std::size_t NormalizeShardCount(std::size_t shard_count) noexcept {
        if (shard_count == 0) {
            return 1;
        }
        if (shard_count > kMaxShards) {
            return kMaxShards;
        }
        return shard_count;
    }

    static std::size_t ComputePerShardCapacity(std::size_t shard_count,
                                               std::size_t reserve_hint) noexcept {
        std::size_t total = reserve_hint;
        if (total == 0) {
            total = 1u << 15;
        }
        std::size_t per_shard = (total + shard_count - 1) / shard_count;
        if (per_shard == 0) {
            per_shard = 1;
        }
        const std::size_t hard_limit = static_cast<std::size_t>(kLocalMask) + 1;
        if (per_shard > hard_limit) {
            per_shard = hard_limit;
        }
        return per_shard;
    }

    bool ValidShardId(std::uint32_t shard_id) const noexcept {
        return shard_id < shard_count_;
    }

    // 将 shard 和 local index 打包进 FdToken 的 position 位段。
    static std::uint32_t EncodePosition(std::uint32_t shard_id,
                                        std::uint32_t local) noexcept {
        return (shard_id << kLocalBits) | (local & kLocalMask);
    }

    static std::pair<std::uint32_t, std::uint32_t> DecodePosition(
        handle_type handle) noexcept {
        if (FdToken::IsNull(handle)) {
            return {kInvalidPosition, kInvalidPosition};
        }
        const std::uint32_t pos = FdToken::Position(handle);
        const std::uint32_t shard_id = pos >> kLocalBits;
        const std::uint32_t local = pos & kLocalMask;
        return {shard_id, local};
    }

    std::uint32_t ShardForKey(const Key& key) const noexcept {
        return static_cast<std::uint32_t>(hasher_(key) % shard_count_);
    }

    // 在单个 shard 内分配本地槽位。
    static std::uint32_t AllocateLocal(Shard& shard) noexcept {
        if (!shard.free_positions.empty()) {
            const std::uint32_t local = shard.free_positions.back();
            shard.free_positions.pop_back();
            return local;
        }
        if (shard.next_unused >= shard.slots.size()) {
            return kInvalidPosition;
        }
        return shard.next_unused++;
    }

    static std::uint32_t NextGeneration(std::uint32_t g) noexcept {
        if (g >= kMaxGeneration) {
            return 1;
        }
        return g + 1;
    }

    // 校验 token 元数据是否与目标槽位一致。
    static bool ValidateSlot(const Slot& slot, handle_type handle) noexcept {
        if (slot.occupied == 0) {
            return false;
        }
        if (slot.type != FdToken::Type(handle)) {
            return false;
        }
        if (slot.generation != FdToken::Generation(handle)) {
            return false;
        }
        return true;
    }

    static handle_type BuildHandle(std::uint8_t type,
                                   std::uint32_t generation,
                                   std::uint32_t shard_id,
                                   std::uint32_t local) noexcept {
        return FdToken::Make(type, generation, EncodePosition(shard_id, local));
    }

    // Insert / InsertOrAssign 的共享实现。
    handle_type InsertImpl(std::uint8_t type,
                           const Key& key,
                           const Value& value,
                           bool assign_if_exists) {
        const std::uint32_t shard_id = ShardForKey(key);
        Shard& shard = shards_[shard_id];
        std::unique_lock<std::shared_mutex> lock(shard.mutex);

        std::uint32_t local = 0;
        if (shard.key_to_local.Find(key, &local)) {
            Slot& slot = shard.slots[local];
            if (assign_if_exists) {
                slot.value = value;
                slot.type = type;
            }
            return BuildHandle(slot.type, slot.generation, shard_id, local);
        }

        local = AllocateLocal(shard);
        if (local == kInvalidPosition) {
            return FdToken::kNull;
        }

        Slot& slot = shard.slots[local];
        slot.key = key;
        slot.value = value;
        slot.type = type;
        slot.occupied = 1;
        if (!shard.key_to_local.Insert(slot.key, local)) {
            slot.occupied = 0;
            shard.free_positions.push_back(local);
            return FdToken::kNull;
        }
        size_.fetch_add(1, std::memory_order_relaxed);
        return BuildHandle(type, slot.generation, shard_id, local);
    }
};

}  // namespace kvcache（KV 缓存命名空间）

