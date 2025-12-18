#pragma once

#include <chrono>
#include <cstdint>

#if defined(_MSC_VER)
  #include <intrin.h>      // __rdtsc on MSVC
  static inline uint64_t rdtsc() { return __rdtsc(); }
  #if defined(_WIN32)
    #include <windows.h>
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #if defined(__x86_64__) || defined(__i386__)
    #include <x86intrin.h> // __rdtsc on GCC/Clang x86/x64
    static inline uint64_t rdtsc() { return __rdtsc(); }
  #else
    // Fallback: use high_resolution_clock if rdtsc isn't available
    static inline uint64_t rdtsc() {
        return (uint64_t)std::chrono::high_resolution_clock::now()
                 .time_since_epoch().count();
    }
  #endif
  #if defined(_WIN32)
    #include <windows.h>
  #endif
#else
  // conservative fallback
  static inline uint64_t rdtsc() {
      return (uint64_t)std::chrono::high_resolution_clock::now()
               .time_since_epoch().count();
  }
  #if defined(_WIN32)
    #include <windows.h>
  #endif
#endif

using ns = std::chrono::nanoseconds;
using HighResClock = std::chrono::high_resolution_clock; // avoid 'clock' name

struct Timer {
    uint64_t start_cycles;
    HighResClock::time_point start_time;

    Timer() { reset(); }
    void reset() {
        start_cycles = rdtsc();
        start_time = HighResClock::now();
    }

    uint64_t cycles() const { return rdtsc() - start_cycles; }
    uint64_t nanoseconds() const {
        return std::chrono::duration_cast<ns>(HighResClock::now() - start_time).count();
    }
};

// Optional: set high priority on Windows
inline void SetHighPriority() {
#if defined(_WIN32)
    (void)SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
}
