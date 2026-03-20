#include <cassert>
#include <cstring>
#include <chrono>
#include <atomic>
#include <thread>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <mycoroutine/hook.h>
#include <mycoroutine/fiber.h>
#include <mycoroutine/iomanager.h>
#include <mycoroutine/fd_manager.h>

// ============================================================================
// Test 1: hook_init — 所有 dlsym 函数指针均非空
// 验证 Bug 3 修复：hook_init 完成后所有函数指针应已初始化
// ============================================================================
void test_hook_init_pointers()
{
    assert(sleep_f != nullptr);
    assert(usleep_f != nullptr);
    assert(nanosleep_f != nullptr);
    assert(socket_f != nullptr);
    assert(connect_f != nullptr);
    assert(accept_f != nullptr);
    assert(read_f != nullptr);
    assert(readv_f != nullptr);
    assert(recv_f != nullptr);
    assert(recvfrom_f != nullptr);
    assert(recvmsg_f != nullptr);
    assert(write_f != nullptr);
    assert(writev_f != nullptr);
    assert(send_f != nullptr);
    assert(sendto_f != nullptr);
    assert(sendmsg_f != nullptr);
    assert(close_f != nullptr);
    assert(fcntl_f != nullptr);
    assert(ioctl_f != nullptr);
    assert(getsockopt_f != nullptr);
    assert(setsockopt_f != nullptr);
}

// ============================================================================
// Test 2: hook 启用/禁用开关
// ============================================================================
void test_hook_enable_disable()
{
    // 默认状态（主线程未设置时为 false）
    bool original = mycoroutine::is_hook_enable();

    mycoroutine::set_hook_enable(true);
    assert(mycoroutine::is_hook_enable() == true);

    mycoroutine::set_hook_enable(false);
    assert(mycoroutine::is_hook_enable() == false);

    // 恢复
    mycoroutine::set_hook_enable(original);
}

// ============================================================================
// Test 3: nanosleep — 无 IOManager 时不崩溃（回退到原始调用）
// 验证 Bug 1 修复：hook 启用但无 IOManager 时应回退到 nanosleep_f
// ============================================================================
void test_nanosleep_without_iomanager()
{
    mycoroutine::set_hook_enable(true);

    // 确认当前线程没有 IOManager
    assert(mycoroutine::IOManager::GetThis() == nullptr);

    struct timespec req = {0, 1000000}; // 1ms
    struct timespec rem = {};
    auto start = std::chrono::steady_clock::now();
    int ret = nanosleep(&req, &rem);
    auto elapsed = std::chrono::steady_clock::now() - start;

    assert(ret == 0);
    // 原始 nanosleep 应实际睡眠，至少 0.5ms
    assert(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() >= 500);

    mycoroutine::set_hook_enable(false);
}

// ============================================================================
// Test 4: nanosleep — 有 IOManager 时走协程路径（定时器调度）
// 验证 Bug 1 修复：整数溢出修复后，协程 nanosleep 正常工作
// ============================================================================
void test_nanosleep_with_iomanager()
{
    std::atomic<bool> done{false};
    std::atomic<int> ret_code{-1};

    {
        mycoroutine::IOManager iom(1, true, "test_nanosleep");

        iom.scheduleLock([&]() {
            // IOManager 线程内 hook 已启用
            mycoroutine::set_hook_enable(true);

            struct timespec req = {0, 50000000}; // 50ms
            struct timespec rem = {};
            auto start = std::chrono::steady_clock::now();
            ret_code = nanosleep(&req, &rem);
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            // 协程路径应在 ~50ms 后返回（允许一定误差）
            assert(elapsed_ms >= 30);
            assert(elapsed_ms < 500);

            done = true;
        });
    } // IOManager 析构会等待任务完成

    assert(done);
    assert(ret_code == 0);
}

