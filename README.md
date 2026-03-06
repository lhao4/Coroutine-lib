# mycoroutine

一个小型、可读性优先的 C++17 有栈协程库，核心基于 `ucontext` + 多线程调度器 + `epoll` + 定时器 + Hook。

## 文档导航

- [01. 项目概览与快速开始](docs/01_project_overview.md)
- [02. 整体设计说明](docs/02_architecture_design.md)
- [03. 模块划分与调用关系](docs/03_module_breakdown.md)
- [04. 核心实现原理](docs/04_core_mechanisms.md)
- [05. 使用示例](docs/05_usage_examples.md)
- [06. 优化方向分析](docs/06_optimization_roadmap.md)
- [07. 简历与面试表达](docs/07_resume_and_interview.md)

## 快速构建

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

运行示例：

```bash
./build/debug/examples/coroutine_http_server
```
