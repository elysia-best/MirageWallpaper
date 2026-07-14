module;
#include <rstd/macro.hpp>

export module rstd:sys.sync.condvar;
#if !defined(RSTD_OS_APPLE)
export import :sys.sync.condvar.futex;
#else
export import :sys.pal.unix.sync.condvar;
#endif

namespace rstd::sys::sync::condvar
{

#if !defined(RSTD_OS_APPLE)
export using condvar::futex::Condvar;
#else
export using pal::unix::sync::condvar::Condvar;
#endif

} // namespace rstd::sys::sync::condvar