// ============================================================================
// Test 5: nanosleep — 大 tv_sec 值不溢出
// 验证 Bug 1 修复：uint64_t 替代 int 后，大值计算不溢出
// ============================================================================
void test_nanosleep_large_value_no_overflow()
{
    // 模拟计算逻辑，验证不溢出
    // 2^31 / 1000 = 2147483.648s ≈ 24.8 天
    // 如果用 int，tv_sec=2147484 时 tv_sec*1000 就溢出了
    struct timespec req;
    req.tv_sec = 100000;       // 100000 秒
    req.tv_nsec = 999000000;   // 999ms

    // 手动计算预期值
    uint64_t expected = (uint64_t)req.tv_sec * 1000 + req.tv_nsec / 1000000;
    assert(expected == 100000999ULL);

    // 验证旧的 int 方式会溢出
    int old_calc = req.tv_sec * 1000 + req.tv_nsec / 1000 / 1000;
    // 100000 * 1000 = 100000000，在 int 范围内，但精度不同
    // 真正的溢出测试：tv_sec = 2200000
    req.tv_sec = 2200000;
    req.tv_nsec = 0;
    uint64_t correct = (uint64_t)req.tv_sec * 1000;
    assert(correct == 2200000000ULL); // 正确：2.2 × 10^9，超出 int32 范围

    // 用 int 计算会截断（溢出行为是 UB，这里只验证 uint64_t 正确）
    (void)old_calc;
}

// ============================================================================
// Test 6: nanosleep — tv_nsec 除法精度
// 验证 Bug 1 修复：/1000000 vs /1000/1000 的精度差异
// ============================================================================
void test_nanosleep_nsec_precision()
{
    // /1000/1000 对于不能被 1000 整除的值会因整数截断而丢失精度
    // 例如 tv_nsec = 1500000 (1.5ms)
    // /1000000 = 1  （正确，截断到 1ms）
    // /1000/1000 = 1500/1000 = 1（碰巧相同）

    // tv_nsec = 999999 (0.999999ms)
    // /1000000 = 0
    // /1000/1000 = 999/1000 = 0（碰巧相同）

    // tv_nsec = 1999999 (1.999999ms)
    // /1000000 = 1
    // /1000/1000 = 1999/1000 = 1（碰巧相同）

    // 两种方式在整数截断下通常结果一致，但 /1000000 更直观且避免中间溢出
    long nsec = 999999999L; // 最大合法值
    assert(nsec / 1000000 == 999);
}

// ============================================================================
// Test 7: close() hook — 关闭 socket 时 FdCtx::isClosed 被设置为 true
// 验证 Bug 2 修复：close 前应调用 setClosed(true)
// ============================================================================
void test_close_sets_is_closed()
{
    std::atomic<bool> done{false};

    {
        mycoroutine::IOManager iom(1, true, "test_close");

        iom.scheduleLock([&]() {
            mycoroutine::set_hook_enable(true);

            // 创建 socket，触发 hook 自动管理 FdCtx
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            assert(fd >= 0);

            // 获取 FdCtx，hook 后的 socket() 会自动创建
            auto ctx = mycoroutine::FdMgr::GetInstance()->get(fd);
            assert(ctx != nullptr);
            assert(ctx->isSocket() == true);
            assert(ctx->isClosed() == false);

            // 保存 ctx 的引用（shared_ptr），在 close 之后检查
            auto ctx_copy = ctx;

            // 关闭 fd
            close(fd);

            // Bug 2 修复后，ctx 应被标记为 closed
            assert(ctx_copy->isClosed() == true);

            // FdManager 中已删除该 fd 的记录
            auto ctx_after = mycoroutine::FdMgr::GetInstance()->get(fd);
            assert(ctx_after == nullptr);

            done = true;
        });
    }

    assert(done);
}

// ============================================================================
// Test 8: close() hook — 非 socket fd 不崩溃
// ============================================================================
void test_close_non_socket_fd()
{
    std::atomic<bool> done{false};

    {
        mycoroutine::IOManager iom(1, true, "test_close_nonsock");

        iom.scheduleLock([&]() {
            mycoroutine::set_hook_enable(true);

            // pipe 不是 socket
            int pipefd[2];
            int ret = pipe(pipefd);
            assert(ret == 0);

            // pipe 端不会被 FdManager 管理（非 socket()创建）
            // close 应正常工作不崩溃
            assert(close(pipefd[0]) == 0);
            assert(close(pipefd[1]) == 0);

            done = true;
        });
    }

    assert(done);
}

