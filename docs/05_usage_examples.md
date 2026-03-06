# 05. 使用示例

## 示例目标

演示最小闭环：
- 创建协程任务
- 提交到调度器
- 协程让出并恢复

## 完整示例

```cpp
#include <iostream>
#include <mycoroutine/fiber.h>
#include <mycoroutine/iomanager.h>

using namespace mycoroutine;

void taskA() {
    std::cout << "taskA step1" << std::endl;
    Fiber::GetThis()->yield();
    std::cout << "taskA step2" << std::endl;
}

void taskB() {
    std::cout << "taskB run once" << std::endl;
}

int main() {
    IOManager iom(1, true, "demo");

    iom.scheduleLock(taskA);
    iom.scheduleLock(taskB);

    // IOManager 生命周期结束时会 stop + join
    return 0;
}
```

## 构建与运行

```bash
cmake --preset debug
cmake --build --preset debug
```

将示例加入 `examples/` 后可执行：

```bash
./build/debug/examples/coroutine_http_server
```

## IO Hook 使用说明

若示例涉及 `read/write/connect/accept` 等阻塞调用，需要确保 Hook 开启：

```cpp
mycoroutine::set_hook_enable(true);
```

当前代码里 `Scheduler::run()` 默认未自动开启 Hook（相关调用是注释状态），因此建议在运行线程显式设置。
