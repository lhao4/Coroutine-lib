# 03. 模块划分与调用关系

本节仅基于当前仓库中真实存在的模块说明。

## 模块职责

## coroutine（`fiber`）

- 文件：`include/mycoroutine/fiber.h`、`src/fiber.cpp`
- 职责：
  - 协程对象封装（ID、状态、栈、入口函数）
  - 上下文创建与销毁
  - `resume()/yield()` 切换控制流

## scheduler

- 文件：`include/mycoroutine/scheduler.h`、`src/scheduler.cpp`
- 职责：
  - 管理任务队列
  - 管理 worker 线程
  - 调度执行协程和回调
  - 处理线程亲和任务

## context（在 fiber 内实现）

- 文件：同 `fiber`
- 职责：
  - 使用 `ucontext_t` 保存执行上下文
  - 通过 `swapcontext` 实现协程/调度上下文切换

说明：项目中没有单独 `context` 模块目录，context 能力由 `Fiber` 内部直接承担。

## event loop（`iomanager` + `timer` + `fd_manager` + `hook`）

- `iomanager`
  - 文件：`include/mycoroutine/iomanager.h`、`src/iomanager.cpp`
  - 职责：`epoll` 事件注册、触发、线程唤醒、IO 事件转调度任务
- `timer`
  - 文件：`include/mycoroutine/timer.h`、`src/timer.cpp`
  - 职责：维护超时队列，收集过期回调并交给调度器
- `fd_manager`
  - 文件：`include/mycoroutine/fd_manager.h`、`src/fd_manager.cpp`
  - 职责：维护 fd 元信息（是否 socket、超时、nonblock）
- `hook`
  - 文件：`include/mycoroutine/hook.h`、`src/hook.cpp`
  - 职责：对阻塞系统调用做协程化封装

## utilities

- `thread`
  - 文件：`include/mycoroutine/thread.h`、`src/thread.cpp`
  - 职责：pthread 线程封装、信号量封装
- `utils`
  - 文件：`include/mycoroutine/utils.h`、`src/utils.cpp`
  - 职责：日志时间戳、线程 ID、协程 ID 等工具函数

## 调用关系

主链路：
1. 业务提交任务到 `Scheduler::scheduleLock`
2. `Scheduler::run` 取任务并执行协程/回调
3. 任务若触发 Hook IO，则由 `hook::do_io` 调用 `IOManager::addEvent`
4. 当前协程 `yield`，控制权回到调度器
5. `IOManager::idle` 中 `epoll_wait` 返回后触发 `FdContext::triggerEvent`
6. 事件对应任务被重新调度，协程恢复执行
7. 超时逻辑由 `TimerManager` 提供，与 `IOManager` 在 `idle` 阶段合并处理
