#include <mycoroutine/coroutine_pool.h>

namespace mycoroutine {

namespace {
constexpr size_t kDefaultFiberStackSize = 128000;

inline size_t normalizeStackSize(size_t stacksize)
{
    return stacksize ? stacksize : kDefaultFiberStackSize;
}

} // namespace

CoroutinePool::CoroutinePool(size_t max_cached_per_key)
    : m_maxCachedPerKey(max_cached_per_key ? max_cached_per_key : 1)
{
}

uint64_t CoroutinePool::makeKey(size_t stacksize, bool run_in_scheduler, bool use_shared_stack)
{
    const uint64_t normalized = static_cast<uint64_t>(normalizeStackSize(stacksize)) & ((1ull << 60) - 1);
    const uint64_t run_flag = run_in_scheduler ? (1ull << 62) : 0;
    const uint64_t shared_flag = use_shared_stack ? (1ull << 63) : 0;
    return normalized | run_flag | shared_flag;
}

std::shared_ptr<Fiber> CoroutinePool::acquire(std::function<void()> cb,
                                              size_t stacksize,
                                              bool run_in_scheduler,
                                              bool use_shared_stack)
{
    if (!cb)
    {
        return nullptr;
    }

    const uint64_t key = makeKey(stacksize, run_in_scheduler, use_shared_stack);
    std::shared_ptr<Fiber> fiber;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_cache.find(key);
        if (it != m_cache.end() && !it->second.empty())
        {
            fiber = it->second.back();
            it->second.pop_back();
        }
    }

    if (fiber)
    {
        fiber->reset(std::move(cb));
        return fiber;
    }

    return std::make_shared<Fiber>(std::move(cb), stacksize, run_in_scheduler, use_shared_stack);
}

void CoroutinePool::release(std::shared_ptr<Fiber>& fiber,
                            size_t stacksize,
                            bool run_in_scheduler,
                            bool use_shared_stack)
{
    if (!fiber)
    {
        return;
    }

    if (fiber->getState() != Fiber::TERM)
    {
        return;
    }

    const uint64_t key = makeKey(stacksize, run_in_scheduler, use_shared_stack);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& bucket = m_cache[key];
        if (bucket.size() < m_maxCachedPerKey)
        {
            bucket.push_back(fiber);
        }
    }

    fiber.reset();
}

void CoroutinePool::setMaxCachedPerKey(size_t max_cached_per_key)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxCachedPerKey = max_cached_per_key ? max_cached_per_key : 1;
}

size_t CoroutinePool::getMaxCachedPerKey() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_maxCachedPerKey;
}

size_t CoroutinePool::cachedCount(size_t stacksize,
                                  bool run_in_scheduler,
                                  bool use_shared_stack) const
{
    const uint64_t key = makeKey(stacksize, run_in_scheduler, use_shared_stack);
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cache.find(key);
    if (it == m_cache.end())
    {
        return 0;
    }
    return it->second.size();
}

void CoroutinePool::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
}

} // namespace mycoroutine
