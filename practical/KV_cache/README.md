# KV Cache（fd 思想）实验说明

## 1. 项目目标
在 `practical/KV_cache` 下实现一个“fd 驱动”的高性能 KV，并与传统容器做基准对比。

本项目的 fd 思想来自内核句柄设计：把访问凭证编码为一段定长 token，并在访问时做快速校验。

## 2. fd 句柄设计
句柄是 64-bit，无符号整数，位布局如下：

`[ type:8 | generation:24 | position:32 ]`

- `type`：对象类型（例如不同资源类别）
- `generation`：代际号，防止旧句柄（悬挂 fd）误访问
- `position`：槽位下标，支持 O(1) 直达存储

对应实现见：
- `assets/fd_token.h`

## 3. KV 实现要点
核心容器包含两版：

- `kvcache::FdKVCache<Key, Value>`：单机单线程热点访问版本
- `kvcache::ShardedFdKVCache<Key, Value>`：并发优化版本（分片 + cache line 对齐）

设计要点：
- `Slot` 直接存储 `Key/Value`，不再使用 `std::optional`
- 索引层改为开放寻址平铺哈希表（`FlatIndexMap`），避免节点式 `unordered_map` 指针追逐
- 主存储是 `slots_`（顺序数组），`position` 直接索引槽位
- `Erase` 后槽位回收到 `free_positions_`，并递增 `generation`
- `Get(handle)` 会校验 `type/generation/position`，校验失败返回 `nullptr`
- `ShardedFdKVCache` 采用分片结构，每个分片独立锁、独立 freelist
- 分片为连续数组（非指针数组），并使用 `alignas(64)` 隔离分片元数据缓存行
- 每个分片在初始化阶段固定容量，运行期不做锁内扩容，避免延迟毛刺
- 当容量达到上限时，`Insert` 返回空句柄（`FdToken::kNull`），而不是隐式扩容

对应实现见：
- `assets/fd_kv_cache.h`

## 4. Benchmark 对比项
基准程序见：
- `main.cpp`

对比对象：
- `FdKVCache`
- `std::unordered_map`
- `std::map`

基准组：
- `Read`：高频读取
- `Update`：高频更新
- `InsertErase`：插入后删除（完整生命周期）
- `MT_Read`：多线程读取对比（`BM_MT_*_Read`）
- `MT_Update`：多线程更新对比（`BM_MT_*_Update`）

## 5. 构建与运行
依赖：
- C++17 编译器
- CMake 3.16+
- Google Benchmark（`benchmark::benchmark`）

构建命令：

```bash
cmake -S practical/KV_cache -B practical/KV_cache/build
cmake --build practical/KV_cache/build -j
```

运行全部基准：

```bash
./practical/KV_cache/build/kv_cache_bench
```

仅运行某组：

```bash
./practical/KV_cache/build/kv_cache_bench --benchmark_filter='.*InsertErase.*'
./practical/KV_cache/build/kv_cache_bench --benchmark_filter='(FdKV|UnorderedMap|Map)_(Read|Update)'
./practical/KV_cache/build/kv_cache_bench --benchmark_filter='BM_MT_(FdKV|UnorderedMap|Map)_(Read|Update)'
```

## 6. 结果解读（当前版本）
从已跑结果看，趋势通常是：

- `Read/Update`：`FdKVCache` 明显快于 `unordered_map`，`map` 最慢
- `InsertErase`：`FdKVCache` 与 `unordered_map` 接近，`map` 仍明显落后
- `MT_Read/MT_Update`：`ShardedFdKVCache` 在线程增多时退化更小，明显优于加全局锁的 `unordered_map/map`

原因：
- `FdKVCache` 在热点路径可绕开哈希/树查找，直接按 `position` 访问
- 插删阶段仍需维护 key 映射，收益相对读写路径会收窄
- 并发版通过分片将热点拆散，减少锁竞争和缓存行抖动

## 7. CMake 特性
`CMakeLists.txt` 已包含以下工程化配置：

- 默认 `Release`（未显式指定构建类型时）
- GNU/Clang 下启用 `-O3 -march=native -Wall -Wextra -Wpedantic`
- 可选 LTO/IPO（`KV_CACHE_ENABLE_LTO=ON`）
- 头文件目录自动包含 `assets`

## 8. 目录结构
```text
practical/KV_cache
├── CMakeLists.txt
├── main.cpp
└── assets
    ├── fd_kv_cache.h
    └── fd_token.h
```
