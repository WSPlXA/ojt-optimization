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
 * @brief 对比串行与 OpenMP 并行的函数计算吞吐性能。
 *
 * 测试内容：
 * 1. 串行执行 a[i] = func(a[i])
 * 2. OpenMP 并行执行 a[i] = func(a[i])
 */
constexpr size_t n = 1<<28;

/**
 * @brief 被测试的全局数组，大小约 1 GB。
 *
 * 采用全局分配可避免在基准循环内重复申请/释放内存，减少非目标开销。
 */
std::vector<float> a(n);  // 1GB

/**
 * @brief 用于基准测试的计算函数。
 * @param x 输入值。
 * @return 经过一系列乘加与除法运算后的结果。
 *
 * 函数包含多项算术与除法操作，用于模拟计算密集型负载。
 */
static float func(float x) {
    return x * (x * x + x * 3.14f - 1 / (x + 1)) + 42 / (2.718f - x);
}

/**
 * @brief 串行计算基准：逐元素执行 func 并写回。
 * @param bm Google Benchmark 状态对象。
 */
void BM_serial_func(benchmark::State &bm) {
    for (auto _: bm) {
        for (size_t i = 0; i < n; i++) {
            a[i] = func(a[i]);
        }
        // 防止编译器将对 a 的写入优化掉，保证基准有效性。
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_serial_func);

/**
 * @brief OpenMP 并行计算基准：并行执行 func 并写回。
 * @param bm Google Benchmark 状态对象。
 *
 * 与串行版本对比，可评估多线程对计算密集型负载的加速效果。
 */
void BM_parallel_func(benchmark::State &bm) {
    for (auto _: bm) {
#pragma omp parallel for
        for (size_t i = 0; i < n; i++) {
            a[i] = func(a[i]);
        }
        // 保留结果可见性，避免被激进优化影响测试结论。
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_parallel_func);

/**
 * @brief Google Benchmark 入口。
 *
 * 自动生成 main 并运行所有注册的基准测试。
 */
BENCHMARK_MAIN();
