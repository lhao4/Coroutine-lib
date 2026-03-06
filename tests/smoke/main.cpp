#include <cassert>
#include <mycoroutine/fiber.h>

int main() {
    auto main_fiber = mycoroutine::Fiber::GetThis();
    assert(main_fiber);
    assert(main_fiber->getState() == mycoroutine::Fiber::RUNNING);
    return 0;
}
