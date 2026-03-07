#include <cassert>

#include <mycoroutine/coroutine_pool.h>

int main() {
    using mycoroutine::CoroutinePool;
    using mycoroutine::Fiber;

    Fiber::GetThis();

    {
        CoroutinePool pool(8);
        int value = 0;

        auto f1 = pool.acquire([&value]() { value = 1; }, 0, false, false);
        assert(f1);
        auto* raw1 = f1.get();
        f1->resume();
        assert(value == 1);
        assert(f1->getState() == Fiber::TERM);

        pool.release(f1, 0, false, false);
        assert(!f1);
        assert(pool.cachedCount(0, false, false) == 1);

        auto f2 = pool.acquire([&value]() { value = 2; }, 0, false, false);
        assert(f2);
        assert(f2.get() == raw1);

        f2->resume();
        assert(value == 2);
        assert(f2->getState() == Fiber::TERM);

        pool.release(f2, 0, false, false);
        assert(pool.cachedCount(0, false, false) == 1);
    }

    {
        CoroutinePool pool(1);

        auto f1 = pool.acquire([] {}, 0, false, false);
        auto f2 = pool.acquire([] {}, 0, false, false);

        f1->resume();
        f2->resume();

        pool.release(f1, 0, false, false);
        pool.release(f2, 0, false, false);

        // 上限为1
        assert(pool.cachedCount(0, false, false) == 1);
    }

    {
        // 验证不同key不混用
        CoroutinePool pool(8);
        auto f_normal = pool.acquire([] {}, 64 * 1024, false, false);
        auto f_shared = pool.acquire([] {}, 64 * 1024, false, true);

        f_normal->resume();
        f_shared->resume();

        pool.release(f_normal, 64 * 1024, false, false);
        pool.release(f_shared, 64 * 1024, false, true);

        assert(pool.cachedCount(64 * 1024, false, false) == 1);
        assert(pool.cachedCount(64 * 1024, false, true) == 1);
    }

    return 0;
}
