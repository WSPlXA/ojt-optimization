#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <benchmark/benchmark.h>

#include "fd_kv_cache.h"
#include "fd_token.h"

namespace {

using Key = std::uint64_t;
using Value = std::uint64_t;
using Handle = kvcache::FdToken::raw_type;

constexpr std::size_t kItemCount = 1u << 18;
constexpr std::size_t kProbeCount = 1u << 15;
constexpr std::size_t kInsertEraseCount = 1u << 15;
constexpr std::size_t kConcurrentProbeCount = 1u << 16;
constexpr std::uint8_t kNodeType = 1;

struct Dataset {
    kvcache::FdKVCache<Key, Value> fd_cache{kItemCount};
    std::unordered_map<Key, Value> unordered;
    std::map<Key, Value> ordered;
    std::vector<Key> keys;
    std::vector<Handle> handles;
    std::vector<std::size_t> probes;
    std::vector<Key> insert_keys;

    Dataset() {
        unordered.reserve(kItemCount);
        keys.reserve(kItemCount);
        handles.reserve(kItemCount);
        probes.reserve(kProbeCount);
        insert_keys.reserve(kInsertEraseCount);

        for (std::size_t i = 0; i < kItemCount; ++i) {
            const Key key = static_cast<Key>(i) * 11400714819323198485ull +
                            0x9e3779b97f4a7c15ull;
            const Value value = static_cast<Value>(i);
            keys.push_back(key);
            handles.push_back(fd_cache.Insert(kNodeType, key, value));
            unordered.emplace(key, value);
            ordered.emplace(key, value);
        }

        std::uint64_t x = 0x123456789abcdef0ull;
        for (std::size_t i = 0; i < kProbeCount; ++i) {
            x = x * 6364136223846793005ull + 1ull;
            probes.push_back(static_cast<std::size_t>(x & (kItemCount - 1)));
        }

        for (std::size_t i = 0; i < kInsertEraseCount; ++i) {
            const Key key = (static_cast<Key>(i) * 0x9e3779b97f4a7c15ull) ^
                            0xd1b54a32d192ed03ull;
            insert_keys.push_back(key);
        }
    }
};

Dataset& GetDataset() {
    static Dataset data;
    return data;
}

int MaxBenchThreads() {
    const unsigned hc = std::thread::hardware_concurrency();
    if (hc == 0) {
        return 4;
    }
    return static_cast<int>(std::min<unsigned>(hc, 16));
}

std::size_t ConcurrentShardCount() {
    const std::size_t hc =
        std::max<std::size_t>(1, static_cast<std::size_t>(std::thread::hardware_concurrency()));
    return std::min<std::size_t>(kvcache::ShardedFdKVCache<Key, Value>::kMaxShards, hc * 2);
}

struct ConcurrentDataset {
    kvcache::ShardedFdKVCache<Key, Value> fd_cache;
    std::unordered_map<Key, Value> unordered;
    std::map<Key, Value> ordered;
    mutable std::shared_mutex unordered_mutex;
    mutable std::shared_mutex ordered_mutex;
    std::vector<Key> keys;
    std::vector<Handle> handles;
    std::vector<std::size_t> probes;

