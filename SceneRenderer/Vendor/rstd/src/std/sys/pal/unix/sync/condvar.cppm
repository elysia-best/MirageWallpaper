module;
#include <rstd/macro.hpp>
export module rstd:sys.pal.unix.sync.condvar;

#if RSTD_OS_UNIX
export import :sys.libc.pthread;
export import :sys.libc.std;
export import :sys.libc.unix;
export import :sys.pal.unix.sync.mutex;
export import rstd.core;

using namespace rstd::sys::libc;
using namespace rstd::sys::pal::unix::sync::mutex;

namespace rstd::sys::pal::unix::sync::condvar
{

export class Condvar {
    pthread_cond_t inner;
#ifdef RSTD_OS_APPLE
    // See pal::Mutex on Apple for rationale — pthread_cond_t is the same
    // kind of address-sensitive object as pthread_mutex_t. Eager-init
    // at the cvar's final address; the `inited` flag tracks whether
    // `inner` still holds a live pthread_cond_t that needs destruction.
    bool inited;
#endif

    Condvar() noexcept
#ifdef RSTD_OS_APPLE
        : inner{}, inited(true) {
        [[maybe_unused]] auto r = pthread_cond_init(&inner, nullptr);
        debug_assert_eq(r, 0);
    }
#else
        : inner(pthread_cond_initializer()) {}
#endif

public:
    ~Condvar() noexcept {
#ifdef RSTD_OS_APPLE
        if (inited) {
            [[maybe_unused]] auto r = pthread_cond_destroy(&inner);
            debug_assert_eq(r, 0);
        }
#else
        [[maybe_unused]] auto r = pthread_cond_destroy(&inner);
        debug_assert_eq(r, 0);
#endif
    }

#ifdef RSTD_OS_APPLE
    Condvar(Condvar&& o) noexcept: inner{}, inited(false) {
        if (o.inited) {
            pthread_cond_destroy(&o.inner);
            o.inited = false;
        }
        [[maybe_unused]] auto r = pthread_cond_init(&inner, nullptr);
        debug_assert_eq(r, 0);
        inited = true;
    }
#else
    Condvar(Condvar&& o) noexcept: inner(o.inner) {}
#endif

    Condvar(const Condvar&)            = delete;
    Condvar& operator=(const Condvar&) = delete;
    Condvar& operator=(Condvar&&)      = delete;

    static auto make() noexcept -> Condvar { return {}; }

    auto raw() noexcept -> pthread_cond_t* { return &inner; }

    // Initialize the condition variable with CLOCK_MONOTONIC
    void init() noexcept {
#ifdef RSTD_OS_APPLE
        // Already pthread_cond_init'd at construction time with default
        // attrs (macOS has no pthread_condattr_setclock anyway). wait_timeout
        // below uses CLOCK_REALTIME to compute the deadline. Nothing to do.
#else
        pthread_condattr_t attr;
        auto r = pthread_condattr_init(&attr);
        rstd_assert_eq(r, 0);

        r = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        rstd_assert_eq(r, 0);

        r = pthread_cond_init(raw(), &attr);
        rstd_assert_eq(r, 0);

        r = pthread_condattr_destroy(&attr);
        rstd_assert_eq(r, 0);
#endif
    }

    // Signal one waiting thread
    void notify_one() noexcept {
        [[maybe_unused]] auto r = pthread_cond_signal(raw());
        debug_assert_eq(r, 0);
    }

    // Signal all waiting threads
    void notify_all() noexcept {
        [[maybe_unused]] auto r = pthread_cond_broadcast(raw());
        debug_assert_eq(r, 0);
    }

    // Wait on the condition variable
    // mutex must be locked by the current thread
    void wait(Mutex& mutex) noexcept {
        [[maybe_unused]] auto r = pthread_cond_wait(raw(), mutex.raw());
        debug_assert_eq(r, 0);
    }

    // Wait on the condition variable with a timeout
    // Returns true if notified, false if timed out
    // mutex must be locked by the current thread
    auto wait_timeout(Mutex& mutex, u64 timeout_ns) noexcept -> bool {
        timespec ts;

#ifdef RSTD_OS_APPLE
        // No CLOCK_MONOTONIC condvar on macOS — use CLOCK_REALTIME.
        clock_gettime(static_cast<rstd::sys::libc::clockid_t>(CLOCK_REALTIME), &ts);
#else
        // Get current time with CLOCK_MONOTONIC
        clock_gettime(CLOCK_MONOTONIC, &ts);
#endif

        // Add timeout
        const u64 NSEC_PER_SEC = 1'000'000'000;
        u64 total_nsec = ts.tv_nsec + timeout_ns;
        ts.tv_sec += total_nsec / NSEC_PER_SEC;
        ts.tv_nsec = total_nsec % NSEC_PER_SEC;

        auto r = pthread_cond_timedwait(raw(), mutex.raw(), &ts);
        rstd_assert(r == ETIMEDOUT || r == 0, "pthread_cond_timedwait failed");
        return r == 0;
    }
};

} // namespace rstd::sys::pal::unix::sync::condvar
#endif
