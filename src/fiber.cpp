#include <mycoroutine/fiber.h>
#include <mycoroutine/thread.h>

#include <cstring>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <unordered_map>

#if defined(__SANITIZE_ADDRESS__)
#define MYCOROUTINE_HAS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define MYCOROUTINE_HAS_ASAN 1
#endif
#endif
#ifndef MYCOROUTINE_HAS_ASAN
#define MYCOROUTINE_HAS_ASAN 0
#endif

#if MYCOROUTINE_HAS_ASAN
#include <sanitizer/asan_interface.h>
#endif

// 调试模式开关，设置为true时会输出协程的创建、销毁和切换信息
static bool debug = false;

extern "C" {
__attribute__((weak)) void __sanitizer_start_switch_fiber(void** fake_stack_save,
                                                           const void* bottom,
                                                           size_t size);
__attribute__((weak)) void __sanitizer_finish_switch_fiber(void* fake_stack_save,
                                                            const void** bottom_old,
                                                            size_t* size_old);
__attribute__((weak)) void* __tsan_create_fiber(unsigned flags);
__attribute__((weak)) void __tsan_destroy_fiber(void* fiber);
__attribute__((weak)) void __tsan_switch_to_fiber(void* fiber, unsigned flags);
__attribute__((weak)) void* __tsan_get_current_fiber();
}

namespace mycoroutine {

namespace {

struct SharedStackSlot {
    char* stack = nullptr;
    size_t size = 0;
    std::weak_ptr<Fiber> owner;
};

class SharedStackPool {
public:
    SharedStackPool(size_t slot_count, size_t slot_size) {
        if (slot_count == 0) {
            slot_count = 1;
        }
        m_slots.resize(slot_count);
        for (auto& slot : m_slots) {
            slot.stack = static_cast<char*>(malloc(slot_size));
            assert(slot.stack != nullptr);
            slot.size = slot_size;
            slot.owner.reset();
        }
    }

    ~SharedStackPool() {
        for (auto& slot : m_slots) {
            free(slot.stack);
            slot.stack = nullptr;
            slot.owner.reset();
            slot.size = 0;
        }
    }

