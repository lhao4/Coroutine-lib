# mycoroutine

基于 C++17 的有栈协程库，核心包含 Fiber、Scheduler、IOManager、Hook 和 Timer。

## 环境要求

- Linux
- CMake >= 3.20
- C++17 编译器（GCC 9+/Clang 10+ 推荐）

## 目录结构

```text
.
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/
├── include/mycoroutine/
├── src/
├── examples/
└── tests/
```

## 快速构建

### 方式一：使用 Preset（推荐）

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

### 方式二：手动配置

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## 安装与包导出

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j
cmake --install build/release --prefix /usr/local
```

安装后会导出 CMake 包配置，可通过 `find_package(mycoroutine CONFIG REQUIRED)` 使用。

## 可选目标

- `MYCOROUTINE_BUILD_EXAMPLES=ON|OFF`：是否构建示例，默认 `ON`
- `MYCOROUTINE_BUILD_NETWORK_DEMOS=ON|OFF`：是否构建 `tests/epoll` 与 `tests/libevent` 里的网络 demo，默认 `OFF`
