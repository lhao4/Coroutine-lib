#ifndef __MYCOROUTINE_CONTEXT_H_
#define __MYCOROUTINE_CONTEXT_H_

#include <cstddef>
#include <cstdint>

namespace mycoroutine {

#if defined(__x86_64__)

struct FiberContext
{
    void* rsp = nullptr;
    void* rip = nullptr;
    std::uint64_t rbx = 0;
    std::uint64_t rbp = 0;
    std::uint64_t r12 = 0;
    std::uint64_t r13 = 0;
    std::uint64_t r14 = 0;
    std::uint64_t r15 = 0;
};

constexpr std::size_t kFiberContextStackAlignment = 16;
constexpr std::size_t kFiberContextEntryStackAdjust = 8;

#elif defined(__aarch64__)

struct FiberContext
{
    void* sp = nullptr;
    void* pc = nullptr;
    std::uint64_t x19 = 0;
    std::uint64_t x20 = 0;
    std::uint64_t x21 = 0;
    std::uint64_t x22 = 0;
    std::uint64_t x23 = 0;
    std::uint64_t x24 = 0;
    std::uint64_t x25 = 0;
    std::uint64_t x26 = 0;
    std::uint64_t x27 = 0;
    std::uint64_t x28 = 0;
    std::uint64_t x29 = 0;
    std::uint64_t x30 = 0;
    // AAPCS64: d8-d15 are callee-saved.
    std::uint64_t d8 = 0;
    std::uint64_t d9 = 0;
    std::uint64_t d10 = 0;
    std::uint64_t d11 = 0;
    std::uint64_t d12 = 0;
    std::uint64_t d13 = 0;
    std::uint64_t d14 = 0;
    std::uint64_t d15 = 0;
};

constexpr std::size_t kFiberContextStackAlignment = 16;
constexpr std::size_t kFiberContextEntryStackAdjust = 0;

#else

#error "mycoroutine currently supports x86_64/aarch64 for assembly context switch"

#endif

} // namespace mycoroutine

extern "C" void mycoroutine_context_swap(mycoroutine::FiberContext* from,
                                          mycoroutine::FiberContext* to);

#endif
