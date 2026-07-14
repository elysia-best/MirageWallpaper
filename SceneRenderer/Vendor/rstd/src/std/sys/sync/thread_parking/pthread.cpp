module;
#include <rstd/macro.hpp>
module rstd;
import :sys.sync.thread_parking.pthread;

#if RSTD_OS_UNIX && !RSTD_OS_LINUX

using rstd::sync::atomic::Ordering;

namespace rstd::sys::sync::thread_parking::pthread
{

Parker::Parker(): state(EMPTY), lock(sys::pal::Mutex::make()), cvar(Condvar::make()) {}

void Parker::park() {
    // Try fast path for already-notified state
    usize expected = NOTIFIED;
    if (state.compare_exchange_strong(expected, EMPTY, Ordering::Acquire, Ordering::Relaxed)) {
        return;
    }

    lock.lock();

    expected = EMPTY;
    if (! state.compare_exchange_strong(expected, PARKED, Ordering::Relaxed, Ordering::Relaxed)) {
        if (expected == NOTIFIED) {
            // Must read here for synchronization, see Rust comments
            [[maybe_unused]]
            usize old = state.exchange(EMPTY, Ordering::Acquire);
            rstd_assert(old == NOTIFIED, "park state changed unexpectedly");
            // NOTE: must release the lock taken above before returning —
            // leaking it here (the original code did) leaves the mutex
            // permanently held, so the *next* park() on this Parker
            // deadlocks at lock.lock(). On Linux this path is unused
            // (futex::Parker is selected instead), which hid the bug.
            lock.unlock();
            return;
        }
        lock.unlock();
        panic { "inconsistent park state" };
    }

    while (true) {
        cvar.wait(lock);

        expected = NOTIFIED;
        if (state.compare_exchange_strong(expected, EMPTY, Ordering::Acquire, Ordering::Relaxed)) {
            break;
        }
    }
    // cvar.wait reacquires the mutex on wakeup — release it before
    // returning so the next park()/unpark() can acquire it.
    lock.unlock();
}

void Parker::park_timeout(rstd::time::Duration dur) {
    usize expected = NOTIFIED;
    if (state.compare_exchange_strong(expected, EMPTY, Ordering::Acquire, Ordering::Relaxed)) {
        return;
    }

    lock.lock();
    expected = EMPTY;
    if (! state.compare_exchange_strong(expected, PARKED, Ordering::Acquire, Ordering::Relaxed)) {
        if (expected == NOTIFIED) {
            [[maybe_unused]]
            usize old = state.exchange(EMPTY, Ordering::Acquire);
            rstd_assert(old == NOTIFIED, "park state changed unexpectedly");
            lock.unlock();
            return;
        }
        lock.unlock();
        panic { "inconsistent park_timeout state" };
    }

    cvar.wait_timeout(lock,
                      (i64)(dur.as_secs() * u64(rstd::time::NANOS_PER_SEC)) + (i64)dur.subsec_nanos());

    usize old = state.exchange(EMPTY, Ordering::Acquire);
    // Whether we were notified or timed out, release the lock reacquired
    // by wait_timeout before returning.
    lock.unlock();
    if (old != NOTIFIED && old != PARKED) {
        panic("inconsistent park_timeout state: {}", old);
    }
}

void Parker::unpark() {
    usize prev = state.exchange(NOTIFIED, Ordering::Relaxed);

    switch (prev) {
    case EMPTY:
    case NOTIFIED: return;
    case PARKED: break;
    default: panic { "inconsistent state in unpark" };
    }

    // Lock to ensure proper synchronization with the parking thread
    lock.lock();
    lock.unlock();
    cvar.notify_one();
}

} // namespace rstd::sys::sync::thread_parking::pthread
#endif
