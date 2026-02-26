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
 * @brief 使用 Google Benchmark 对串行/并行数组填充与正弦计算进行性能测试。
 *
 * 测试内容：
 * 1. 串行填充数组（将每个元素写为 1）
 * 2. OpenMP 并行填充数组
 * 3. 串行计算 sin(i)
 * 4. OpenMP 并行计算 sin(i)
 */
constexpr size_t n = 1<<26;

/**
 * @brief 被测试的全局数据缓冲区。
 *
 * 元素个数为 n，每个元素为 float（4 字节），总占用约 256 MB。
 * 采用全局变量可以避免在每次基准迭代中重复分配内存，减少额外干扰。
 */
std::vector<float> a(n);  // 256MB

/**
 * @brief 串行写入基准：逐元素将数组写为 1。
 * @param bm Google Benchmark 传入的状态对象。
 *
 * 该测试主要反映单线程顺序写内存的吞吐能力。
 */
void BM_fill(benchmark::State &bm) {
    for (auto _: bm) {
        for (size_t i = 0; i < n; i++) {
            a[i] = 1;
        }
    }
}
BENCHMARK(BM_fill);

/**
 * @brief 并行写入基准：使用 OpenMP 将数组元素并行写为 1。
 * @param bm Google Benchmark 传入的状态对象。
 *
 * 与 BM_fill 对比，可观察多线程对纯内存写场景的加速效果与带宽上限影响。
 */
void BM_parallel_fill(benchmark::State &bm) {
    for (auto _: bm) {
#pragma omp parallel for
        for (size_t i = 0; i < n; i++) {
            a[i] = 1;
        }
    }
}
BENCHMARK(BM_parallel_fill);

/**
 * @brief 串行计算基准：逐元素执行 a[i] = sin(i)。
 * @param bm Google Benchmark 传入的状态对象。
 *
 * 该测试包含数学函数计算与写内存操作，用于评估单线程计算密集 + 存储混合场景。
 */
void BM_sine(benchmark::State &bm) {
    for (auto _: bm) {
        for (size_t i = 0; i < n; i++) {
            a[i] = std::sin(i);
        }
    }
}
BENCHMARK(BM_sine);

/**
 * @brief 并行计算基准：使用 OpenMP 并行执行 a[i] = sin(i)。
 * @param bm Google Benchmark 传入的状态对象。
 *
 * 与 BM_sine 对比，可评估多线程对计算与写入混合任务的整体收益。
 */
void BM_parallel_sine(benchmark::State &bm) {
    for (auto _: bm) {
#pragma omp parallel for
        for (size_t i = 0; i < n; i++) {
            a[i] = std::sin(i);
        }
    }
}
BENCHMARK(BM_parallel_sine);

/**
 * @brief Google Benchmark 程序入口宏。
 *
 * 宏会生成 main 函数并运行所有通过 BENCHMARK(...) 注册的测试项。
 */
BENCHMARK_MAIN();
