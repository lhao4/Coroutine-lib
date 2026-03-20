# mycoroutine

一个小型 C++17 有栈协程库，基于 `汇编上下文切换 + 多线程调度 + epoll + timer + hook`。
当前版本已完成七项核心能力：共享栈、协程嵌套、复杂调度策略、协程池、协程同步原语、任务队列优化、汇编切换。

## 项目简介
`mycoroutine` 提供从协程创建、切换、调度到 IO 挂起恢复的完整链路，代码规模可控，适合系统编程学习与运行时实现实践。

## 项目目标
- 提供可运行、可扩展的协程运行时基础库。
- 支持网络 IO 与定时器任务协程化调度。
- 在小规模代码体量下落地可验证的运行时优化能力。

## 项目特点
- 有栈协程（stackful），支持显式 `yield/resume`。
- 汇编上下文切换（x86_64 / aarch64），替代 `ucontext`。
- 协程嵌套支持 `call/back`，`call()` 使用错误码返回。
- 调度器支持 `FIFO / PRIORITY / MLFQ / EDF / HYBRID`。
- 共享栈支持按线程配置槽位并执行快照保存/恢复。
- 协程池支持 `TERM` 协程复用，降低频繁创建销毁开销。
- 协程同步原语：`CoMutex`、`Channel<T>`、`WaitGroup`。

## 目录结构概览

```text
.
├── CMakeLists.txt
├── include/mycoroutine/
│   ├── coroutine_pool.h
│   ├── context.h
│   ├── fiber.h
│   ├── scheduler.h
│   ├── iomanager.h
│   ├── sync.h
│   ├── timer.h
│   ├── hook.h
│   ├── fd_manager.h
│   ├── thread.h
│   └── utils.h
├── src/
│   ├── context_switch_x86_64.S
│   ├── context_switch_aarch64.S
│   ├── coroutine_pool.cpp
│   ├── fiber.cpp
│   ├── scheduler.cpp
│   ├── iomanager.cpp
│   ├── timer.cpp
│   ├── hook.cpp
│   ├── fd_manager.cpp
│   ├── thread.cpp
│   └── utils.cpp
├── tests/
│   ├── smoke/main.cpp
│   ├── shared_stack/main.cpp
│   ├── nested/main.cpp
│   ├── policy/main.cpp
│   ├── pool/main.cpp
│   ├── sync/main.cpp
│   ├── hook/main.cpp
│   ├── stress/main.cpp
│   └── bench/main.cpp
└── docs/
    ├── 架构设计.md
    ├── 核心流程.md
    ├── 高级特性设计.md
    ├── 模块职责.md
    ├── 使用指南.md
    ├── 测试用例.md
    ├── 测试报告.md
    ├── 问题排查与修复记录.md
    └── 面试手册.md
```

## 性能亮点（Release, AMD Ryzen 7 6800H）

| 指标 | 数值 | 说明 |
|------|------|------|
| Fiber 上下文切换 | **17 ns/op** | 汇编直接切换，无系统调用 |
| Scheduler 单线程吞吐 | **527 ns/op** | deque + move 优化 |
| Scheduler 4 线程吞吐 | **42,297 ns/op** | 多线程调度 |
| 对比 std::thread 创建销毁 | 111,343 ns/op | 协程切换快 6500x |

> 详细对比数据见 `docs/测试报告.md`。

## 快速编译和运行

### 1) Debug 构建 + 测试
```bash
cmake --preset debug
cmake --build --preset debug -j
ctest --preset debug --output-on-failure
```

### 2) Release 构建 + Benchmark
```bash
cmake --preset release
cmake --build --preset release -j
./build/release/tests/mycoroutine_benchmark
```

### 3) ASan / TSan 检测
```bash
cmake --preset asan && cmake --build --preset asan -j && ctest --preset asan
cmake --preset tsan && cmake --build --preset tsan -j && ctest --preset tsan
```

> TSan 在 WSL2 高 ASLR 内核下需先执行 `sudo sysctl vm.mmap_rnd_bits=28`。

### 4) 运行示例
```bash
./build/debug/examples/coroutine_http_server
```

## 基本使用示例

```cpp
#include <mycoroutine/scheduler.h>
#include <mycoroutine/sync.h>

int main() {
    mycoroutine::Scheduler sc(1, true, "demo");
    sc.setPolicy(mycoroutine::SchedulePolicy::PRIORITY);

    mycoroutine::ScheduleOptions high;
    high.priority = 10;

    sc.scheduleEx([] {
        // 高优先级任务
    }, high);

    sc.scheduleShared([] {
        // 共享栈协程任务
        mycoroutine::Fiber::GetThis()->yield();
    }, 64 * 1024);

    mycoroutine::Channel<int> ch(1);
    mycoroutine::WaitGroup wg;
    wg.add(1);
    sc.scheduleLock([&]() {
        ch.send(42);
        wg.done();
    });
    sc.scheduleLock([&]() {
        int value = 0;
        if (ch.recv(value)) {
            // value == 42
        }
        wg.wait();
    });

    sc.stop();
    return 0;
}
```

## 文档导航
- `docs/架构设计.md`：运行时架构与模块关系。
- `docs/核心流程.md`：启动→调度→切换→IO→退出的完整流程。
- `docs/高级特性设计.md`：七项核心能力的实现设计 + 使用注意 + 已知限制。
- `docs/模块职责.md`：模块职责、接口、调用关系。
- `docs/使用指南.md`：构建、运行与 API 示例。
- `docs/测试用例.md`：测试用例、验证方法与最新结果。
- `docs/测试报告.md`：Debug / ASan / TSan 全量测试结果与性能基准。
- `docs/问题排查与修复记录.md`：15 个关键问题的决策记录。
- `docs/面试手册.md`：项目介绍、Q&A、简历落地与回答策略。
- `docs/优化路线.md`：已完成优化与剩余方向。