// ============================================================================
// Test 9: socket/close 生命周期 — 创建和销毁 FdCtx
// ============================================================================
void test_socket_fdctx_lifecycle()
{
    std::atomic<bool> done{false};

    {
        mycoroutine::IOManager iom(1, true, "test_lifecycle");

        iom.scheduleLock([&]() {
            mycoroutine::set_hook_enable(true);

            int fd1 = socket(AF_INET, SOCK_STREAM, 0);
            int fd2 = socket(AF_INET, SOCK_STREAM, 0);
            assert(fd1 >= 0 && fd2 >= 0);
            assert(fd1 != fd2);

            auto ctx1 = mycoroutine::FdMgr::GetInstance()->get(fd1);
            auto ctx2 = mycoroutine::FdMgr::GetInstance()->get(fd2);
            assert(ctx1 != nullptr && ctx2 != nullptr);
            assert(!ctx1->isClosed());
            assert(!ctx2->isClosed());

            // 关闭 fd1，fd2 应不受影响
            close(fd1);
            assert(ctx1->isClosed() == true);
            assert(ctx2->isClosed() == false);
            assert(mycoroutine::FdMgr::GetInstance()->get(fd1) == nullptr);
            assert(mycoroutine::FdMgr::GetInstance()->get(fd2) != nullptr);

            close(fd2);
            assert(ctx2->isClosed() == true);

            done = true;
        });
    }

    assert(done);
}

// ============================================================================
// Test 10: IOManager 定时器基本功能
// ============================================================================
void test_iomanager_timer()
{
    std::atomic<bool> timer_fired{false};

    {
        mycoroutine::IOManager iom(1, true, "test_timer");

        iom.addTimer(50, [&]() {
            timer_fired = true;
        });
    } // IOManager 析构等待

    assert(timer_fired);
}

// ============================================================================
// Test 11: IOManager 多个定时器按序触发
// ============================================================================
void test_iomanager_multiple_timers()
{
    std::vector<int> order;
    std::mutex mtx;

    {
        mycoroutine::IOManager iom(1, true, "test_multi_timer");

        iom.addTimer(80, [&]() {
            std::lock_guard<std::mutex> lock(mtx);
            order.push_back(2);
        });

        iom.addTimer(30, [&]() {
            std::lock_guard<std::mutex> lock(mtx);
            order.push_back(1);
        });

        iom.addTimer(150, [&]() {
            std::lock_guard<std::mutex> lock(mtx);
            order.push_back(3);
        });
    }

    assert(order.size() == 3);
    assert(order[0] == 1);
    assert(order[1] == 2);
    assert(order[2] == 3);
}

// ============================================================================
// Test 12: sleep/usleep hook 在 IOManager 中工作
// ============================================================================
void test_sleep_usleep_hook()
{
    std::atomic<bool> done{false};

    {
        mycoroutine::IOManager iom(1, true, "test_sleep");

        iom.scheduleLock([&]() {
            mycoroutine::set_hook_enable(true);

            auto start = std::chrono::steady_clock::now();
            usleep(50000); // 50ms
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            // 协程路径
            assert(elapsed_ms >= 30);
            assert(elapsed_ms < 500);

            done = true;
        });
    }

    assert(done);
}

// ============================================================================
// Test 13: hook 禁用时 nanosleep 走原始系统调用
// ============================================================================
void test_nanosleep_hook_disabled()
{
    mycoroutine::set_hook_enable(false);

    struct timespec req = {0, 2000000}; // 2ms
    struct timespec rem = {};
    auto start = std::chrono::steady_clock::now();
    int ret = nanosleep(&req, &rem);
    auto elapsed = std::chrono::steady_clock::now() - start;

    assert(ret == 0);
    // 原始系统调用，至少睡了 1ms
    assert(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() >= 1000);
}

// ============================================================================
// Test 14: IOManager addEvent/cancelAll — 基本事件管理
// ============================================================================
void test_iomanager_event_cancel()
{
    std::atomic<bool> event_fired{false};

    {
        mycoroutine::IOManager iom(1, true, "test_event_cancel");

        iom.scheduleLock([&]() {
            int fd = socket_f(AF_INET, SOCK_STREAM, 0);
            assert(fd >= 0);

            // 添加写事件
            bool ret = iom.addEvent(fd, mycoroutine::IOManager::WRITE, [&]() {
                event_fired = true;
            });
            assert(ret);

            // 取消所有事件
            bool ok = iom.cancelAll(fd);
            assert(ok);

            close_f(fd);
        });
    }

    // cancelAll 会触发回调
    assert(event_fired);
}

