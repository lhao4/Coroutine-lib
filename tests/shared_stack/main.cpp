#include <cassert>
#include <cstring>
#include <memory>
#include <vector>

#include <mycoroutine/fiber.h>

int main() {
    using mycoroutine::Fiber;

    Fiber::SetSharedStackSlotCount(1);
    Fiber::GetThis();

    constexpr int kFiberCount = 8;
    constexpr int kRounds = 4;

    std::vector<std::shared_ptr<Fiber>> fibers;
    fibers.reserve(kFiberCount);
    std::vector<int> finished(kFiberCount, 0);

    for (int i = 0; i < kFiberCount; ++i) {
        fibers.emplace_back(std::make_shared<Fiber>(
            [i, &finished]() {
                for (int round = 0; round < kRounds; ++round) {
                    char guard[512];
                    const unsigned char pattern = static_cast<unsigned char>((i + 1) * 17 + round);
                    std::memset(guard, static_cast<int>(pattern), sizeof(guard));

                    Fiber::GetThis()->yield();

                    for (size_t idx = 0; idx < sizeof(guard); ++idx) {
                        assert(static_cast<unsigned char>(guard[idx]) == pattern);
                    }
                }
                finished[i] = 1;
            },
            64 * 1024,
            false,
            true));
    }

    bool all_term = false;
    while (!all_term) {
        all_term = true;
        for (auto& fiber : fibers) {
            if (fiber->getState() != Fiber::TERM) {
                all_term = false;
                fiber->resume();
            }
        }
    }

    for (int i = 0; i < kFiberCount; ++i) {
        assert(finished[i] == 1);
    }

    return 0;
}
