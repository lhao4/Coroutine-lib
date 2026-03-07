# mycoroutine

一个小型 C++17 有栈协程库，基于 `ucontext + 多线程调度 + epoll + timer + hook`。
当前版本已完成四项核心升级：共享栈、协程嵌套、复杂调度策略、协程池。

## 项目简介
`mycoroutine` 提供从协程创建、切换、调度到 IO 挂起恢复的完整链路，代码规模可控，适合系统编程学习与运行时实现实践。

## 项目目标
- 提供可运行、可扩展的协程运行时基础库。
- 支持网络 IO 与定时器任务协程化调度。
- 在小规模代码体量下落地可验证的运行时优化能力。

## 项目特点
- 有栈协程（stackful），支持显式 `yield/resume`。
- 协程嵌套支持 `call/back`，`call()` 使用错误码返回。
- 调度器支持 `FIFO / PRIORITY / MLFQ / EDF / HYBRID`。
- 共享栈支持按线程配置槽位并执行快照保存/恢复。
- 协程池支持 `TERM` 协程复用，降低频繁创建销毁开销。

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
    ├── 架构设计说明.md
    ├── 核心设计与优化方案.md
    ├── 模块职责说明.md
    ├── 编译运行与使用说明.md
    ├── 测试用例与验证说明.md
    ├── 测试报告.md
    ├── 优化总结与注意事项.md
    ├── 已知限制与边界.md
    └── 升级记录.md
```

## 快速编译和运行

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
- `docs/架构设计说明.md`：运行时架构与模块关系。
- `docs/核心设计与优化方案.md`：四项升级的实现设计。
- `docs/模块职责说明.md`：模块职责、接口、调用关系。
- `docs/编译运行与使用说明.md`：构建、运行与 API 示例。
- `docs/测试用例与验证说明.md`：测试用例与验证方法。
- `docs/测试报告.md`：最近一轮测试结论。
- `docs/优化总结与注意事项.md`：优化结果与运行代价。
- `docs/已知限制与边界.md`：当前版本边界。
- `docs/升级记录.md`：升级变更记录。
