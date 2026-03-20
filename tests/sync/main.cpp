#include <cassert>
#include <memory>
#include <vector>

#include <mycoroutine/scheduler.h>
#include <mycoroutine/sync.h>

int main() {
    using mycoroutine::Channel;
    using mycoroutine::CoMutex;
    using mycoroutine::Scheduler;
    using mycoroutine::WaitGroup;

    {
        Scheduler sc(4, true, "sync_mutex_waitgroup");
        CoMutex mutex;
        WaitGroup wg;

        constexpr int kTaskCount = 64;
        constexpr int kLoop = 300;
        int counter = 0;
        bool wait_finished = false;

        wg.add(kTaskCount);
        for (int i = 0; i < kTaskCount; ++i) {
            sc.scheduleLock([&]() {
                for (int j = 0; j < kLoop; ++j) {
                    mutex.lock();
                    ++counter;
                    mutex.unlock();
                }
                wg.done();
            });
        }

        sc.scheduleLock([&]() {
            wg.wait();
            wait_finished = true;
        });

        sc.stop();

        assert(wait_finished);
        assert(counter == kTaskCount * kLoop);
    }

    {
        Scheduler sc(1, true, "sync_channel_buffered");
        Channel<int> ch(2);
        WaitGroup wg;
        std::vector<int> consumed;
        bool done = false;

        wg.add(2);
        sc.scheduleLock([&]() {
            for (int i = 1; i <= 24; ++i) {
                const bool ok = ch.send(i);
                assert(ok);
            }
            ch.close();
            wg.done();
        });

        sc.scheduleLock([&]() {
            int value = 0;
            while (ch.recv(value)) {
                consumed.push_back(value);
            }
            wg.done();
        });

        sc.scheduleLock([&]() {
            wg.wait();
            done = true;
        });

        sc.stop();

        assert(done);
        assert(consumed.size() == 24);
        for (int i = 0; i < 24; ++i) {
            assert(consumed[static_cast<size_t>(i)] == i + 1);
        }
    }

    {
        Scheduler sc(1, true, "sync_channel_unbuffered");
        Channel<int> ch(0);
        WaitGroup wg;
        std::vector<int> consumed;

        wg.add(2);
        sc.scheduleLock([&]() {
            for (int i = 0; i < 8; ++i) {
                const bool ok = ch.send(i);
                assert(ok);
            }
            ch.close();
            wg.done();
        });

        sc.scheduleLock([&]() {
            int value = 0;
            while (ch.recv(value)) {
                consumed.push_back(value);
            }
            wg.done();
        });

        sc.scheduleLock([&]() {
            wg.wait();
        });

        sc.stop();

        assert(consumed.size() == 8);
        for (int i = 0; i < 8; ++i) {
            assert(consumed[static_cast<size_t>(i)] == i);
        }
    }

    // ---------------------------------------------------------------
    // 边界测试 1: CoMutex::tryLock 成功与失败
    // ---------------------------------------------------------------
    {
        Scheduler sc(1, true, "sync_trylock");
        CoMutex mutex;
        bool trylock_failed = false;
        bool trylock_succeeded = false;

        sc.scheduleLock([&]() {
            // 首次 tryLock 应成功
            bool ok = mutex.tryLock();
            assert(ok);
            trylock_succeeded = true;

            // 调度另一个协程尝试 tryLock（应失败）
            sc.scheduleLock([&]() {
                bool ok2 = mutex.tryLock();
                assert(!ok2);
                trylock_failed = true;
            });

            // yield 让上面的协程执行
            mycoroutine::Fiber::GetThis()->yield();

            mutex.unlock();
        });

        sc.stop();

        assert(trylock_succeeded);
        assert(trylock_failed);
    }

    // ---------------------------------------------------------------
    // 边界测试 2: Channel close 后 send 返回 false
    // ---------------------------------------------------------------
    {
        Scheduler sc(1, true, "sync_send_after_close");
        Channel<int> ch(4);
        bool send_ok = false;
        bool send_failed = false;

        sc.scheduleLock([&]() {
            send_ok = ch.send(1);
            assert(send_ok);

            ch.close();

            // close 后 send 应返回 false
            send_failed = !ch.send(2);
            assert(send_failed);

            // close 后 recv 缓冲区中剩余数据仍可读
            int value = 0;
            bool recv_ok = ch.recv(value);
            assert(recv_ok);
            assert(value == 1);

            // 缓冲区空 + 已关闭 -> recv 返回 false
            bool recv_after_drain = ch.recv(value);
            assert(!recv_after_drain);
        });

        sc.stop();

        assert(send_ok);
        assert(send_failed);
    }

    // ---------------------------------------------------------------
    // 边界测试 3: 多生产者多消费者 Channel
    // ---------------------------------------------------------------
    {
        Scheduler sc(4, true, "sync_mpmc_channel");
        Channel<int> ch(4);
        WaitGroup wg;
        std::atomic<int> total_sent{0};
        std::atomic<int> total_received{0};

        constexpr int kProducers = 4;
        constexpr int kConsumers = 4;
        constexpr int kItemsPerProducer = 50;

        wg.add(kProducers + kConsumers);

        // 生产者
        for (int p = 0; p < kProducers; ++p) {
            sc.scheduleLock([&]() {
                for (int i = 0; i < kItemsPerProducer; ++i) {
                    bool ok = ch.send(i);
                    assert(ok);
                    total_sent.fetch_add(1, std::memory_order_relaxed);
                }
                wg.done();
            });
        }

        // 消费者
        for (int c = 0; c < kConsumers; ++c) {
            sc.scheduleLock([&]() {
                int value = 0;
                while (ch.recv(value)) {
                    total_received.fetch_add(1, std::memory_order_relaxed);
                }
                wg.done();
            });
        }

        // 等生产者全部完成后关闭通道
        sc.scheduleLock([&]() {
            // 等待所有生产者
            while (total_sent.load(std::memory_order_acquire) < kProducers * kItemsPerProducer) {
                mycoroutine::Fiber::GetThis()->yield();
            }
            ch.close();
        });

        sc.scheduleLock([&]() {
            wg.wait();
        });

        sc.stop();

        assert(total_sent.load() == kProducers * kItemsPerProducer);
        assert(total_received.load() == kProducers * kItemsPerProducer);
    }

    // ---------------------------------------------------------------
    // 边界测试 4: WaitGroup 计数已归零时 wait 立即返回
    // ---------------------------------------------------------------
    {
        Scheduler sc(1, true, "sync_wg_already_zero");
        bool immediate_return = false;

        sc.scheduleLock([&]() {
            WaitGroup wg;
            // 不调用 add，count == 0，wait 应立即返回
            wg.wait();
            immediate_return = true;
        });

        sc.stop();

        assert(immediate_return);
    }

    // ---------------------------------------------------------------
    // 边界测试 5: Channel 重复 close 不崩溃
    // ---------------------------------------------------------------
    {
        Scheduler sc(1, true, "sync_double_close");
        Channel<int> ch(2);
        bool done = false;

        sc.scheduleLock([&]() {
            ch.close();
            ch.close();  // 第二次 close 应无副作用
            assert(ch.isClosed());
            done = true;
        });

        sc.stop();

        assert(done);
    }

    // ---------------------------------------------------------------
    // 生命周期测试: waiter 生命周期长于 Scheduler
    // ---------------------------------------------------------------
    {
        // waiter 生命周期长于 Scheduler 时，notify 不应触发悬空调度器访问。
        auto gate = std::make_shared<WaitGroup>();
        gate->add(1);

        {
            Scheduler sc(1, true, "sync_waiter_scheduler_lifetime");
            sc.scheduleLock([gate]() {
                gate->wait();
            });
            sc.stop();
        }

        // Scheduler 已析构，notify 不能崩溃（历史问题：潜在UAF）。
        gate->done();
    }

    return 0;
}
