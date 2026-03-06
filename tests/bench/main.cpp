#include <mycoroutine/fiber.h>
#include <mycoroutine/iomanager.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct BenchResult {
    std::string name;
    std::uint64_t ops = 0;
    double seconds = 0.0;
};

BenchResult makeResult(const std::string& name,
                       std::uint64_t ops,
                       const Clock::time_point& begin,
                       const Clock::time_point& end) {
    BenchResult r;
    r.name = name;
    r.ops = ops;
    r.seconds = std::chrono::duration<double>(end - begin).count();
    return r;
}

BenchResult bench_direct_increment(std::uint64_t ops) {
    volatile std::uint64_t sink = 0;
    const auto begin = Clock::now();
    for (std::uint64_t i = 0; i < ops; ++i) {
        sink += 1;
    }
    const auto end = Clock::now();
    (void)sink;
    return makeResult("direct increment loop", ops, begin, end);
}

BenchResult bench_fiber_switch(std::uint64_t yields) {
    mycoroutine::Fiber::GetThis();

    auto worker = std::make_shared<mycoroutine::Fiber>(
        [yields]() {
            for (std::uint64_t i = 0; i < yields; ++i) {
                mycoroutine::Fiber::GetThis()->yield();
            }
        },
        0,
        false);

    const auto begin = Clock::now();
    while (worker->getState() != mycoroutine::Fiber::TERM) {
        worker->resume();
    }
    const auto end = Clock::now();

    // One resume + one yield ~= two context switches.
    return makeResult("fiber context switch", yields * 2, begin, end);
}

BenchResult bench_scheduler_callbacks(std::uint64_t tasks, std::size_t worker_threads) {
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<std::uint64_t> done{0};
    bool ready = false;

    const auto begin = Clock::now();
    {
        // `use_caller=true` avoids stop() assertion path for !use_caller in current implementation.
        // Pass worker_threads + 1 so scheduler still creates `worker_threads` real worker threads.
        mycoroutine::IOManager iom(worker_threads + 1, true, "bench_iom");
        for (std::uint64_t i = 0; i < tasks; ++i) {
            iom.scheduleLock([&]() {
                const auto current = done.fetch_add(1, std::memory_order_relaxed) + 1;
                if (current == tasks) {
                    std::lock_guard<std::mutex> lock(mutex);
                    ready = true;
                    cv.notify_one();
                }
            });
        }

        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return ready; });
    }
    const auto end = Clock::now();

    const std::string name = "scheduler callbacks (workers=" + std::to_string(worker_threads) + ")";
    return makeResult(name, tasks, begin, end);
}

BenchResult bench_thread_spawn(std::uint64_t threads) {
    std::atomic<std::uint64_t> done{0};
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(threads));

    const auto begin = Clock::now();
    for (std::uint64_t i = 0; i < threads; ++i) {
        workers.emplace_back([&done]() { done.fetch_add(1, std::memory_order_relaxed); });
    }
    for (auto& t : workers) {
        t.join();
    }
    const auto end = Clock::now();

    return makeResult("std::thread create+join", threads, begin, end);
}

void printResult(const BenchResult& r) {
    const double throughput = (r.seconds > 0.0) ? (static_cast<double>(r.ops) / r.seconds) : 0.0;
    const double ns_per_op = (r.ops > 0) ? (r.seconds * 1e9 / static_cast<double>(r.ops)) : 0.0;

    std::cout << std::left << std::setw(34) << r.name
              << std::right << std::setw(14) << r.ops
              << std::setw(14) << std::fixed << std::setprecision(6) << r.seconds
              << std::setw(16) << std::fixed << std::setprecision(2) << throughput
              << std::setw(14) << std::fixed << std::setprecision(2) << ns_per_op
              << '\n';
}

}  // namespace

int main() {
    std::cout << "mycoroutine benchmark\n";
    std::cout << "(建议使用 Release 构建运行以体现真实性能)\n\n";

    std::cout << std::left << std::setw(34) << "benchmark"
              << std::right << std::setw(14) << "ops"
              << std::setw(14) << "seconds"
              << std::setw(16) << "ops/sec"
              << std::setw(14) << "ns/op"
              << '\n';
    std::cout << std::string(92, '-') << '\n';

    std::vector<BenchResult> results;
    results.push_back(bench_direct_increment(2'000'000));
    results.push_back(bench_fiber_switch(300'000));
    results.push_back(bench_scheduler_callbacks(120'000, 1));
    results.push_back(bench_scheduler_callbacks(120'000, 4));
    results.push_back(bench_thread_spawn(8'000));

    for (const auto& r : results) {
        printResult(r);
    }

    return 0;
}
