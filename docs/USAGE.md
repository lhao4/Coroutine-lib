# USAGE

## 文档目的

本文档给出项目的编译、运行和 API 使用方法，包含最小可运行示例、常见用法和排障建议。

---

## 1. 编译环境要求

建议环境：

- OS：Linux（依赖 `ucontext` / `epoll` / `eventfd` / `dlsym`）
- 编译器：`g++` 或 `clang++`（支持 C++17）
- CMake：>= 3.20
- 构建工具：Ninja（推荐）

---

## 2. 编译依赖

项目当前依赖：

- `pthread`（线程）
- `dl`（`dlsym`，Hook 依赖）
- Linux 系统调用头（`epoll`, `eventfd`, `ucontext`）

可选依赖（仅当开启网络 demo）：

- `libevent`

---

## 3. 编译命令

## 3.1 使用 Presets（推荐）

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## 3.2 手动方式

```bash
cmake -S . -B build/debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DMYCOROUTINE_BUILD_EXAMPLES=ON \
  -DMYCOROUTINE_BUILD_NETWORK_DEMOS=OFF

cmake --build build/debug -j
ctest --test-dir build/debug --output-on-failure
```

## 3.3 启用可选网络 demo

```bash
cmake -S . -B build/debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DMYCOROUTINE_BUILD_EXAMPLES=ON \
  -DMYCOROUTINE_BUILD_NETWORK_DEMOS=ON
cmake --build build/debug -j
```

---

## 4. 运行方式

### 4.1 运行 smoke test

```bash
./build/debug/tests/mycoroutine_smoke_test
```

### 4.2 运行示例服务器

```bash
./build/debug/examples/coroutine_http_server
```

---

## 5. 最小可运行示例

下面示例展示：
- 创建 `IOManager`
- 提交两个任务
- 协程主动 `yield` 并恢复

```cpp
#include <iostream>
#include <mycoroutine/fiber.h>
#include <mycoroutine/iomanager.h>

using namespace mycoroutine;

void taskA() {
    std::cout << "taskA enter\n";
    Fiber::GetThis()->yield();
    std::cout << "taskA resume\n";
}

int main() {
    IOManager iom(1, true, "quick_start");

    iom.scheduleLock(taskA);
    iom.scheduleLock([] {
        std::cout << "taskB run once\n";
    });

    // IOManager 析构时会 stop + join
    return 0;
}
```

---

## 6. 示例代码解释

- `IOManager iom(...)`：创建调度 + IO + 定时器一体化运行时
- `scheduleLock(taskA)`：提交协程任务
- `Fiber::yield()`：当前协程主动让出执行权
- 第二次调度到该协程时，从 `yield` 之后继续执行

---

## 7. API 使用说明（按模块）

## 7.1 Fiber API

- `Fiber(cb, stacksize=0, run_in_scheduler=true)`
  - 用途：创建子协程
  - 时机：你需要将一段函数作为协程实体执行时
- `resume()`
  - 用途：恢复协程执行
  - 时机：一般由调度器调用
- `yield()`
  - 用途：协程让出执行权
  - 时机：协程内部主动挂起或执行结束
- `reset(cb)`
  - 用途：复用 `TERM` 协程

## 7.2 Scheduler API

- `scheduleLock(fiber_or_cb, thread=-1)`
  - 用途：线程安全提交任务
  - 时机：业务线程或运行时内部都可调用
- `start()` / `stop()`
  - 用途：控制调度器生命周期

## 7.3 IOManager API

- `addEvent(fd, READ/WRITE, cb)`
  - 用途：注册 IO 事件
  - 时机：你希望 fd 就绪时执行回调/恢复协程
- `delEvent(fd,event)`
  - 用途：删除已注册事件，不触发回调
- `cancelEvent(fd,event)`
  - 用途：取消事件并触发对应执行路径
- `cancelAll(fd)`
  - 用途：取消 fd 上全部事件

## 7.4 TimerManager API

- `addTimer(ms, cb, recurring=false)`
  - 用途：添加定时任务
- `addConditionTimer(ms, cb, weak_cond, recurring=false)`
  - 用途：条件定时任务，条件失效则回调不执行

## 7.5 Hook API

- `set_hook_enable(bool)` / `is_hook_enable()`
  - 用途：控制当前线程是否启用 Hook
  - 当前实现中：调度线程在 `Scheduler::run()` 入口自动启用

---

## 8. 常见使用方式

## 8.1 只用协程调度，不用 Hook IO

- 手动将 fd 设为非阻塞
- 使用 `IOManager::addEvent` 注册回调

## 8.2 使用 Hook IO 协程化

- 在调度线程中运行（默认已启用 Hook）
- 调用 `read/write/connect/...` 时遇 `EAGAIN` 会自动挂起协程

## 8.3 定时任务 + IO 混合

- 用 `addTimer` 驱动周期任务
- 用 `addEvent` 驱动 IO 任务
- 两者统一由调度器执行

---

## 9. 注意事项

1. 该项目依赖 Linux，非 Linux 平台不保证可用。
2. Hook 开关是线程局部的，不是全局状态。
3. `Fiber::yield()` 必须在协程上下文内调用。
4. 默认调度策略不是实时优先级调度，不适合硬实时场景。
5. `tests/epoll` 与 `tests/libevent` 是 demo，不属于默认自动测试集。

---

## 10. 常见错误与排查建议

## 问题 1：程序卡住或任务不执行

排查：
- 确认任务是否真的提交到 `scheduleLock`
- 确认调度器是否已 `start`
- 打开必要日志，观察 `run()` 是否在循环

## 问题 2：Hook 行为不符合预期

排查：
- 确认代码运行线程是否为调度线程
- 检查 `is_hook_enable()` 返回值
- 确认 fd 是否是 socket，且未被用户强制 nonblock 逻辑绕过

## 问题 3：连接超时行为异常

排查：
- 检查 `SO_RCVTIMEO/SO_SNDTIMEO` 是否正确设置
- 确认 `addConditionTimer` 与 `cancelEvent` 路径是否被触发

## 问题 4：事件未触发

排查：
- 确认 `addEvent` 返回值
- 确认 fd 仍有效且未被关闭
- 确认 `epoll_ctl` 是否成功

---

## 11. 推荐阅读顺序

1. `README.md`
2. `docs/ARCHITECTURE.md`
3. `docs/DESIGN.md`
4. `docs/WORKFLOW.md`
5. `docs/MODULES.md`
6. `docs/IMPROVEMENTS.md`

