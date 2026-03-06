# TEST_REPORT

## 1. 报告概述

- 项目：`mycoroutine`
- 测试时间：2026-03-06
- 测试目标：在 `Debug/Release` 以及启用网络 demo（含 `libevent`）条件下完成完整系统测试，并对结果做对比分析。

---

## 2. 测试环境

- OS：Linux（当前执行环境）
- 编译器：GCC 13.3.0
- 构建工具：CMake + Ninja
- 测试工具：CTest + 本地 HTTP 连通性检查（`curl --noproxy '*'`）

补充说明：
- 系统已安装 `libevent-dev`。
- 原先 `libevent` 未检测到的根因是系统缺少 `pkg-config` 可执行程序。
- 本轮使用项目内安装的 `pkgconf` 作为 `PKG_CONFIG_EXECUTABLE`，并成功检测到 `libevent`。

---

## 3. 测试矩阵

| 测试维度 | 配置 |
|---|---|
| 默认调试构建 | `debug` preset（`NETWORK_DEMOS=OFF`） |
| 默认发布构建 | `release` preset（`NETWORK_DEMOS=OFF`） |
| 网络 demo 构建 | `Debug + NETWORK_DEMOS=ON + libevent enabled` |
| 自动化测试 | `ctest`（当前只有 `mycoroutine_smoke_test`） |
| 运行态检查 | `coroutine_http_server` / `epoll_http_demo` / `libevent_http_demo` |

---

## 4. 执行步骤与结果

## 4.1 Debug（默认）

命令：

```bash
cmake --preset debug
cmake --build --preset debug -j4
ctest --preset debug
```

结果：

- 配置：通过
- 构建：通过
- 测试：`1/1 passed`

## 4.2 Release（默认）

命令：

```bash
cmake --preset release
cmake --build --preset release -j4
ctest --test-dir build/release --output-on-failure
```

结果：

- 配置：通过
- 构建：通过
- 测试：`1/1 passed`

## 4.3 启用网络 demo + libevent

命令：

```bash
LD_LIBRARY_PATH=$PWD/.tools/pkgconf/lib \
PKG_CONFIG_PATH=/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig \
cmake -S . -B build/debug-net-libevent -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DMYCOROUTINE_BUILD_EXAMPLES=ON \
  -DMYCOROUTINE_BUILD_NETWORK_DEMOS=ON \
  -DPKG_CONFIG_EXECUTABLE=$PWD/.tools/pkgconf/bin/pkgconf

cmake --build build/debug-net-libevent -j4
ctest --test-dir build/debug-net-libevent --output-on-failure
```

结果：

- 配置：通过
- 构建：通过
- 自动化测试：`1/1 passed`
- demo 产物：
  - `tests/epoll_http_demo` 已生成
  - `tests/libevent_http_demo` 已生成
  - `examples/coroutine_http_server` 已生成

---

## 5. 运行态联通测试（系统级）

## 5.1 关键修正

初次联通测试失败的根因不是服务端，而是 `curl` 默认走环境代理（`http_proxy=127.0.0.1:9674`），导致请求未直连本地服务。

本轮统一使用：

```bash
curl --noproxy '*'
```

## 5.2 联通结果

| 可执行程序 | 端口 | 预期响应 | 实际响应 | 结果 |
|---|---:|---|---|---|
| `coroutine_http_server` | 8080 | `Hello, World!` | `Hello, World!` | 通过 |
| `epoll_http_demo` | 8888 | `1` | `1` | 通过 |
| `libevent_http_demo` | 8080 | `Hello, World!` | `Hello, World!` | 通过 |

结论：
- 三个服务均可正常启动并对外提供 HTTP 响应。

---

## 6. 对比分析

## 6.1 Debug vs Release（默认矩阵）

| 指标 | Debug | Release | 分析 |
|---|---:|---:|---|
| 配置成功 | 是 | 是 | 无差异 |
| 构建成功 | 是 | 是 | 无差异 |
| ctest 通过率 | 100% (1/1) | 100% (1/1) | 无差异 |

说明：
- 当前自动化测试规模较小，主要验证协程基础可用性。
- 从结果上看，核心路径在两个构建类型下行为一致。

## 6.2 网络 demo 支持能力对比

| 阶段 | `libevent` 检测状态 | 原因 |
|---|---|---|
| 上次测试 | 未检测到 | 缺少 `pkg-config` 可执行文件 |
| 本次测试 | 检测并构建成功 | 使用本地 `pkgconf` + 正确 `PKG_CONFIG_PATH` |

说明：
- 依赖本身存在，问题在“工具链缺口”。

## 6.3 自动化测试与系统测试的差异

- 自动化测试（`ctest`）目前只覆盖 `smoke`。
- 运行态联通测试（HTTP 实际请求）补齐了 demo 级验证。
- 两者结合后，本轮测试链路完整性明显高于上次。

---

## 7. 结论

1. 项目在 `Debug/Release` 下均可稳定构建并通过现有自动化测试。
2. 在补齐 `pkg-config` 能力后，`libevent` 相关 demo 可成功检测并构建。
3. 三个服务程序均通过实际 HTTP 连通性验证，说明运行态基本可用。
4. 上次“未成功完成”的主要原因已排除：
   - `libevent` 检测问题：已解决
   - 本地请求超时问题：代理路径问题已解决

---

## 8. 仍需改进的测试点

1. 增加 Hook IO 路径单测（`EINTR/EAGAIN/ETIMEDOUT`）。
2. 增加 `IOManager` 事件取消语义测试（`del/cancel/cancelAll`）。
3. 增加定时器边界测试（同一触发时间、多循环定时器）。
4. 增加并发压力测试与 Sanitizer（ASan/TSan/UBSan）。

