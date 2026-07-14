module;
#include <rstd/macro.hpp>
export module rstd:sys.sync.mutex;
#if !defined(RSTD_OS_APPLE)
export import :sys.sync.mutex.futex;
#else
export import :sys.pal.unix.sync.mutex;
#endif
export import :sys.sync.mutex.pthread;

namespace rstd::sys::sync::mutex
{
// futex(2) is Linux-only at the syscall level (rstd's futex pal::futex_*
// are RSTD_OS_LINUX-gated in src/std/sys/pal/unix/futex.cpp). On Apple
// fall back to the pthread-backed Mutex so the futex symbols don't have
// to link.
#if RSTD_OS_LINUX || RSTD_OS_WINDOWS
export using mutex::futex::Mutex;
#else
export using pal::unix::sync::mutex::Mutex;
#endif
}
