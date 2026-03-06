# 02. 整体设计说明

## 协程实现方式

项目采用 **有栈协程（stackful coroutine）**，底层使用 `ucontext`：
- `getcontext()` 初始化上下文
- `makecontext()` 绑定协程入口 `Fiber::MainFunc`
- `swapcontext()` 在协程与调度上下文之间切换

对应实现：`include/mycoroutine/fiber.h`、`src/fiber.cpp`。

该方案优点是实现直观、可在任意调用层级 `yield`；代价是依赖 `ucontext`，可移植性弱于纯语言级协程。

## 调度器设计

### 结构

- `Scheduler`：负责任务队列和线程池调度
- `IOManager`：继承 `Scheduler` + `TimerManager`，在 `idle()` 中执行 `epoll_wait`

### 任务模型

任务实体是 `ScheduleTask`：
- 可持有 `Fiber::ptr`
- 或持有 `std::function<void()>`
- 可选线程亲和字段 `thread`

`Scheduler::run()` 在循环中取任务并执行；无任务时进入 `idle()`。

## 协程生命周期

`Fiber::State` 包含三个状态：
- `READY`
- `RUNNING`
- `TERM`

典型路径：
1. 创建后为 `READY`
2. `resume()` 后进入 `RUNNING`
3. 执行 `yield()` 回到 `READY`（若未结束）
4. 回调函数返回后进入 `TERM`

## 协程之间的调度关系

线程局部存储中维护三个关键指针：
- `t_thread_fiber`：线程主协程
- `t_scheduler_fiber`：调度协程
- `t_fiber`：当前运行协程

切换原则：
- `run_in_scheduler=true` 的协程与调度协程互切
- `run_in_scheduler=false` 的协程与线程主协程互切

## 调度与 IO 的结合

当 Hook 生效时，阻塞 IO 的流程为：
1. 调用被 Hook 的 `read/write/connect` 等
2. 若返回 `EAGAIN`，通过 `IOManager::addEvent()` 注册事件
3. 当前协程 `yield()` 挂起
4. `epoll_wait` 收到事件后触发回调/协程重新入队
5. 协程恢复并重试系统调用

这使代码可以保留“同步写法”，但运行时行为是事件驱动。
