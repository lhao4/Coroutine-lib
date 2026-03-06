// ============================================================================
// 定时器模块实现文件
// 实现了Timer和TimerManager类的具体功能，包括定时器的创建、取消、刷新、重置以及
// 定时器管理器的添加、查询、过期处理等核心功能
// ============================================================================
#include <mycoroutine/timer.h>

namespace mycoroutine {

// ============================================================================
// Timer类方法实现
// ============================================================================

// ============================================================================
// 取消定时器
// 从定时器管理器的时间堆中移除该定时器
// @return 取消成功返回true，失败返回false
// ============================================================================
bool Timer::cancel() 
{
    // 获取写锁以保护共享数据
    std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);

    // 检查回调函数是否为空（可能已被取消）
    if(m_cb == nullptr) 
    {        
        return false;    
    }
    else
    {        
        m_cb = nullptr;  // 清空回调函数
    }

    // 从定时器管理器的时间堆中查找并删除该定时器
    auto it = m_manager->m_timers.find(shared_from_this());
    if(it != m_manager->m_timers.end())
    {
        m_manager->m_timers.erase(it);
    }
    return true;
}

// ============================================================================
// 刷新定时器
// 将定时器的下一次超时时间调整为当前时间加上定时时间
// @return 刷新成功返回true，失败返回false
// ============================================================================
bool Timer::refresh() 
{
    // 获取写锁以保护共享数据
    std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);

    // 检查回调函数是否存在
    if(!m_cb) 
    {
        return false;
    }

    // 查找定时器在堆中的位置
    auto it = m_manager->m_timers.find(shared_from_this());
    if(it == m_manager->m_timers.end())
    {
        return false;
    }

    // 从时间堆中移除定时器
    m_manager->m_timers.erase(it);
    // 更新下一次超时时间为当前时间加上定时毫秒数
    m_next = std::chrono::system_clock::now() + std::chrono::milliseconds(m_ms);
    // 重新插入到时间堆中
    m_manager->m_timers.insert(shared_from_this());
    return true;
}

// ============================================================================
// 重设定时器
// 修改定时器的超时时间，并可选择是否从当前时间开始计时
// @param ms 新的超时时间（毫秒）
// @param from_now 是否从当前时间开始计算超时时间
// @return 重置成功返回true，失败返回false
// ============================================================================
bool Timer::reset(uint64_t ms, bool from_now) 
{
    // 如果超时时间相同且不从现在开始，无需重置
    if(ms == m_ms && !from_now)
    {
        return true;
    }

    {
        // 获取写锁以保护共享数据
        std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);
        
        // 检查回调函数是否存在
        if(!m_cb) 
        {            
            return false;        
        }
        
        // 查找定时器在堆中的位置
        auto it = m_manager->m_timers.find(shared_from_this());
        if(it == m_manager->m_timers.end())
        {            
            return false;        
        }   
        // 从时间堆中移除定时器
        m_manager->m_timers.erase(it); 
    }

    // 计算新的起始时间点
    auto start = from_now ? std::chrono::system_clock::now() : m_next - std::chrono::milliseconds(m_ms);
    // 更新超时时间
    m_ms = ms;
    // 计算新的下一次超时时间
    m_next = start + std::chrono::milliseconds(m_ms);
    // 重新添加到定时器管理器中（内部会加锁）
    m_manager->addTimer(shared_from_this()); 
    return true;
}

// ============================================================================
// Timer构造函数
// 初始化定时器的基本属性
// @param ms 超时时间（毫秒）
// @param cb 超时回调函数
// @param recurring 是否循环执行
// @param manager 所属的定时器管理器
// ============================================================================
Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager):
    m_recurring(recurring), m_ms(ms), m_cb(cb), m_manager(manager) 
{
    // 计算下一次超时时间点
    auto now = std::chrono::system_clock::now();
    m_next = now + std::chrono::milliseconds(m_ms);
}

// ============================================================================
// 定时器比较函数
// 用于在set中保持定时器按超时时间排序
// @param lhs 左侧定时器
// @param rhs 右侧定时器
// @return 左侧定时器的超时时间早于右侧则返回true
// ============================================================================
bool Timer::Comparator::operator()(const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const
{
    assert(lhs != nullptr && rhs != nullptr);
    return lhs->m_next < rhs->m_next;
}

// ============================================================================
// TimerManager类方法实现
// ============================================================================

// ============================================================================
// TimerManager构造函数
// 初始化定时器管理器，记录当前时间作为上次检查时间
// ============================================================================
TimerManager::TimerManager() 
{
    m_previouseTime = std::chrono::system_clock::now();
}

// ============================================================================
// TimerManager析构函数
// 虚析构函数，便于派生类正确析构
// ============================================================================
TimerManager::~TimerManager() 
{
}

// ============================================================================
// 添加定时器
// 创建并添加一个新的定时器到管理器
// @param ms 超时时间（毫秒）
// @param cb 超时回调函数
// @param recurring 是否循环执行
// @return 创建的定时器智能指针
// ============================================================================
std::shared_ptr<Timer> TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring) 
{
    // 创建新的定时器对象
    std::shared_ptr<Timer> timer(new Timer(ms, cb, recurring, this));
    // 添加到管理器中
    addTimer(timer);
    return timer;
}