    SharedStackSlot* acquireSlot() {
        assert(!m_slots.empty());
        SharedStackSlot* slot = &m_slots[m_next % m_slots.size()];
        ++m_next;
        return slot;
    }

private:
    std::vector<SharedStackSlot> m_slots;
    size_t m_next = 0;
};

static SharedStackSlot* ToSharedStackSlot(void* slot) {
    return reinterpret_cast<SharedStackSlot*>(slot);
}

static uintptr_t AlignDown(uintptr_t value, size_t alignment) {
    assert(alignment > 0);
    return value & ~(static_cast<uintptr_t>(alignment) - 1);
}

static uintptr_t GetContextStackPointer(const FiberContext& ctx) {
#if defined(__x86_64__)
    return reinterpret_cast<uintptr_t>(ctx.rsp);
#elif defined(__aarch64__)
    return reinterpret_cast<uintptr_t>(ctx.sp);
#else
#error "unsupported architecture"
#endif
}

[[noreturn]] static void FiberEntryPoint() {
    Fiber::MainFunc();
    std::abort();
}

static void InitFiberContextStack(FiberContext& ctx, void* stack, size_t stack_size) {
    assert(stack != nullptr);
    assert(stack_size > 0);

    ctx = FiberContext{};
    const uintptr_t stack_bottom = reinterpret_cast<uintptr_t>(stack);
    const uintptr_t stack_top = stack_bottom + stack_size;
    const uintptr_t aligned_top = AlignDown(stack_top, kFiberContextStackAlignment);

#if defined(__x86_64__)
    const uintptr_t entry_sp = aligned_top - kFiberContextEntryStackAdjust;
    *reinterpret_cast<uintptr_t*>(entry_sp) = 0;
    ctx.rsp = reinterpret_cast<void*>(entry_sp);
    ctx.rip = reinterpret_cast<void*>(&FiberEntryPoint);
#elif defined(__aarch64__)
    const uintptr_t entry_sp = aligned_top - kFiberContextEntryStackAdjust;
    ctx.sp = reinterpret_cast<void*>(entry_sp);
    ctx.pc = reinterpret_cast<void*>(&FiberEntryPoint);
#else
#error "unsupported architecture"
#endif
}

#if defined(__x86_64__)
static_assert(sizeof(FiberContext) == 64, "x86_64 FiberContext layout mismatch");
static_assert(offsetof(FiberContext, rsp) == 0, "x86_64 rsp offset mismatch");
static_assert(offsetof(FiberContext, rip) == 8, "x86_64 rip offset mismatch");
static_assert(offsetof(FiberContext, rbx) == 16, "x86_64 rbx offset mismatch");
static_assert(offsetof(FiberContext, rbp) == 24, "x86_64 rbp offset mismatch");
static_assert(offsetof(FiberContext, r12) == 32, "x86_64 r12 offset mismatch");
static_assert(offsetof(FiberContext, r13) == 40, "x86_64 r13 offset mismatch");
static_assert(offsetof(FiberContext, r14) == 48, "x86_64 r14 offset mismatch");
static_assert(offsetof(FiberContext, r15) == 56, "x86_64 r15 offset mismatch");
#elif defined(__aarch64__)
static_assert(sizeof(FiberContext) == 176, "aarch64 FiberContext layout mismatch");
static_assert(offsetof(FiberContext, sp) == 0, "aarch64 sp offset mismatch");
static_assert(offsetof(FiberContext, pc) == 8, "aarch64 pc offset mismatch");
static_assert(offsetof(FiberContext, x19) == 16, "aarch64 x19 offset mismatch");
static_assert(offsetof(FiberContext, x30) == 104, "aarch64 x30 offset mismatch");
static_assert(offsetof(FiberContext, d8) == 112, "aarch64 d8 offset mismatch");
static_assert(offsetof(FiberContext, d15) == 168, "aarch64 d15 offset mismatch");
#endif

} // namespace

/**
 * 线程局部存储变量，保存当前线程相关的协程信息
 * 这些变量是线程私有的，每个线程都有自己独立的副本
 */

// 当前正在运行的协程指针
static thread_local Fiber* t_fiber = nullptr;

// 主协程，每个线程的第一个协程，负责调度其他协程
static thread_local std::shared_ptr<Fiber> t_thread_fiber = nullptr;

// 调度协程指针，用于协程间切换回调度器
static thread_local Fiber* t_scheduler_fiber = nullptr;

// 共享栈池（按栈大小分桶）
static thread_local std::unordered_map<size_t, std::unique_ptr<SharedStackPool>> t_shared_stack_pools;

// 全局协程ID计数器，用于为每个协程分配唯一ID
static std::atomic<uint64_t> s_fiber_id{0};

// 当前系统中协程总数计数器
static std::atomic<uint64_t> s_fiber_count{0};

// 每个线程共享栈槽位数量
static std::atomic<size_t> s_shared_stack_slot_count{8};

static SharedStackPool* GetSharedStackPool(size_t stack_size) {
    auto it = t_shared_stack_pools.find(stack_size);
    if (it != t_shared_stack_pools.end()) {
        return it->second.get();
    }

    auto pool = std::make_unique<SharedStackPool>(
        s_shared_stack_slot_count.load(std::memory_order_relaxed), stack_size);
    SharedStackPool* raw_pool = pool.get();
    t_shared_stack_pools.emplace(stack_size, std::move(pool));
    return raw_pool;
}

/**
 * @brief 设置当前正在运行的协程
 * @param f 要设置为当前运行的协程指针
 */
void Fiber::SetThis(Fiber *f)
{
    t_fiber = f;
}

/**
 * @brief 获取当前运行的协程
 * @return 返回当前运行协程的智能指针
 * @details 如果当前没有协程在运行，则创建一个主协程
 *          主协程是线程运行的第一个协程，由操作系统调度
 */
std::shared_ptr<Fiber> Fiber::GetThis()
{
    if(t_fiber)
    {
        // 如果已经有协程在运行，返回该协程的智能指针
        return t_fiber->shared_from_this();
    }

    // 创建主协程
    std::shared_ptr<Fiber> main_fiber(new Fiber());
    t_thread_fiber = main_fiber;
    t_scheduler_fiber = main_fiber.get(); // 默认情况下，主协程也是调度协程

    assert(t_fiber == main_fiber.get());
    return t_fiber->shared_from_this();
}

/**
 * @brief 设置调度协程
 * @param f 调度协程指针
 * @details 调度协程负责调度其他协程的运行，通常是主协程或专门的调度器协程
 */
void Fiber::SetSchedulerFiber(Fiber* f)
{
    t_scheduler_fiber = f;
}

/**
 * @brief 获取当前运行的协程ID
 * @return 返回当前协程ID，如果没有协程运行则返回-1
 */
uint64_t Fiber::GetFiberId()
{
    if(t_fiber)
    {
        return t_fiber->getId();
    }
    return (uint64_t)-1;
}

void Fiber::SetSharedStackSlotCount(size_t slot_count)
{
    if (slot_count == 0) {
        slot_count = 1;
    }
    s_shared_stack_slot_count.store(slot_count, std::memory_order_relaxed);
}

size_t Fiber::GetSharedStackSlotCount()
{
    return s_shared_stack_slot_count.load(std::memory_order_relaxed);
}

void Fiber::initFiberContext()
{
    assert(m_stack != nullptr && m_stacksize > 0);
    InitFiberContextStack(m_ctx, m_stack, m_stacksize);
    m_ctxInitialized = true;
}

void Fiber::swapWithSanitizer(Fiber* from, Fiber* to)
{
    assert(from != nullptr);
    assert(to != nullptr);

    const bool has_sanitizer_hooks =
        (__sanitizer_start_switch_fiber != nullptr) &&
        (__sanitizer_finish_switch_fiber != nullptr);
    if (has_sanitizer_hooks)
    {
        const void* next_stack_bottom = nullptr;
        size_t next_stack_size = 0;
        if (to->m_stack != nullptr && to->m_stacksize > 0)
        {
            next_stack_bottom = to->m_stack;
            next_stack_size = to->m_stacksize;
        }
        __sanitizer_start_switch_fiber(&from->m_sanitizerFakeStack, next_stack_bottom, next_stack_size);
    }

    if (__tsan_switch_to_fiber != nullptr && to->m_tsanFiber != nullptr)
    {
        __tsan_switch_to_fiber(to->m_tsanFiber, 0);
    }

    mycoroutine_context_swap(&(from->m_ctx), &(to->m_ctx));

    if (has_sanitizer_hooks)
    {
        const void* old_stack_bottom = nullptr;
        size_t old_stack_size = 0;
        __sanitizer_finish_switch_fiber(from->m_sanitizerFakeStack, &old_stack_bottom, &old_stack_size);
    }
}

void Fiber::saveSharedStackSnapshot()
{
    if (!m_useSharedStack || !m_sharedStackSlot || !m_ctxInitialized) {
        return;
    }

    SharedStackSlot* slot = ToSharedStackSlot(m_sharedStackSlot);
    assert(slot != nullptr && slot->stack != nullptr);

    const uintptr_t stack_bottom = reinterpret_cast<uintptr_t>(slot->stack);
    const uintptr_t stack_top = stack_bottom + slot->size;
    const uintptr_t sp = GetContextStackPointer(m_ctx);

    assert(sp >= stack_bottom && sp <= stack_top);
    const size_t used = stack_top - sp;

    m_stackSnapshot.resize(used);
    if (used > 0) {
#if MYCOROUTINE_HAS_ASAN
        // Unpoison the shared stack region so we can snapshot it.
        // ASan plants redzone markers for local variables on this heap-backed
        // stack, which would otherwise trigger a false positive during memcpy.
        __asan_unpoison_memory_region(reinterpret_cast<void*>(sp), used);
#endif
        memcpy(m_stackSnapshot.data(), reinterpret_cast<void*>(sp), used);
    }
}

void Fiber::saveCurrentSharedStackSnapshot()
{
    if (!m_useSharedStack || !m_sharedStackSlot) {
        return;
    }

    SharedStackSlot* slot = ToSharedStackSlot(m_sharedStackSlot);
    assert(slot != nullptr && slot->stack != nullptr);

    const uintptr_t stack_bottom = reinterpret_cast<uintptr_t>(slot->stack);
    const uintptr_t stack_top = stack_bottom + slot->size;
    volatile char marker = 0;
    const uintptr_t sp = reinterpret_cast<uintptr_t>(&marker);

    assert(sp >= stack_bottom && sp <= stack_top);
    const size_t used = stack_top - sp;

    m_stackSnapshot.resize(used);
    if (used > 0) {
#if MYCOROUTINE_HAS_ASAN
        __asan_unpoison_memory_region(reinterpret_cast<const void*>(sp), used);
#endif
        memcpy(m_stackSnapshot.data(), reinterpret_cast<const void*>(sp), used);
    }
}

void Fiber::restoreSharedStackSnapshot()
{
    if (!m_useSharedStack || !m_sharedStackSlot || m_stackSnapshot.empty()) {
        return;
    }

    SharedStackSlot* slot = ToSharedStackSlot(m_sharedStackSlot);
    assert(slot != nullptr && slot->stack != nullptr);

    const uintptr_t stack_bottom = reinterpret_cast<uintptr_t>(slot->stack);
    const uintptr_t stack_top = stack_bottom + slot->size;
    const size_t used = m_stackSnapshot.size();

    assert(used <= slot->size);
    const uintptr_t restore_addr = stack_top - used;
    memcpy(reinterpret_cast<void*>(restore_addr), m_stackSnapshot.data(), used);
}

void Fiber::prepareSharedStack()
{
    assert(m_useSharedStack);

    const pid_t tid = Thread::GetThreadId();
    if (m_ownerThread == -1) {
        m_ownerThread = tid;
    } else {
        // 共享栈协程仅支持在绑定线程中执行
        assert(m_ownerThread == tid);
    }

    if (m_sharedStackSlot == nullptr) {
        SharedStackPool* pool = GetSharedStackPool(m_stacksize);
        SharedStackSlot* slot = pool->acquireSlot();
        assert(slot != nullptr);

        m_sharedStackSlot = slot;
        m_stack = slot->stack;
        m_stacksize = static_cast<uint32_t>(slot->size);
    }

    SharedStackSlot* slot = ToSharedStackSlot(m_sharedStackSlot);
    assert(slot != nullptr);

    if (!m_ctxInitialized) {
        m_stack = slot->stack;
        m_stacksize = static_cast<uint32_t>(slot->size);
        initFiberContext();
    }

    std::shared_ptr<Fiber> owner = slot->owner.lock();
    if (owner && owner.get() != this) {
        if (owner.get() == t_fiber) {
            owner->saveCurrentSharedStackSnapshot();
        } else {
            owner->saveSharedStackSnapshot();
        }
    }

    if (!owner || owner.get() != this) {
        restoreSharedStackSnapshot();
        slot->owner = shared_from_this();
    }
}

/**
 * @brief 主协程构造函数（私有）
 * @details 仅由GetThis()调用，创建线程的第一个协程
 *          主协程使用线程的栈空间，不需要额外分配
 */
Fiber::Fiber()
{
    // 设置当前协程为自己
    SetThis(this);

    // 主协程创建时处于运行状态
    m_state = RUNNING;

    m_ctx = FiberContext{};
    m_ctxInitialized = true;
    m_sanitizerFakeStack = nullptr;

    if (__tsan_get_current_fiber != nullptr)
    {
        m_tsanFiber = __tsan_get_current_fiber();
        m_ownsTsanFiber = false;
    }

    // 分配唯一ID并增加协程计数
    m_id = s_fiber_id++;
    s_fiber_count++;

    if(debug)
        std::cout << "Fiber(): main id = " << m_id << std::endl;
}

/**
 * @brief 子协程构造函数
 * @param cb 协程要执行的回调函数
 * @param stacksize 协程栈大小，默认为0（将使用默认值）
 * @param run_in_scheduler 是否在调度器中运行
 * @details 创建一个新的协程，分配栈空间并设置上下文
 */
Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler, bool use_shared_stack):
    m_cb(std::move(cb)), m_runInScheduler(run_in_scheduler), m_useSharedStack(use_shared_stack)
{
    // 初始状态为就绪态
    m_state = READY;

    // 分配协程栈空间，默认128KB
    m_stacksize = stacksize ? stacksize : 128000;

    if (!m_useSharedStack) {
        m_stack = malloc(m_stacksize);
        assert(m_stack != nullptr);
        initFiberContext();
    }

    m_sanitizerFakeStack = nullptr;
    if (__tsan_create_fiber != nullptr)
    {
        m_tsanFiber = __tsan_create_fiber(0);
        m_ownsTsanFiber = (m_tsanFiber != nullptr);
    }

    // 分配唯一ID并增加协程计数
    m_id = s_fiber_id++;
    s_fiber_count++;

    if(debug)
        std::cout << "Fiber(): child id = " << m_id << (m_useSharedStack ? " (shared stack)" : "") << std::endl;
}

