#ifndef __MYCOROUTINE_COROUTINE_POOL_H_
#define __MYCOROUTINE_COROUTINE_POOL_H_

#include <mycoroutine/fiber.h>

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace mycoroutine {

/**
 * @brief 协程池
 * @details 复用已结束(Term)的Fiber对象，降低频繁创建/销毁开销
 */
class CoroutinePool
{
public:
    explicit CoroutinePool(size_t max_cached_per_key = 256);

    /**
     * @brief 获取一个可执行协程
     * @param cb 协程回调
     * @param stacksize 协程栈大小，0表示默认值
     * @param run_in_scheduler 是否在调度器中运行
     * @param use_shared_stack 是否使用共享栈
     */
    std::shared_ptr<Fiber> acquire(std::function<void()> cb,
                                   size_t stacksize = 0,
                                   bool run_in_scheduler = true,
                                   bool use_shared_stack = false);

    /**
     * @brief 回收协程到池中
     * @details 仅回收TERM状态协程
     */
    void release(std::shared_ptr<Fiber>& fiber,
                 size_t stacksize = 0,
                 bool run_in_scheduler = true,
                 bool use_shared_stack = false);

    /**
     * @brief 设置每个key的最大缓存数量
     */
    void setMaxCachedPerKey(size_t max_cached_per_key);

    /**
     * @brief 获取每个key的最大缓存数量
     */
    size_t getMaxCachedPerKey() const;

    /**
     * @brief 获取某个key当前缓存数量
     */
    size_t cachedCount(size_t stacksize = 0,
                       bool run_in_scheduler = true,
                       bool use_shared_stack = false) const;

    /**
     * @brief 清空协程池
     */
    void clear();

private:
    static uint64_t makeKey(size_t stacksize, bool run_in_scheduler, bool use_shared_stack);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<Fiber>>> m_cache;
    size_t m_maxCachedPerKey = 256;
};

} // namespace mycoroutine

#endif
