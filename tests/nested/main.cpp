#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include <mycoroutine/fiber.h>

int main() {
    using mycoroutine::Fiber;

    Fiber::GetThis();

    {
        std::vector<std::string> trace;
        auto child = std::make_shared<Fiber>(
            [&trace]() {
                trace.push_back("child-1");
                assert(Fiber::GetThis()->parent() != nullptr);
                Fiber::GetThis()->yield();
                trace.push_back("child-2");
            },
            0,
            false,
            false);

        trace.push_back("parent-0");
        child->call();
        trace.push_back("parent-1");
        assert(child->getState() == Fiber::READY);

        child->call();
        trace.push_back("parent-2");
        assert(child->getState() == Fiber::TERM);

        const std::vector<std::string> expected = {
            "parent-0", "child-1", "parent-1", "child-2", "parent-2"};
        assert(trace == expected);
    }

    {
        std::vector<std::string> trace;

        auto grand = std::make_shared<Fiber>(
            [&trace]() {
                trace.push_back("grand-1");
                assert(Fiber::GetThis()->parent() != nullptr);
                Fiber::GetThis()->yield();
                trace.push_back("grand-2");
            },
            0,
            false,
            false);

        auto mid = std::make_shared<Fiber>(
            [&trace, &grand]() {
                trace.push_back("mid-1");
                grand->call();
                trace.push_back("mid-2");
                grand->call();
                trace.push_back("mid-3");
            },
            0,
            false,
            false);

        trace.push_back("root-1");
        mid->call();
        trace.push_back("root-2");

        assert(mid->getState() == Fiber::TERM);
        assert(grand->getState() == Fiber::TERM);

        const std::vector<std::string> expected = {
            "root-1", "mid-1", "grand-1", "mid-2", "grand-2", "mid-3", "root-2"};
        assert(trace == expected);
    }

    return 0;
}