/**
 * @brief 协程析构函数
 * @details 减少协程计数并释放栈空间
 */
Fiber::~Fiber()
{
    s_fiber_count--;

    if (m_ownsTsanFiber && m_tsanFiber && __tsan_destroy_fiber != nullptr)
    {
        __tsan_destroy_fiber(m_tsanFiber);
    }
    m_tsanFiber = nullptr;
    m_ownsTsanFiber = false;

    if (!m_useSharedStack && m_stack) {
        free(m_stack);
    }

    if(debug)
        std::cout << "~Fiber(): id = " << m_id << std::endl;
}

/**
 * @brief 重置协程函数
 * @param cb 新的协程回调函数
 * @details 重置一个已完成的协程，重新设置其回调函数和上下文
 *          仅当协程处于TERM状态时才能重置
 */
void Fiber::reset(std::function<void()> cb)
{
    // 只有已终止的协程才能重置
    assert(m_state == TERM);

    // 重置协程状态为就绪
    m_state = READY;
    m_cb = std::move(cb);
    m_stackSnapshot.clear();
    m_parent.reset();
    m_returnToParent = false;

    if (!m_useSharedStack) {
        assert(m_stack != nullptr);
        initFiberContext();
        return;
    }

    SharedStackSlot* slot = ToSharedStackSlot(m_sharedStackSlot);
    if (slot) {
        m_stack = slot->stack;
        m_stacksize = static_cast<uint32_t>(slot->size);
        initFiberContext();
    } else {
        m_ctxInitialized = false;
    }
}

