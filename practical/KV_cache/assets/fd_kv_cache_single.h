#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "detail/flat_index_map.h"
#include "fd_token.h"

namespace kvcache {

template <typename Key,
          typename Value,
          typename Hash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
class FdKVCache {
public:
    using key_type = Key;
    using mapped_type = Value;
    using handle_type = FdToken::raw_type;

    // 单线程版本：
    // - slots_ 连续存储，提升数据局部性
    // - 平铺 key->position 索引
    // - 每次 Get/Erase 都做 fd token 校验
    explicit FdKVCache(std::size_t reserve_hint = 0) { Reserve(reserve_hint); }

    // 初始化数据槽和索引表的固定容量存储。
    // 热路径上不会再发生隐式扩容。
    void Reserve(std::size_t n) {
        if (n == 0) {
            n = 1;
        }
        slots_.assign(n, Slot{});
        free_positions_.clear();
        free_positions_.reserve(n);
        key_to_position_.Init(n);
        next_unused_ = 0;
        size_ = 0;
    }

    std::size_t size() const noexcept { return size_; }

    bool empty() const noexcept { return size_ == 0; }

    // 插入新 key/value 并返回 token。
    // 若 key 已存在，返回现有 token。
    // 若容量耗尽，返回 kNull。
    handle_type Insert(std::uint8_t type, const Key& key, const Value& value) {
        std::uint32_t pos = 0;
        if (key_to_position_.Find(key, &pos)) {
            return BuildHandle(pos);
        }

        pos = AllocatePosition();
        if (pos == kInvalidPosition) {
            return FdToken::kNull;
        }

        Slot& slot = slots_[pos];
        slot.key = key;
        slot.value = value;
        slot.type = type;
        slot.occupied = 1;
        if (!key_to_position_.Insert(slot.key, pos)) {
            slot.occupied = 0;
            free_positions_.push_back(pos);
            return FdToken::kNull;
        }
        ++size_;
        return BuildHandle(pos);
    }

    // 按 key 执行 upsert。
    // 已存在时保持 position/generation 不变，只更新 type/value。
    handle_type InsertOrAssign(std::uint8_t type, const Key& key, const Value& value) {
        std::uint32_t pos = 0;
        if (key_to_position_.Find(key, &pos)) {
            Slot& slot = slots_[pos];
            slot.value = value;
            slot.type = type;
            return BuildHandle(pos);
        }
        return Insert(type, key, value);
    }

    // 快路径：校验 token 后直接返回 slots_ 内部指针。
    Value* Get(handle_type handle) noexcept {
        const std::uint32_t pos = ValidateHandle(handle);
        if (pos == kInvalidPosition) {
            return nullptr;
        }
        return &slots_[pos].value;
    }

    const Value* Get(handle_type handle) const noexcept {
        const std::uint32_t pos = ValidateHandle(handle);
        if (pos == kInvalidPosition) {
            return nullptr;
        }
        return &slots_[pos].value;
    }

    // 按token 删除；删除后递增 generation，失效旧句柄。
    bool Erase(handle_type handle) {
        const std::uint32_t pos = ValidateHandle(handle);
        if (pos == kInvalidPosition) {
            return false;
        }

        Slot& slot = slots_[pos];
        if (!key_to_position_.Erase(slot.key)) {
            return false;
        }
        slot.occupied = 0;
        slot.type = 0;
        slot.generation = NextGeneration(slot.generation);
        free_positions_.push_back(pos);
        --size_;
        return true;
    }

    // 按 key 查找 token。
    handle_type FindHandle(const Key& key) const noexcept {
        std::uint32_t pos = 0;
        if (!key_to_position_.Find(key, &pos)) {
            return FdToken::kNull;
        }
        return BuildHandle(pos);
    }

private:
    static constexpr std::uint32_t kInvalidPosition = 0xffffffffu;

    // 紧凑槽结构：
    // Key + Value 始终实体化（无 std::optional 额外开销）。
    // occupied 表示存活状态，generation 用于防止旧句柄误访问。
    struct Slot {
        Key key{};
        Value value{};
        std::uint32_t generation{1};
        std::uint8_t type{0};
        std::uint8_t occupied{0};
        std::uint16_t reserved{0};
    };

    static constexpr std::uint32_t kMaxGeneration =
        (1u << FdToken::kGenerationBits) - 1u;

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_positions_;
    detail::FlatIndexMap<Key, Hash, KeyEqual> key_to_position_;
    std::uint32_t next_unused_{0};
    std::size_t size_{0};

    // 优先从 freelist 分配；否则走单调递增的 next_unused_。
    std::uint32_t AllocatePosition() {
        if (!free_positions_.empty()) {
            const std::uint32_t pos = free_positions_.back();
            free_positions_.pop_back();
            return pos;
        }
        if (next_unused_ >= slots_.size()) {
            return kInvalidPosition;
        }
        return next_unused_++;
    }

    static std::uint32_t NextGeneration(std::uint32_t g) noexcept {
        if (g >= kMaxGeneration) {
            return 1;
        }
        return g + 1;
    }

    handle_type BuildHandle(std::uint32_t pos) const noexcept {
        const Slot& slot = slots_[pos];
        return FdToken::Make(slot.type, slot.generation, pos);
    }

    // 用当前槽元数据校验 [type|generation|position]。
    std::uint32_t ValidateHandle(handle_type handle) const noexcept {
        if (FdToken::IsNull(handle)) {
            return kInvalidPosition;
        }

        const std::uint32_t pos = FdToken::Position(handle);
        if (pos >= slots_.size()) {
            return kInvalidPosition;
        }

        const Slot& slot = slots_[pos];
        if (slot.occupied == 0) {
            return kInvalidPosition;
        }
        if (slot.type != FdToken::Type(handle)) {
            return kInvalidPosition;
        }
        if (slot.generation != FdToken::Generation(handle)) {
            return kInvalidPosition;
        }
        return pos;
    }
};

}  // namespace kvcache（KV 缓存命名空间）

