#pragma once

#include <cstdlib>
#include <iostream>
#include <thread>

namespace core {

inline std::thread::id& MainThreadId() {
    static std::thread::id id;
    return id;
}

inline void InitMainThread() {
    MainThreadId() = std::this_thread::get_id();
}

inline bool IsMainThread() {
    return MainThreadId() == std::this_thread::get_id();
}

} // namespace core

#ifndef NDEBUG
#define MC_ASSERT(condition, message)                                                         \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << "[Assert] " << (message) << " (" << __FILE__ << ":" << __LINE__       \
                      << ")\n";                                                                \
            std::abort();                                                                      \
        }                                                                                      \
    } while (false)

#define MC_ASSERT_MAIN_THREAD_GL()                                                             \
    do {                                                                                       \
        if (!core::IsMainThread()) {                                                           \
            std::cerr << "[Assert] OpenGL call on non-main thread (" << __FILE__ << ":"         \
                      << __LINE__ << ")\n";                                                    \
            std::abort();                                                                      \
        }                                                                                      \
    } while (false)
#else
#define MC_ASSERT(condition, message) do { (void)sizeof(condition); } while (false)
#define MC_ASSERT_MAIN_THREAD_GL() do {} while (false)
#endif