/**
 * @brief 恢复协程执行
 * @details 从当前协程切换到该协程继续执行
 *          只能恢复处于READY状态的协程
 */
void Fiber::resume()
{
    // 确保协程处于就绪状态
    assert(m_state == READY);

    if (m_useSharedStack) {
        prepareSharedStack();
    }

    // 将协程状态设置为运行中
    m_state = RUNNING;

    if(m_runInScheduler)
    {
        // 如果协程在调度器中运行，则切换到调度协程
        assert(t_scheduler_fiber != nullptr);
        SetThis(this);
        swapWithSanitizer(t_scheduler_fiber, this);
    }
    else
    {
        // 如果协程不在调度器中运行，则切换到主协程
        assert(t_thread_fiber != nullptr);
        SetThis(this);
        swapWithSanitizer(t_thread_fiber.get(), this);
    }
}

/**
 * @brief 在当前协程上下文内同步调用该协程
 * @details 当前协程作为父协程，目标协程作为子协程执行；子协程yield或结束后返回父协程
 */
int Fiber::call()
{
    if (m_state != READY) {
        return CALL_ERR_NOT_READY;
    }

    std::shared_ptr<Fiber> parent_holder = Fiber::GetThis();
    Fiber* parent = parent_holder.get();
    if (parent == nullptr) {
        return CALL_ERR_NO_CURRENT_FIBER;
    }
    if (parent == this) {
        return CALL_ERR_SELF_CALL;
    }
    // TODO(lihao): 嵌套与共享栈的深度融合需单独处理父协程运行时快照时机。
    if (m_useSharedStack && parent->m_useSharedStack) {
        return CALL_ERR_SHARED_NESTED_UNSUPPORTED;
    }

    if (m_useSharedStack) {
        prepareSharedStack();
    }

    m_parent = parent->shared_from_this();
    m_returnToParent = true;
    m_state = RUNNING;

    SetThis(this);
    swapWithSanitizer(parent, this);

    // 回到父协程后，清理本次调用关系，避免悬挂父指针
    m_parent.reset();
    m_returnToParent = false;
    return CALL_OK;
}

