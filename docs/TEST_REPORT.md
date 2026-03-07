# TEST_REPORT

## 1. 报告概述
- 项目：`mycoroutine`
- 报告日期：2026-03-07
- 报告范围：当前代码基线的构建与核心自动化回归测试

## 2. 测试环境
- OS：Linux
- 编译器：GCC（CMake debug preset）
- 构建工具：CMake + Ninja
- 测试工具：CTest

## 3. 执行命令

```bash
cmake --build --preset debug -j
ctest --preset debug --output-on-failure
```

## 4. 自动化测试结果

| 测试名 | 结果 |
|---|---|
| `mycoroutine_smoke_test` | Passed |
| `mycoroutine_shared_stack_test` | Passed |
| `mycoroutine_nested_test` | Passed |
| `mycoroutine_policy_test` | Passed |
| `mycoroutine_pool_test` | Passed |

汇总：`5/5 passed`，`0 failed`。

## 5. 覆盖点映射
- `smoke`：基础调度路径
- `shared_stack`：共享栈复用与快照一致性
- `nested`：嵌套 `call/back` 与错误码路径
- `policy`：FIFO/PRIORITY/MLFQ/EDF/HYBRID 与 MLFQ 参数归一化
- `pool`：协程池复用、容量上限、key 隔离

## 6. 结论
当前版本核心升级能力已通过自动化回归：共享栈、协程嵌套、复杂调度、协程池均处于可运行状态。