    ConcurrentDataset() : fd_cache(ConcurrentShardCount(), kItemCount) {
        unordered.reserve(kItemCount);
        keys.reserve(kItemCount);
        handles.reserve(kItemCount);
        probes.reserve(kConcurrentProbeCount);

        for (std::size_t i = 0; i < kItemCount; ++i) {
            const Key key = static_cast<Key>(i) * 11400714819323198485ull +
                            0x9e3779b97f4a7c15ull;
            const Value value = static_cast<Value>(i);
            keys.push_back(key);
            handles.push_back(fd_cache.Insert(kNodeType, key, value));
            unordered.emplace(key, value);
            ordered.emplace(key, value);
        }

        std::uint64_t x = 0x0ddc0ffeebadf00dull;
        for (std::size_t i = 0; i < kConcurrentProbeCount; ++i) {
            x = x * 2862933555777941757ull + 3037000493ull;
            probes.push_back(static_cast<std::size_t>(x & (kItemCount - 1)));
        }
    }
};

ConcurrentDataset& GetConcurrentDataset() {
    static ConcurrentDataset data;
    return data;
}

void BM_FdKV_Read(benchmark::State& state) {
    Dataset& data = GetDataset();
    for (auto _ : state) {
        Value sum = 0;
        for (const std::size_t idx : data.probes) {
            const Value* ptr = data.fd_cache.Get(data.handles[idx]);
            sum += *ptr;
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kProbeCount));
}
BENCHMARK(BM_FdKV_Read)->Unit(benchmark::kMicrosecond);

void BM_UnorderedMap_Read(benchmark::State& state) {
    Dataset& data = GetDataset();
    for (auto _ : state) {
        Value sum = 0;
        for (const std::size_t idx : data.probes) {
            sum += data.unordered.find(data.keys[idx])->second;
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kProbeCount));
}
BENCHMARK(BM_UnorderedMap_Read)->Unit(benchmark::kMicrosecond);

void BM_Map_Read(benchmark::State& state) {
    Dataset& data = GetDataset();
    for (auto _ : state) {
        Value sum = 0;
        for (const std::size_t idx : data.probes) {
            sum += data.ordered.find(data.keys[idx])->second;
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kProbeCount));
}
BENCHMARK(BM_Map_Read)->Unit(benchmark::kMicrosecond);

void BM_FdKV_Update(benchmark::State& state) {
    Dataset& data = GetDataset();
    for (auto _ : state) {
        for (const std::size_t idx : data.probes) {
            Value* ptr = data.fd_cache.Get(data.handles[idx]);
            *ptr += 1;
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kProbeCount));
}
BENCHMARK(BM_FdKV_Update)->Unit(benchmark::kMicrosecond);

void BM_UnorderedMap_Update(benchmark::State& state) {
    Dataset& data = GetDataset();
    for (auto _ : state) {
        for (const std::size_t idx : data.probes) {
            data.unordered.find(data.keys[idx])->second += 1;
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kProbeCount));
}
BENCHMARK(BM_UnorderedMap_Update)->Unit(benchmark::kMicrosecond);

void BM_Map_Update(benchmark::State& state) {
    Dataset& data = GetDataset();
    for (auto _ : state) {
        for (const std::size_t idx : data.probes) {
            data.ordered.find(data.keys[idx])->second += 1;
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kProbeCount));
}
BENCHMARK(BM_Map_Update)->Unit(benchmark::kMicrosecond);

void BM_FdKV_InsertErase(benchmark::State& state) {
    Dataset& data = GetDataset();
    std::vector<Handle> handles;
    handles.reserve(kInsertEraseCount);

    for (auto _ : state) {
        state.PauseTiming();
        kvcache::FdKVCache<Key, Value> cache(kInsertEraseCount);
        handles.clear();
        state.ResumeTiming();

        for (std::size_t i = 0; i < kInsertEraseCount; ++i) {
            handles.push_back(
                cache.Insert(kNodeType, data.insert_keys[i], static_cast<Value>(i)));
        }

        std::size_t erased = 0;
        for (const Handle handle : handles) {
            erased += static_cast<std::size_t>(cache.Erase(handle));
        }

        benchmark::DoNotOptimize(erased);
        benchmark::DoNotOptimize(cache.size());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations() * kInsertEraseCount * 2));
}
BENCHMARK(BM_FdKV_InsertErase)->Unit(benchmark::kMicrosecond);

