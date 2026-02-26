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
 * @brief 不同数组规模下的顺序写入性能测试，用于观察缓存层级影响。
 */
// L1: 32KB
// L2: 256KB
// L3: 12MB

/**
 * @brief 最大测试规模（约 1GB 的 float 数组）。
 *
 * 采用全局数组避免在基准循环内反复分配内存，减少非目标开销。
 */
constexpr size_t n = 1<<28;

/**
 * @brief 全局数据缓冲区，容量为 n 个 float。
 */
std::vector<float> a(n);

/**
 * @brief 填充 1GB 数据：完全覆盖数组。
 * @param bm Google Benchmark 状态对象。
 */
void BM_fill1GB(benchmark::State &bm) {
    for (auto _: bm) {
        for (size_t i = 0; i < (1<<28); i++) {
            a[i] = 1;
        }
    }
}
BENCHMARK(BM_fill1GB);

/**
 * @brief 填充 128MB 数据：覆盖部分数组。
 * @param bm Google Benchmark 状态对象。
 */
void BM_fill128MB(benchmark::State &bm) {
    for (auto _: bm) {
        for (size_t i = 0; i < (1<<25); i++) {
            a[i] = 1;
        }
    }
}
BENCHMARK(BM_fill128MB);

/**
 * @brief 填充 16MB 数据：接近或略大于 L3 的规模。
 * @param bm Google Benchmark 状态对象。
 */
void BM_fill16MB(benchmark::State &bm) {
    for (auto _: bm) {
        for (size_t i = 0; i < (1<<22); i++) {
            a[i] = 1;
        }
    }
}
BENCHMARK(BM_fill16MB);

/**
 * @brief 填充 1MB 数据：显著小于 L3，但大于 L2 的规模。
 * @param bm Google Benchmark 状态对象。
 */
void BM_fill1MB(benchmark::State &bm) {
    for (auto _: bm) {
        for (size_t i = 0; i < (1<<18); i++) {
            a[i] = 1;
        }
    }
}
BENCHMARK(BM_fill1MB);

/**
 * @brief 填充 128KB 数据：接近或小于 L2 的规模。
 * @param bm Google Benchmark 状态对象。
 */
void BM_fill128KB(benchmark::State &bm) {
    for (auto _: bm) {
        for (size_t i = 0; i < (1<<15); i++) {
            a[i] = 1;
        }
    }
}
BENCHMARK(BM_fill128KB);

/**
 * @brief 填充 16KB 数据：接近或小于 L1 的规模。
 * @param bm Google Benchmark 状态对象。
 */
void BM_fill16KB(benchmark::State &bm) {
    for (auto _: bm) {
        for (size_t i = 0; i < (1<<12); i++) {
            a[i] = 1;
        }
    }
}
BENCHMARK(BM_fill16KB);

/**
 * @brief Google Benchmark 入口。
 *
 * 自动生成 main 并运行所有注册的基准测试。
 */
BENCHMARK_MAIN();
