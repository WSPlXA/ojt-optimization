#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <array>
#include <benchmark/benchmark.h>
#include <x86intrin.h>
#include <omp.h>

/**
 * @file main.cpp
 * @brief 测试不同步长（stride）下的并行写入性能。
 *
 * 通过改变步长 i += k，观察缓存行与预取对吞吐的影响。
 */
// L1: 32KB
// L2: 256KB
// L3: 12MB

/**
 * @brief 最大测试规模（约 1GB 的 float 数组）。
 */
constexpr size_t n = 1<<28;

/**
 * @brief 全局数组缓冲区，避免在基准循环内重复分配内存。
 */
std::vector<float> a(n);  // 1GB

/**
 * @brief 步长为 1 的并行写入基准（连续访问）。
 * @param bm Google Benchmark 状态对象。
 */
void BM_skip1(benchmark::State &bm) {
    for (auto _: bm) {
#pragma omp parallel for
        for (size_t i = 0; i < n; i += 1) {
            a[i] = 1;
        }
        // 防止编译器将写入优化掉，保证基准有效性。
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_skip1);

/**
 * @brief 步长为 2 的并行写入基准。
 * @param bm Google Benchmark 状态对象。
 */
void BM_skip2(benchmark::State &bm) {
    for (auto _: bm) {
#pragma omp parallel for
        for (size_t i = 0; i < n; i += 2) {
            a[i] = 1;
        }
        // 保留结果可见性，避免被激进优化影响测试结论。
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_skip2);

/**
 * @brief 步长为 4 的并行写入基准。
 * @param bm Google Benchmark 状态对象。
 */
void BM_skip4(benchmark::State &bm) {
    for (auto _: bm) {
#pragma omp parallel for
        for (size_t i = 0; i < n; i += 4) {
            a[i] = 1;
        }
        // 保留结果可见性，避免被激进优化影响测试结论。
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_skip4);

/**
 * @brief 步长为 8 的并行写入基准。
 * @param bm Google Benchmark 状态对象。
 */
void BM_skip8(benchmark::State &bm) {
    for (auto _: bm) {
#pragma omp parallel for
        for (size_t i = 0; i < n; i += 8) {
            a[i] = 1;
        }
        // 保留结果可见性，避免被激进优化影响测试结论。
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_skip8);

/**
 * @brief 步长为 16 的并行写入基准。
 * @param bm Google Benchmark 状态对象。
 */
void BM_skip16(benchmark::State &bm) {
    for (auto _: bm) {
#pragma omp parallel for
        for (size_t i = 0; i < n; i += 16) {
            a[i] = 1;
        }
        // 保留结果可见性，避免被激进优化影响测试结论。
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_skip16);

/**
 * @brief 步长为 32 的并行写入基准。
 * @param bm Google Benchmark 状态对象。
 */
void BM_skip32(benchmark::State &bm) {
    for (auto _: bm) {
#pragma omp parallel for
        for (size_t i = 0; i < n; i += 32) {
            a[i] = 1;
        }
        // 保留结果可见性，避免被激进优化影响测试结论。
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_skip32);

/**
 * @brief 步长为 64 的并行写入基准。
 * @param bm Google Benchmark 状态对象。
 */
void BM_skip64(benchmark::State &bm) {
    for (auto _: bm) {
#pragma omp parallel for
        for (size_t i = 0; i < n; i += 64) {
            a[i] = 1;
        }
        // 保留结果可见性，避免被激进优化影响测试结论。
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_skip64);

/**
 * @brief 步长为 128 的并行写入基准。
 * @param bm Google Benchmark 状态对象。
 */
void BM_skip128(benchmark::State &bm) {
    for (auto _: bm) {
#pragma omp parallel for
        for (size_t i = 0; i < n; i += 128) {
            a[i] = 1;
        }
        // 保留结果可见性，避免被激进优化影响测试结论。
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_skip128);

/**
 * @brief Google Benchmark 入口。
 *
 * 自动生成 main 并运行所有注册的基准测试。
 */
BENCHMARK_MAIN();
