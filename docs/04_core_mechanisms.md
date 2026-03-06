# 04. 核心实现原理

## 协程创建

入口是 `Fiber` 构造函数：
1. 分配独立协程栈（默认 128KB）
2. `getcontext` 初始化 `ucontext_t`
3. 配置 `uc_stack` 指向协程栈
4. `makecontext` 绑定到 `Fiber::MainFunc`

`MainFunc` 作为统一入口，执行用户回调并在结束时做状态回收。

## 协程切换

核心 API：`Fiber::resume()` 和 `Fiber::yield()`。

- `resume()`：从调度上下文切到目标协程
- `yield()`：从当前协程切回调度上下文

切换底层都是 `swapcontext`，并根据 `run_in_scheduler` 决定是与 `t_scheduler_fiber` 还是 `t_thread_fiber` 互切。

## 调度逻辑

`Scheduler::run()` 主循环：
1. 从任务容器取出一个可执行任务
2. 若是协程任务，直接 `resume()`
3. 若是函数任务，包装成临时协程后 `resume()`
4. 若无任务，进入 `idle()`

`IOManager` 覆盖 `idle()`：
1. 计算最近定时器超时
2. 调用 `epoll_wait` 等待 IO 或超时
3. 收集过期定时器回调并投递到调度器
4. 将激活 fd 事件转成任务重新调度

## 协程退出机制

`Fiber::MainFunc` 执行结束后：
1. 清理 `m_cb`
2. 将状态置为 `TERM`
3. 调用 `yield()` 主动归还控制权

这种设计保证协程函数返回后不会继续落入未知执行流，退出路径统一且可控。

## 关键设计意图

- 把“执行实体（Fiber）”和“执行策略（Scheduler）”分离
- 让 `IOManager` 通过继承扩展 `Scheduler`，复用任务调度框架
- 把阻塞 IO 协程化逻辑集中在 `hook::do_io`，避免散落在业务代码
- 用 `FdManager` 保存 fd 语义，减少重复 `fcntl/getsockopt` 查询
