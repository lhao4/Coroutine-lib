# mycoroutine 问题梳理与修复计划（2026-03-06）

## 1. 目标

在不引入复杂新特性的前提下，修复当前项目中影响“功能可用性与稳定性”的关键问题，确保小项目的核心能力可正确运行：
- 协程调度
- IO 事件注册/触发
- 定时器触发
- Hook IO 协程化

## 2. 已发现问题清单

## P0（必须优先修复）

1. `IOManager` 的 `bool` 返回接口错误使用 `return -1`，失败会被当作成功。
- 位置：`src/iomanager.cpp`（`delEvent/cancelEvent/cancelAll`）

2. `Timer::Comparator` 只比较 `m_next`，同一触发时间的多个定时器会发生键冲突导致丢失。
- 位置：`src/timer.cpp`

3. `addEvent` 可能记录空 `scheduler`，事件触发时会空指针解引用。
- 位置：`src/iomanager.cpp`

4. Hook IO 模板 `do_io` 假设一定存在 `IOManager`，在非 IOManager 线程启用 Hook 会崩溃。
- 位置：`src/hook.cpp`

## P1（应修复）

5. `TimerManager::getNextTimer` 在共享锁下写 `m_tickled`，存在并发数据竞争。
- 位置：`src/timer.cpp`

6. `connect_with_timeout` 在 `addEvent` 失败后仍继续执行后续流程，错误语义不清。
- 位置：`src/hook.cpp`

7. `Fiber::MainFunc` 无异常保护，任务抛异常将导致进程终止。
- 位置：`src/fiber.cpp`

8. `FdCtx::m_isClosed` 从未设置为 `true`，该状态字段无效。
- 位置：`include/mycoroutine/fd_manager.h`, `src/fd_manager.cpp`

9. `fcntl` hook 中 `f_owner_exlock` 类型名可移植性有问题。
- 位置：`src/hook.cpp`

## P2（清理项）

10. 示例程序中存在未使用函数/头文件。
- 位置：`examples/coroutine_http_server.cpp`

11. 调试输出默认开启，影响运行时噪声。
- 位置：`src/scheduler.cpp`, `src/iomanager.cpp`

## 3. 本轮修复范围

本轮包含：P0 + P1 + P2（轻量清理），不做大规模架构重写。

不包含：
- 新调度策略（work stealing 等）
- 大规模 API 重构
- 全量测试体系重建

## 4. 修复步骤

1. 修复 `IOManager` 事件接口返回值语义与空指针防护。
2. 修复定时器比较器与 `m_tickled` 并发访问问题。
3. 修复 Hook 关键路径空指针与错误分支处理，修正 `fcntl` 类型。
4. 为协程执行入口添加异常保护。
5. 补齐 `FdCtx` 关闭状态，清理示例无用代码与过量调试输出。
6. 编译并运行测试验证。

## 5. 验收标准

- `cmake --build --preset debug` 成功。
- `ctest --preset debug` 成功（至少 smoke test 通过）。
- 关键修复点对应代码路径可编译、无明显回归。