/**
 * @brief 协程让出执行权
 * @details 暂停当前协程的执行，将控制权交回调用者
 *          可以是运行中的协程主动让出，也可以是已完成的协程自动让出
 */
void Fiber::yield()
{
    // 确保协程处于运行中或已终止状态
    assert(m_state == RUNNING || m_state == TERM);

    // 如果协程未终止，则设置状态为就绪
    if(m_state != TERM)
    {
        m_state = READY;
    }

    // 协程嵌套：优先返回父协程
    if (m_returnToParent && !m_parent.expired()) {
        back();
        return;
    }

    if(m_runInScheduler)
    {
        // 如果协程在调度器中运行，则切换回调度协程
        assert(t_scheduler_fiber != nullptr);
        SetThis(t_scheduler_fiber);
        swapWithSanitizer(this, t_scheduler_fiber);
    }
    else
    {
        // 如果协程不在调度器中运行，则切换回主协程
        assert(t_thread_fiber != nullptr);
        SetThis(t_thread_fiber.get());
        swapWithSanitizer(this, t_thread_fiber.get());
    }
}

void Fiber::back()
{
    auto parent = m_parent.lock();
    assert(parent && "parent fiber expired in back()");

    m_parent.reset();
    m_returnToParent = false;

    // 共享栈嵌套切换：切回父协程前先恢复父协程栈快照
    if (parent->m_useSharedStack) {
        parent->prepareSharedStack();
    }

    SetThis(parent.get());
    swapWithSanitizer(this, parent.get());
}

