# mycoroutine

一个小型 C++17 协程运行时库，核心基于 `ucontext`（有栈协程）、多线程调度器、`epoll` 事件循环、定时器和系统调用 Hook。

本项目不是“全功能生产级框架”，而是一个可运行、可阅读、可扩展的系统编程实践工程，强调协程运行时核心链路的完整性。

## 项目简介

`mycoroutine` 通过如下组件构建协程运行时：

- 协程对象：`Fiber`
- 调度器：`Scheduler`
- IO 事件循环：`IOManager`
- 定时器：`TimerManager`
- 阻塞系统调用协程化：`hook`
- 文件描述符上下文：`FdManager`

## 项目目标

- 用较小代码规模打通协程运行时关键闭环
- 展示协程在系统编程中的实际落地方式
- 为学习者提供可直接阅读和实验的实现样本

## 协程库要解决的问题

- 避免“阻塞 IO + 多线程”模型的高上下文切换成本
- 将阻塞系统调用转换为“事件等待 + 协程挂起/恢复”
- 统一管理普通任务、IO 事件任务、定时任务

## 项目特点

- `ucontext` 有栈协程（stackful）
- 任务既可提交 `Fiber`，也可提交普通回调
- 调度器支持多线程与线程亲和任务
- `epoll + eventfd` 事件驱动 + 线程唤醒
- 定时器支持一次性、循环、条件触发
- Hook `sleep/read/write/connect/accept/...` 等系统调用

## 技术要点概览

- 协程状态机：`READY / RUNNING / TERM`
- 协程切换：`swapcontext`
- 调度核心：`Scheduler::run()` 主循环
- IO 核心：`IOManager::idle()` 中 `epoll_wait`
- Hook 核心：`do_io()` 模板封装 EAGAIN/超时/重试

## 目录结构说明

```text
.
├── CMakeLists.txt
├── CMakePresets.json
├── include/mycoroutine/
│   ├── fiber.h
│   ├── scheduler.h
│   ├── iomanager.h
│   ├── timer.h
│   ├── hook.h
│   ├── fd_manager.h
│   ├── thread.h
│   └── utils.h
├── src/
│   ├── fiber.cpp
│   ├── scheduler.cpp
│   ├── iomanager.cpp
│   ├── timer.cpp
│   ├── hook.cpp
│   ├── fd_manager.cpp
│   ├── thread.cpp
│   └── utils.cpp
├── examples/
│   └── coroutine_http_server.cpp
├── tests/
│   ├── bench/main.cpp
│   ├── smoke/main.cpp
│   ├── epoll/main.cpp
│   └── libevent/main.cpp
└── docs/
    ├── ARCHITECTURE.md
    ├── DESIGN.md
    ├── MODULES.md
    ├── WORKFLOW.md
    ├── USAGE.md
    ├── IMPROVEMENTS.md
    ├── RESUME.md
    └── TEST_REPORT.md
```

## 快速开始

### 1) 配置 + 编译 + 测试（推荐）

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

### 2) 运行示例程序

```bash
./build/debug/examples/coroutine_http_server
```

### 3) 运行性能基准（Release）

```bash
cmake --preset release
cmake --build --preset release -j4
./build/release/tests/mycoroutine_benchmark
```

### 4) 查看最新测试与性能报告

`docs/TEST_REPORT.md` 已同步到最新测试结果（2026-03-06），包含：
- Debug/Release 构建与 `ctest` 结果
- `libevent` 网络 demo 构建与联通验证
- 协程微基准与 HTTP 并发对比数据

## 编译与运行方式

- 推荐构建方式：`CMakePresets.json`
- 默认 `debug` 预设：
  - `BUILD_TESTING=ON`
  - `MYCOROUTINE_BUILD_EXAMPLES=ON`
  - `MYCOROUTINE_BUILD_NETWORK_DEMOS=OFF`
  - `MYCOROUTINE_BUILD_BENCHMARKS=ON`（默认值）

如需启用网络 demo（`tests/epoll`、`tests/libevent`）：

```bash
cmake -S . -B build/debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DMYCOROUTINE_BUILD_EXAMPLES=ON \
  -DMYCOROUTINE_BUILD_NETWORK_DEMOS=ON \
  -DMYCOROUTINE_BUILD_BENCHMARKS=ON
cmake --build build/debug -j
```

## 最新测试快照（2026-03-06）

- 自动化测试：`debug/release` 下 `ctest` 均为 `1/1 passed`
- 微基准（Release）：
  - `fiber context switch` 约 `2.0M ops/s`（约 `0.51us`/次）
  - `scheduler callbacks (workers=1)` 在完整轮次均值约 `11.3k ops/s`
  - 补充复测中 `workers=1` 波动较大（约 `1.9k~8.1k ops/s`）
- HTTP 并发（1000 req, c=32）：
  - `coroutine_http_server` 约 `431 req/s`
  - `epoll_http_demo/libevent_http_demo` 约 `459~461 req/s`

详细命令、原始结果和对比分析见 [docs/TEST_REPORT.md](docs/TEST_REPORT.md)。

## 最小使用示例

```cpp
#include <iostream>
#include <mycoroutine/iomanager.h>
#include <mycoroutine/fiber.h>

using namespace mycoroutine;

void taskA() {
    std::cout << "taskA step1\n";
    Fiber::GetThis()->yield();
    std::cout << "taskA step2\n";
}

int main() {
    IOManager iom(1, true, "demo");
    iom.scheduleLock(taskA);
    iom.scheduleLock([] { std::cout << "taskB run once\n"; });
    return 0;
}
```

## 文档索引

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)：总体架构、依赖关系、运行时组件关系
- [docs/DESIGN.md](docs/DESIGN.md)：设计思想、协程模型、调度与切换机制
- [docs/MODULES.md](docs/MODULES.md)：模块职责、接口、调用关系、局限
- [docs/WORKFLOW.md](docs/WORKFLOW.md)：启动到退出的完整运行流程与调用链
- [docs/USAGE.md](docs/USAGE.md)：编译、运行、API、排障
- [docs/IMPROVEMENTS.md](docs/IMPROVEMENTS.md)：当前不足与优化优先级
- [docs/KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md)：`v1.0.0` 已知限制与发布边界
- [docs/RESUME.md](docs/RESUME.md)：简历与面试表达模板
- [docs/TEST_REPORT.md](docs/TEST_REPORT.md)：功能验证 + 性能基准 + 对比分析

## 项目状态说明

- 状态：可运行、可学习、可继续演进
- 最新验证：已完成系统测试与性能测试（见 `docs/TEST_REPORT.md`）
- 适合：学习协程运行时、系统编程、网络事件驱动
- 不适合：直接作为生产环境高可靠基础库
