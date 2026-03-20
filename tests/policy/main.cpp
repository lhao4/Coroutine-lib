#include <cassert>
#include <vector>
#include <memory>

#include <mycoroutine/scheduler.h>

int main() {
    using mycoroutine::ScheduleOptions;
    using mycoroutine::SchedulePolicy;
    using mycoroutine::Scheduler;
    using mycoroutine::Fiber;

    {
        std::vector<int> order;
        Scheduler sc(1, true, "policy_priority");
        sc.setPolicy(SchedulePolicy::PRIORITY);

        ScheduleOptions low;
        low.priority = 1;
        sc.scheduleEx([&order]() { order.push_back(1); }, low);

        ScheduleOptions high;
        high.priority = 10;
        sc.scheduleEx([&order]() { order.push_back(2); }, high);

        ScheduleOptions mid;
        mid.priority = 5;
        sc.scheduleEx([&order]() { order.push_back(3); }, mid);

        sc.stop();

        const std::vector<int> expected = {2, 3, 1};
        assert(order == expected);
    }

    {
        std::vector<int> order;
        Scheduler sc(1, true, "policy_edf");
        sc.setPolicy(SchedulePolicy::EDF);

        ScheduleOptions late;
        late.deadline_ms = 300;
        sc.scheduleEx([&order]() { order.push_back(1); }, late);

        ScheduleOptions early;
        early.deadline_ms = 100;
        sc.scheduleEx([&order]() { order.push_back(2); }, early);

        ScheduleOptions none;
        none.deadline_ms = 0;
        sc.scheduleEx([&order]() { order.push_back(3); }, none);

        sc.stop();

        const std::vector<int> expected = {2, 1, 3};
        assert(order == expected);
    }

    {
        std::vector<int> order;
        Scheduler sc(1, true, "policy_hybrid");
        sc.setPolicy(SchedulePolicy::HYBRID);

        ScheduleOptions low;
        low.priority = 1;
        sc.scheduleEx([&order]() { order.push_back(1); }, low);

        ScheduleOptions high;
        high.priority = 9;
        sc.scheduleEx([&order]() { order.push_back(2); }, high);

        ScheduleOptions deadline;
        deadline.priority = 0;
        deadline.deadline_ms = 50;
        sc.scheduleEx([&order]() { order.push_back(3); }, deadline);

        sc.stop();

        const std::vector<int> expected = {3, 2, 1};
        assert(order == expected);
    }

    {
        std::vector<int> order;
        Scheduler sc(1, true, "policy_mlfq");
        sc.setPolicy(SchedulePolicy::MLFQ);
        sc.setMLFQConfig({3, true, true, 64});

        auto long_task = std::make_shared<Fiber>([&order]() {
            order.push_back(1);
            Fiber::GetThis()->yield();
            order.push_back(4);
            Fiber::GetThis()->yield();
            order.push_back(5);
        });

        auto short_a = std::make_shared<Fiber>([&order]() {
            order.push_back(2);
        });

        auto short_b = std::make_shared<Fiber>([&order]() {
            order.push_back(3);
        });

        sc.scheduleEx(long_task);
        sc.scheduleEx(short_a);
        sc.scheduleEx(short_b);

        sc.stop();

        const std::vector<int> expected = {1, 2, 3, 4, 5};
        assert(order == expected);
    }

    {
        Scheduler sc(1, true, "policy_mlfq_config");
        sc.setPolicy(SchedulePolicy::MLFQ);

        mycoroutine::MLFQConfig cfg;
        cfg.levels = 0;               // 非法输入，内部应归一化为1
        cfg.demote_on_yield = false;
        cfg.enable_aging = true;
        cfg.aging_sequence_gap = 0;   // 非法输入，内部应归一化为1
        sc.setMLFQConfig(cfg);

        const auto got = sc.getMLFQConfig();
        assert(got.levels == 1);
        assert(got.demote_on_yield == false);
        assert(got.enable_aging == true);
        assert(got.aging_sequence_gap == 1);

        sc.stop();
    }

    return 0;
}