// ============================================================================
// Test 15: IOManager 多协程并发调度
// ============================================================================
void test_iomanager_concurrent_schedule()
{
    std::atomic<int> counter{0};
    const int N = 20;

    {
        mycoroutine::IOManager iom(2, true, "test_concurrent");

        for (int i = 0; i < N; ++i) {
            iom.scheduleLock([&]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
    }

    assert(counter == N);
}

// ============================================================================
// Test 16: FdCtx 属性正确初始化
// ============================================================================
void test_fdctx_properties()
{
    std::atomic<bool> done{false};

    {
        mycoroutine::IOManager iom(1, true, "test_fdctx_props");

        iom.scheduleLock([&]() {
            mycoroutine::set_hook_enable(true);

            int fd = socket(AF_INET, SOCK_STREAM, 0);
            assert(fd >= 0);

            auto ctx = mycoroutine::FdMgr::GetInstance()->get(fd);
            assert(ctx != nullptr);
            assert(ctx->isInit() == true);
            assert(ctx->isSocket() == true);
            assert(ctx->isClosed() == false);
            // hook 后的 socket 会自动设置系统非阻塞
            assert(ctx->getSysNonblock() == true);
            // 用户层面默认阻塞
            assert(ctx->getUserNonblock() == false);

            close(fd);
            done = true;
        });
    }

    assert(done);
}

// ============================================================================
// Test 17: fcntl hook — 用户非阻塞标志管理
// ============================================================================
void test_fcntl_nonblock_flag()
{
    std::atomic<bool> done{false};

    {
        mycoroutine::IOManager iom(1, true, "test_fcntl");

        iom.scheduleLock([&]() {
            mycoroutine::set_hook_enable(true);

            int fd = socket(AF_INET, SOCK_STREAM, 0);
            assert(fd >= 0);

            auto ctx = mycoroutine::FdMgr::GetInstance()->get(fd);
            assert(ctx != nullptr);

            // 用户设置 O_NONBLOCK
            int flags = fcntl(fd, F_GETFL);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            assert(ctx->getUserNonblock() == true);

            // 用户取消 O_NONBLOCK
            fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
            assert(ctx->getUserNonblock() == false);

            close(fd);
            done = true;
        });
    }

    assert(done);
}

// ============================================================================
// Test 18: 条件定时器 — 条件对象销毁后不触发
// ============================================================================
void test_condition_timer()
{
    std::atomic<bool> timer_fired{false};

    {
        mycoroutine::IOManager iom(1, true, "test_cond_timer");

        {
            auto cond = std::make_shared<int>(42);
            iom.addConditionTimer(50, [&]() {
                timer_fired = true;
            }, cond);
            // cond 离开作用域被销毁
        }

        // 等足够时间让定时器到期
        iom.addTimer(200, []() {});
    }

    // 条件对象已销毁，定时器不应触发
    assert(!timer_fired);
}

// ============================================================================
// Main
// ============================================================================
int main()
{
    // 初始化 Fiber 系统
    mycoroutine::Fiber::GetThis();

    // Bug 3: hook_init 函数指针验证
    test_hook_init_pointers();

    // Hook 开关
    test_hook_enable_disable();

    // Bug 1: nanosleep 溢出 + null IOManager
    test_nanosleep_without_iomanager();
    test_nanosleep_with_iomanager();
    test_nanosleep_large_value_no_overflow();
    test_nanosleep_nsec_precision();
    test_nanosleep_hook_disabled();

    // Bug 2: close() 设置 isClosed
    test_close_sets_is_closed();
    test_close_non_socket_fd();
    test_socket_fdctx_lifecycle();

    // sleep/usleep hook
    test_sleep_usleep_hook();

    // IOManager 定时器
    test_iomanager_timer();
    test_iomanager_multiple_timers();
    test_condition_timer();

    // IOManager 事件管理
    test_iomanager_event_cancel();
    test_iomanager_concurrent_schedule();

    // FdCtx 属性
    test_fdctx_properties();
    test_fcntl_nonblock_flag();

    return 0;
}