void BM_UnorderedMap_InsertErase(benchmark::State& state) {
    Dataset& data = GetDataset();

    for (auto _ : state) {
        state.PauseTiming();
        std::unordered_map<Key, Value> cache;
        cache.reserve(kInsertEraseCount);
        state.ResumeTiming();

        for (std::size_t i = 0; i < kInsertEraseCount; ++i) {
            cache.emplace(data.insert_keys[i], static_cast<Value>(i));
        }

        std::size_t erased = 0;
        for (const Key key : data.insert_keys) {
            erased += cache.erase(key);
        }

        benchmark::DoNotOptimize(erased);
        benchmark::DoNotOptimize(cache.size());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations() * kInsertEraseCount * 2));
}
BENCHMARK(BM_UnorderedMap_InsertErase)->Unit(benchmark::kMicrosecond);

void BM_Map_InsertErase(benchmark::State& state) {
    Dataset& data = GetDataset();

    for (auto _ : state) {
        state.PauseTiming();
        std::map<Key, Value> cache;
        state.ResumeTiming();

        for (std::size_t i = 0; i < kInsertEraseCount; ++i) {
            cache.emplace(data.insert_keys[i], static_cast<Value>(i));
        }

        std::size_t erased = 0;
        for (const Key key : data.insert_keys) {
            erased += cache.erase(key);
        }

        benchmark::DoNotOptimize(erased);
        benchmark::DoNotOptimize(cache.size());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations() * kInsertEraseCount * 2));
}
BENCHMARK(BM_Map_InsertErase)->Unit(benchmark::kMicrosecond);

void BM_MT_FdKV_Read(benchmark::State& state) {
    ConcurrentDataset& data = GetConcurrentDataset();
    const std::size_t thread_index = static_cast<std::size_t>(state.thread_index());
    const std::size_t thread_count = static_cast<std::size_t>(state.threads());
    const std::size_t n = data.probes.size();
    std::size_t ops_per_iter = 0;
    for (std::size_t i = thread_index; i < n; i += thread_count) {
        ++ops_per_iter;
    }

    for (auto _ : state) {
        Value sum = 0;
        for (std::size_t i = thread_index; i < n; i += thread_count) {
            const std::size_t idx = data.probes[i];
            data.fd_cache.Read(data.handles[idx], [&](const Value& value) { sum += value; });
        }
        benchmark::DoNotOptimize(sum);
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * ops_per_iter));
}
BENCHMARK(BM_MT_FdKV_Read)->ThreadRange(1, MaxBenchThreads())->Unit(benchmark::kMicrosecond);

void BM_MT_UnorderedMap_Read(benchmark::State& state) {
    ConcurrentDataset& data = GetConcurrentDataset();
    const std::size_t thread_index = static_cast<std::size_t>(state.thread_index());
    const std::size_t thread_count = static_cast<std::size_t>(state.threads());
    const std::size_t n = data.probes.size();
    std::size_t ops_per_iter = 0;
    for (std::size_t i = thread_index; i < n; i += thread_count) {
        ++ops_per_iter;
    }

    for (auto _ : state) {
        Value sum = 0;
        std::shared_lock<std::shared_mutex> lock(data.unordered_mutex);
        for (std::size_t i = thread_index; i < n; i += thread_count) {
            const std::size_t idx = data.probes[i];
            sum += data.unordered.find(data.keys[idx])->second;
        }
        benchmark::DoNotOptimize(sum);
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * ops_per_iter));
}
BENCHMARK(BM_MT_UnorderedMap_Read)
    ->ThreadRange(1, MaxBenchThreads())
    ->Unit(benchmark::kMicrosecond);

