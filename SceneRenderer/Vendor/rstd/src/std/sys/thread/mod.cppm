module;
#include <rstd/macro.hpp>
export module rstd:sys.thread;

#if RSTD_OS_LINUX || RSTD_OS_APPLE
export import :sys.thread.unix;
#elif RSTD_OS_WINDOWS
export import :sys.thread.windows;
#endif

namespace rstd::sys::thread
{
#if RSTD_OS_LINUX || RSTD_OS_APPLE
using unix::Thread;
#elif RSTD_OS_WINDOWS
using windows::Thread;
#endif
} // namespace rstd::sys::thread
