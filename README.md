# mycoroutine

一个小型 C++17 有栈协程库，基于 `ucontext + 多线程调度 + epoll + timer + hook`。
当前版本已完成四项核心升级：共享栈、协程嵌套、复杂调度策略、协程池。

## 项目简介
`mycoroutine` 面向系统编程学习与轻量运行时实践，提供从协程创建、切换、调度到 IO 挂起恢复的完整链路。

## 项目目标
- 提供可运行、可扩展的协程运行时基础库。
- 支持网络 IO 与定时器任务的协程化调度。
- 在小规模代码体量下完成可验证的关键优化能力。

## 项目特点
- 有栈协程（stackful），支持显式 `yield/resume`。
- 调度器支持 `FIFO / PRIORITY / MLFQ / EDF / HYBRID`。
- 共享栈模式支持按线程配置槽位并做栈快照恢复。
- 协程嵌套支持 `call/back` 父子协程切换。
- 协程池复用 `TERM` 协程对象，降低频繁创建销毁开销。

## 目录结构概览

```text
.
├── CMakeLists.txt
├── include/mycoroutine/
│   ├── coroutine_pool.h
│   ├── fiber.h
│   ├── scheduler.h
│   ├── iomanager.h
│   ├── timer.h
│   ├── hook.h
│   ├── fd_manager.h
│   ├── thread.h
│   └── utils.h
├── src/
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
│   └── pool/main.cpp
└── docs/
    ├── DESIGN.md
    ├── MODULES.md
    ├── USAGE.md
    ├── TEST.md
    ├── IMPROVEMENTS.md
    └── UPGRADE_LOG.md
```

## 快速编译和运行方式

### 1) Debug 构建 + 测试
```bash
cmake --preset debug
cmake --build --preset debug -j
ctest --preset debug --output-on-failure
```

### 2) 运行示例
```bash
./build/debug/examples/coroutine_http_server
```

## 基本使用示例

```cpp
#include <mycoroutine/scheduler.h>

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

    sc.stop();
    return 0;
}
```

## 文档导航
- `docs/DESIGN.md`：核心设计与优化实现。
- `docs/MODULES.md`：模块职责、接口和调用关系。
- `docs/USAGE.md`：编译、运行、API 示例。
- `docs/TEST.md`：测试用例与验证方法。
- `docs/IMPROVEMENTS.md`：优化结果与使用注意事项。
- `docs/UPGRADE_LOG.md`：本轮升级变更记录。