void BM_MT_Map_Read(benchmark::State& state) {
    ConcurrentDataset& data = GetConcurrentDataset();
    const std::size_t thread_index = static_cast<std::size_t>(state.thread_index());
    const std::size_t thread_count = static_cast<std::size_t>(state.threads());
    const std::size_t n = data.probes.size();
    std::size_t ops_per_iter = 0;
    for (std::size_t i = thread_index; i < n; i += thread_count) {
        ++ops_per_iter;
    }

    for (auto _ : state) {
        Value sum = 0;
        std::shared_lock<std::shared_mutex> lock(data.ordered_mutex);
        for (std::size_t i = thread_index; i < n; i += thread_count) {
            const std::size_t idx = data.probes[i];
            sum += data.ordered.find(data.keys[idx])->second;
        }
        benchmark::DoNotOptimize(sum);
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * ops_per_iter));
}
BENCHMARK(BM_MT_Map_Read)->ThreadRange(1, MaxBenchThreads())->Unit(benchmark::kMicrosecond);

void BM_MT_FdKV_Update(benchmark::State& state) {
    ConcurrentDataset& data = GetConcurrentDataset();
    const std::size_t thread_index = static_cast<std::size_t>(state.thread_index());
    const std::size_t thread_count = static_cast<std::size_t>(state.threads());
    const std::size_t n = data.probes.size();
    std::size_t ops_per_iter = 0;
    for (std::size_t i = thread_index; i < n; i += thread_count) {
        ++ops_per_iter;
    }

    for (auto _ : state) {
        std::size_t ok = 0;
        for (std::size_t i = thread_index; i < n; i += thread_count) {
            const std::size_t idx = data.probes[i];
            ok += static_cast<std::size_t>(data.fd_cache.Add(data.handles[idx], 1));
        }
        benchmark::DoNotOptimize(ok);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * ops_per_iter));
}
BENCHMARK(BM_MT_FdKV_Update)->ThreadRange(1, MaxBenchThreads())->Unit(benchmark::kMicrosecond);

void BM_MT_UnorderedMap_Update(benchmark::State& state) {
    ConcurrentDataset& data = GetConcurrentDataset();
    const std::size_t thread_index = static_cast<std::size_t>(state.thread_index());
    const std::size_t thread_count = static_cast<std::size_t>(state.threads());
    const std::size_t n = data.probes.size();
    std::size_t ops_per_iter = 0;
    for (std::size_t i = thread_index; i < n; i += thread_count) {
        ++ops_per_iter;
    }

    for (auto _ : state) {
        std::size_t ok = 0;
        for (std::size_t i = thread_index; i < n; i += thread_count) {
            const std::size_t idx = data.probes[i];
            std::unique_lock<std::shared_mutex> lock(data.unordered_mutex);
            auto it = data.unordered.find(data.keys[idx]);
            if (it != data.unordered.end()) {
                it->second += 1;
                ++ok;
            }
        }
        benchmark::DoNotOptimize(ok);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * ops_per_iter));
}
BENCHMARK(BM_MT_UnorderedMap_Update)
    ->ThreadRange(1, MaxBenchThreads())
    ->Unit(benchmark::kMicrosecond);

void BM_MT_Map_Update(benchmark::State& state) {
    ConcurrentDataset& data = GetConcurrentDataset();
    const std::size_t thread_index = static_cast<std::size_t>(state.thread_index());
    const std::size_t thread_count = static_cast<std::size_t>(state.threads());
    const std::size_t n = data.probes.size();
    std::size_t ops_per_iter = 0;
    for (std::size_t i = thread_index; i < n; i += thread_count) {
        ++ops_per_iter;
    }

    for (auto _ : state) {
        std::size_t ok = 0;
        for (std::size_t i = thread_index; i < n; i += thread_count) {
            const std::size_t idx = data.probes[i];
            std::unique_lock<std::shared_mutex> lock(data.ordered_mutex);
            auto it = data.ordered.find(data.keys[idx]);
            if (it != data.ordered.end()) {
                it->second += 1;
                ++ok;
            }
        }
        benchmark::DoNotOptimize(ok);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * ops_per_iter));
}
BENCHMARK(BM_MT_Map_Update)->ThreadRange(1, MaxBenchThreads())->Unit(benchmark::kMicrosecond);

}  // namespace

BENCHMARK_MAIN();
