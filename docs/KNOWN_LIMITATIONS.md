# KNOWN_LIMITATIONS

## 文档用途
汇总 `mycoroutine` 当前版本的已知边界与限制条件。

## 1. 功能边界

### 1.1 共享栈 + 嵌套协程组合
- 边界：父子协程同时启用共享栈时，`call()` 不执行切换。
- 行为：返回 `Fiber::CALL_ERR_SHARED_NESTED_UNSUPPORTED`。

### 1.2 平台依赖
- 运行时依赖 Linux 能力：`ucontext`、`epoll`、`eventfd`、`dlsym`。
- 非 Linux 平台未适配。

### 1.3 调度队列结构
- 当前为全局任务向量 + 策略选取。
- 在任务数量极大时，选取与锁竞争开销会放大。

## 2. 测试边界
- 当前自动化测试覆盖 5 个核心目标（smoke/shared_stack/nested/policy/pool）。
- 网络 demo 连通性与 benchmark 仍以手工流程为主，不在默认 `ctest` 集合中。

## 3. 运行时约束

### 3.1 共享栈协程线程绑定
- 共享栈协程在首次运行线程绑定后，不支持跨线程执行。

### 3.2 `call()` 使用约束
- `call()` 要求目标协程处于 `READY`。
- 禁止自调用。
- 调用失败通过错误码返回，不使用异常路径。

### 3.3 协程池回收约束
- `CoroutinePool::release()` 仅回收 `TERM` 协程。
- `acquire/release` 参数必须匹配同一 key 维度（`stacksize/run_in_scheduler/use_shared_stack`）。

## 4. 观测能力边界
- 运行期主要依赖日志输出。
- 尚未提供内建指标导出（如调度延迟、池命中率、共享栈拷贝字节数）。