// ============================================================================
// 条件定时器回调包装函数
// 只有当条件对象仍然存在时才执行回调函数
// @param weak_cond 条件对象的弱引用
// @param cb 原始回调函数
// ============================================================================
static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb)
{
    // 尝试获取条件对象的强引用
    std::shared_ptr<void> tmp = weak_cond.lock();
    // 如果条件对象仍然存在，则执行回调
    if(tmp)
    {
        cb();
    }
}

// ============================================================================
// 添加条件定时器
// 创建一个带条件的定时器，当条件不再满足时不会执行回调
// @param ms 超时时间（毫秒）
// @param cb 超时回调函数
// @param weak_cond 条件对象的弱引用
// @param recurring 是否循环执行
// @return 创建的定时器智能指针
// ============================================================================
std::shared_ptr<Timer> TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring) 
{
    // 使用条件包装函数创建定时器
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

// ============================================================================
// 获取下一个定时器的超时时间
// 计算距离下一个定时器超时还剩多少毫秒
// @return 下一个定时器的剩余超时时间（毫秒），如果没有定时器则返回最大值
// ============================================================================
uint64_t TimerManager::getNextTimer()
{
    // 获取读锁以保护共享数据
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    
    // 重置m_tickled标志，表示已查询过下一次超时时间
    m_tickled = false;
    
    // 如果定时器队列为空，返回最大值表示无定时器
    if (m_timers.empty())
    {
        return ~0ull;
    }

    // 获取当前时间和最近的超时时间
    auto now = std::chrono::system_clock::now();
    auto time = (*m_timers.begin())->m_next;

    // 判断是否有定时器已经超时
    if(now >= time)
    {
        // 已经有定时器超时，返回0
        return 0;
    }
    else
    {
        // 计算剩余超时时间并返回
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(time - now);
        return static_cast<uint64_t>(duration.count());            
    }  
}

// ============================================================================
// 获取所有过期的定时器回调函数
// 查找所有已超时的定时器，并将其回调函数收集到cbs中
// @param cbs 用于存储过期定时器回调函数的容器
// ============================================================================
void TimerManager::listExpiredCb(std::vector<std::function<void()>>& cbs)
{
    // 获取当前时间
    auto now = std::chrono::system_clock::now();

    // 获取写锁以保护共享数据
    std::unique_lock<std::shared_mutex> write_lock(m_mutex); 

    // 检测系统时间是否回退
    bool rollover = detectClockRollover();
    
    // 循环处理所有过期的定时器
    // 如果系统时间回退或有定时器超时，则继续处理
    while (!m_timers.empty() && 
           (rollover || (*m_timers.begin())->m_next <= now))
    {
        // 获取最早超时的定时器
        std::shared_ptr<Timer> temp = *m_timers.begin();
        // 从时间堆中移除
        m_timers.erase(m_timers.begin());
        
        // 将回调函数添加到结果容器中
        cbs.push_back(temp->m_cb); 

        // 如果是循环定时器，重新计算超时时间并加入时间堆
        if (temp->m_recurring)
        {
            temp->m_next = now + std::chrono::milliseconds(temp->m_ms);
            m_timers.insert(temp);
        }
        else
        {
            // 非循环定时器，清空回调函数
            temp->m_cb = nullptr;
        }
    }
}

// ============================================================================
// 判断管理器中是否有定时器
// @return 有定时器返回true，否则返回false
// ============================================================================
bool TimerManager::hasTimer() 
{
    // 获取读锁以保护共享数据
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    return !m_timers.empty();
}

// ============================================================================
// 添加定时器（内部方法）
// 将定时器添加到时间堆中，并在必要时唤醒等待线程
// @param timer 要添加的定时器
// ============================================================================
void TimerManager::addTimer(std::shared_ptr<Timer> timer)
{
    bool at_front = false;
    {
        // 获取写锁以保护共享数据
        std::unique_lock<std::shared_mutex> write_lock(m_mutex);
        // 插入定时器并获取迭代器
        auto it = m_timers.insert(timer).first;
        // 检查是否插入到了堆顶且尚未触发tickle
        at_front = (it == m_timers.begin()) && !m_tickled;
        
        // 设置tickle标志，确保在下次getNextTimer()之前只触发一次onTimerInsertedAtFront()
        if(at_front)
        {
            m_tickled = true;
        }
    }
   
    // 如果定时器被插入到堆顶，调用钩子函数
    if(at_front)
    {
        onTimerInsertedAtFront();
    }
}

// ============================================================================
// 检测系统时钟回退
// 检查系统时间是否发生回退（可能由NTP校正或手动调整引起）
// @return 系统时间回退返回true，否则返回false
// ============================================================================
bool TimerManager::detectClockRollover() 
{
    bool rollover = false;
    // 获取当前时间
    auto now = std::chrono::system_clock::now();
    // 判断系统时间是否回退超过1小时（60*60*1000毫秒）
    if(now < (m_previouseTime - std::chrono::milliseconds(60 * 60 * 1000))) 
    {
        rollover = true;
    }
    // 更新上次检查时间
    m_previouseTime = now;
    return rollover;
}

} // namespace mycoroutine