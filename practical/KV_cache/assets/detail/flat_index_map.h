#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>
#include <vector>

namespace kvcache {
namespace detail {

// 向上取整到最近的 2 的幂。
// 这样 FlatIndexMap 可以用 index = hash & mask 做快速寻址。
inline std::size_t NextPowerOfTwo(std::size_t n) noexcept {
    if (n <= 1) {
        return 1;
    }
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    if constexpr (sizeof(std::size_t) >= 8) {
        n |= n >> 32;
    }
    return n + 1;
}

// 固定容量、平铺（连续内存）的哈希表，仅用于 key->index 映射。
// 设计目标：
// 1) 查找路径无节点分配、无指针追逐。
// 2) 初始化后不再扩容或 rehash。
// 3) 平均 O(1) 且缓存访问模式可预测。
template <typename Key, typename Hash, typename KeyEqual>
class FlatIndexMap {
public:
    static_assert(std::is_default_constructible<Key>::value,
                  "FlatIndexMap requires default-constructible Key");

    using mapped_type = std::uint32_t;

    FlatIndexMap() = default;

    explicit FlatIndexMap(std::size_t max_entries) { Init(max_entries); }

    // 预分配该索引表所需的全部存储。
    // 逻辑容量是 max_entries_，底层桶数组为 2 倍并向上取整到 2 的幂，
    // 以降低线性探测链长度。
    void Init(std::size_t max_entries) {
        if (max_entries == 0) {
            max_entries = 1;
        }
        max_entries_ = max_entries;
        const std::size_t capacity = NextPowerOfTwo(max_entries_ * 2);
        table_.assign(capacity, Entry{});
        mask_ = capacity - 1;
        size_ = 0;
        tombstones_ = 0;
    }

    std::size_t size() const noexcept { return size_; }

    // 线性探测查找；遇到第一个 kEmpty 即可提前停止。
    bool Find(const Key& key, mapped_type* out_value) const noexcept {
        if (table_.empty()) {
            return false;
        }
        std::size_t idx = ProbeStart(key);
        for (std::size_t i = 0; i < table_.size(); ++i) {
            const Entry& entry = table_[idx];
            if (entry.state == kEmpty) {
                return false;
            }
            if (entry.state == kOccupied && equal_(entry.key, key)) {
                if (out_value != nullptr) {
                    *out_value = entry.value;
                }
                return true;
            }
            idx = NextIndex(idx);
        }
        return false;
    }

    // 插入新键或更新已有键。
    // 当表未初始化或逻辑容量耗尽时返回 false。
    bool Insert(const Key& key, mapped_type value) noexcept {
        if (table_.empty()) {
            return false;
        }

        std::size_t idx = ProbeStart(key);
        std::size_t first_deleted = kNpos;

        for (std::size_t i = 0; i < table_.size(); ++i) {
            Entry& entry = table_[idx];
            if (entry.state == kEmpty) {
                return InsertAt(first_deleted == kNpos ? idx : first_deleted, key, value);
            }
            if (entry.state == kDeleted) {
                if (first_deleted == kNpos) {
                    first_deleted = idx;
                }
            } else if (equal_(entry.key, key)) {
                entry.value = value;
                return true;
            }
            idx = NextIndex(idx);
        }

        if (first_deleted != kNpos) {
            return InsertAt(first_deleted, key, value);
        }
        return false;
    }

    // 删除时标记为墓碑（kDeleted），而不是直接清空为 kEmpty，以保持探测链完整。
    bool Erase(const Key& key) noexcept {
        if (table_.empty()) {
            return false;
        }

        std::size_t idx = ProbeStart(key);
        for (std::size_t i = 0; i < table_.size(); ++i) {
            Entry& entry = table_[idx];
            if (entry.state == kEmpty) {
                return false;
            }
            if (entry.state == kOccupied && equal_(entry.key, key)) {
                entry.state = kDeleted;
                --size_;
                ++tombstones_;
                return true;
            }
            idx = NextIndex(idx);
        }
        return false;
    }

private:
    enum : std::uint8_t {
        kEmpty = 0,
        kOccupied = 1,
        kDeleted = 2,
    };

    static constexpr std::size_t kNpos = std::numeric_limits<std::size_t>::max();

    struct Entry {
        Key key{};
        mapped_type value{0};
        // kEmpty: 从未使用
        // kOccupied: 当前有效
        // kDeleted: 墓碑
        std::uint8_t state{kEmpty};
    };

    std::vector<Entry> table_;
    std::size_t mask_{0};
    std::size_t max_entries_{0};
    std::size_t size_{0};
    std::size_t tombstones_{0};
    Hash hasher_{};
    KeyEqual equal_{};

    std::size_t ProbeStart(const Key& key) const noexcept {
        return hasher_(key) & mask_;
    }

    std::size_t NextIndex(std::size_t idx) const noexcept { return (idx + 1) & mask_; }

    // 逻辑容量约束，避免隐藏式增长。
    bool CanInsertNew() const noexcept { return size_ < max_entries_; }

    bool InsertAt(std::size_t idx, const Key& key, mapped_type value) noexcept {
        Entry& entry = table_[idx];
        if (entry.state == kOccupied) {
            entry.value = value;
            return true;
        }
        if (!CanInsertNew()) {
            return false;
        }
        if (entry.state == kDeleted) {
            --tombstones_;
        }
        entry.key = key;
        entry.value = value;
        entry.state = kOccupied;
        ++size_;
        return true;
    }
};

}  // namespace detail（内部实现）
}  // namespace kvcache（KV 缓存命名空间）