/**
 * @brief 协程入口函数
 * @details 所有协程的入口点，负责执行协程回调函数并在完成后让出执行权
 *          使用智能指针确保协程在执行过程中不会被销毁
 */
void Fiber::MainFunc()
{
    // 获取当前协程的智能指针，延长其生命周期
    std::shared_ptr<Fiber> curr = GetThis();
    assert(curr != nullptr);

    // Complete the ASan fiber switch that was started by the caller's
    // swapWithSanitizer().  Without this, ASan thinks we are still mid-switch
    // and the next start_switch_fiber (in yield()) would abort.
    if (__sanitizer_finish_switch_fiber != nullptr)
    {
        const void* old_bottom = nullptr;
        size_t old_size = 0;
        __sanitizer_finish_switch_fiber(
            curr->m_sanitizerFakeStack, &old_bottom, &old_size);
    }

    try
    {
        // 执行协程回调函数
        curr->m_cb();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Fiber::MainFunc caught exception: " << ex.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Fiber::MainFunc caught unknown exception" << std::endl;
    }

    // 执行完成后清除回调函数，避免循环引用
    curr->m_cb = nullptr;

    // 设置协程状态为已终止
    curr->m_state = TERM;

    // 保存裸指针，因为接下来要重置智能指针
    auto raw_ptr = curr.get();

    // 释放智能指针引用，减少引用计数
    curr.reset();

    // 让出执行权，返回到调用者协程
    raw_ptr->yield();

    // The control flow should never continue after yielding out.
    assert(false && "Fiber::MainFunc should not return after yield()");
}

}
