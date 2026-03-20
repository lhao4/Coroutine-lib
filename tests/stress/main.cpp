#include <mycoroutine/fiber.h>
#include <mycoroutine/iomanager.h>

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Scenario 1: Multi-threaded concurrent scheduling
//   Create a scheduler with 4 threads.  From the main thread, schedule 1000
//   fibers.  Each fiber increments an atomic counter, yields once, then
//   increments again.  After stop(), verify counter == 2000.
// ---------------------------------------------------------------------------
static void stress_concurrent() {
    constexpr int NUM_FIBERS = 1000;
    constexpr int EXPECTED   = NUM_FIBERS * 2;

    std::atomic<int> counter{0};
    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;

    {
        // use_caller=true, 4+1 so 4 real worker threads are created
        mycoroutine::IOManager iom(4 + 1, true, "stress1");

        for (int i = 0; i < NUM_FIBERS; ++i) {
            // Create a Fiber that yields once.  After the first increment,
            // re-schedule itself so the scheduler picks it up again after yield.
            auto fiber = std::make_shared<mycoroutine::Fiber>(
                [&counter, &iom]() {
                    counter.fetch_add(1, std::memory_order_relaxed);
                    // Re-schedule this fiber so it gets resumed after yield.
                    auto self = mycoroutine::Fiber::GetThis();
                    iom.scheduleLock(self);
                    self->yield();
                    // Resumed -- second increment.
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            iom.scheduleLock(fiber);
        }

        // Schedule a sentinel that signals completion once counter reaches EXPECTED.
        auto sentinel = std::make_shared<std::function<void()>>();
        *sentinel = [&, sentinel]() {
            if (counter.load(std::memory_order_acquire) >= EXPECTED) {
                std::lock_guard<std::mutex> lock(mtx);
                ready = true;
                cv.notify_one();
            } else {
                iom.scheduleLock(std::function<void()>(*sentinel));
            }
        };
        iom.scheduleLock(std::function<void()>(*sentinel));

        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return ready; });
        lock.unlock();
        *sentinel = nullptr;  // break circular reference
    }

    assert(counter.load() == EXPECTED);
    std::cout << "stress_concurrent: PASS" << std::endl;
}

// ---------------------------------------------------------------------------
// Scenario 2: Schedule from multiple external threads
//   Create a scheduler with 2 threads.  Spawn 4 std::threads that each
//   schedule 250 fibers into the scheduler.  Each fiber increments an atomic
//   counter.  Join all threads, stop scheduler, verify counter == 1000.
// ---------------------------------------------------------------------------
static void stress_external_threads() {
    constexpr int NUM_THREADS    = 4;
    constexpr int FIBERS_PER_THR = 250;
    constexpr int EXPECTED       = NUM_THREADS * FIBERS_PER_THR;

    std::atomic<int> counter{0};
    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;

    {
        mycoroutine::IOManager iom(2 + 1, true, "stress2");

        // Spawn external threads that push work into the scheduler.
        std::vector<std::thread> producers;
        producers.reserve(NUM_THREADS);
        for (int t = 0; t < NUM_THREADS; ++t) {
            producers.emplace_back([&iom, &counter]() {
                for (int i = 0; i < FIBERS_PER_THR; ++i) {
                    iom.scheduleLock(std::function<void()>(
                        [&counter]() {
                            counter.fetch_add(1, std::memory_order_relaxed);
                        }));
                }
            });
        }

        // Join all producer threads.
        for (auto& t : producers) {
            t.join();
        }

        // Schedule a sentinel that signals completion once counter reaches EXPECTED.
        auto sentinel = std::make_shared<std::function<void()>>();
        *sentinel = [&, sentinel]() {
            if (counter.load(std::memory_order_acquire) >= EXPECTED) {
                std::lock_guard<std::mutex> lock(mtx);
                ready = true;
                cv.notify_one();
            } else {
                iom.scheduleLock(std::function<void()>(*sentinel));
            }
        };
        iom.scheduleLock(std::function<void()>(*sentinel));

        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return ready; });
        lock.unlock();
        *sentinel = nullptr;  // break circular reference
    }

    assert(counter.load() == EXPECTED);
    std::cout << "stress_external_threads: PASS" << std::endl;
}

// ---------------------------------------------------------------------------
// Scenario 3: Coroutine pool under contention
//   Create a scheduler with 4 threads.  Schedule 500 callback tasks (not
//   fiber tasks -- use the cb path so the coroutine pool is exercised).
//   Each callback increments an atomic counter.  After stop(), verify
//   counter == 500.
// ---------------------------------------------------------------------------
static void stress_callback_pool() {
    constexpr int NUM_TASKS = 500;

    std::atomic<int> counter{0};
    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;

    {
        mycoroutine::IOManager iom(4 + 1, true, "stress3");

        for (int i = 0; i < NUM_TASKS; ++i) {
            iom.scheduleLock(std::function<void()>(
                [&counter]() {
                    counter.fetch_add(1, std::memory_order_relaxed);
                }));
        }

        // Schedule a sentinel that signals completion once counter reaches NUM_TASKS.
        auto sentinel = std::make_shared<std::function<void()>>();
        *sentinel = [&, sentinel]() {
            if (counter.load(std::memory_order_acquire) >= NUM_TASKS) {
                std::lock_guard<std::mutex> lock(mtx);
                ready = true;
                cv.notify_one();
            } else {
                iom.scheduleLock(std::function<void()>(*sentinel));
            }
        };
        iom.scheduleLock(std::function<void()>(*sentinel));

        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return ready; });
        lock.unlock();
        *sentinel = nullptr;  // break circular reference
    }

    assert(counter.load() == NUM_TASKS);
    std::cout << "stress_callback_pool: PASS" << std::endl;
}

// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== mycoroutine stress tests ===" << std::endl;

    stress_concurrent();
    stress_external_threads();
    stress_callback_pool();

    std::cout << "All stress tests passed." << std::endl;
    return 0;
}
