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
 * @brief 对比串行与 OpenMP 并行的数组自增性能。
 *
 * 测试内容：
 * 1. 串行执行 a[i] = a[i] + 1
 * 2. OpenMP 并行执行 a[i] = a[i] + 1
 */
constexpr size_t n = 1<<28;

/**
 * @brief 被测试的全局数组，大小约 1 GB。
 *
 * 采用全局分配可避免在基准循环内重复申请/释放内存，减少非目标开销。
 */
std::vector<float> a(n);  // 1GB

/**
 * @brief 串行自增基准。
 * @param bm Google Benchmark 状态对象。
 *
 * 单线程逐元素读取并写回，主要观察顺序访存与计算的综合耗时。
 */
void BM_serial_add(benchmark::State &bm) {
    for (auto _: bm) {
        for (size_t i = 0; i < n; i++) {
            a[i] = a[i] + 1;
        }
        // 防止编译器将对 a 的写入优化掉，保证基准有效性。
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_serial_add);

/**
 * @brief OpenMP 并行自增基准。
 * @param bm Google Benchmark 状态对象。
 *
 * 多线程并行处理不同下标区间，用于与串行版本比较并行加速效果。
 */
void BM_parallel_add(benchmark::State &bm) {
    for (auto _: bm) {
#pragma omp parallel for
        for (size_t i = 0; i < n; i++) {
            a[i] = a[i] + 1;
        }
        // 保留结果可见性，避免被激进优化影响测试结论。
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_parallel_add);

/**
 * @brief Google Benchmark 入口。
 *
 * 自动生成 main 并运行所有注册的基准测试。
 */
BENCHMARK_MAIN();
