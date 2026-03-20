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
