module;
#include <rstd/macro.hpp>
export module rstd:sys.pal.unix.sync.mutex;

#if RSTD_OS_UNIX
export import :sys.libc.pthread;
export import rstd.core;

using namespace rstd::sys::libc;

namespace rstd::sys::pal::unix::sync::mutex
{

export class Mutex {
    pthread_mutex_t inner;
#ifdef RSTD_OS_APPLE
    // Force eager pthread_mutex_init on Apple. PTHREAD_MUTEX_INITIALIZER
    // on macOS only marks the mutex as "first-use-initializes"; on the
    // first lock() the kernel records the mutex's *address* internally,
    // so any earlier relocation (RVO miss, defensive copy, Box::pin's
    // placement-new from an rvalue) breaks it. pthread_mutex_init at the
    // mutex's final address sidesteps that handshake.
    //
    // The `inited` flag is true when `inner` holds a real (not yet moved-
    // from) pthread_mutex_t that must be destroyed at scope exit.
    bool inited;
#endif

    Mutex() noexcept
#ifdef RSTD_OS_APPLE
        : inner{}, inited(true) {
        [[maybe_unused]] auto r = pthread_mutex_init(&inner, nullptr);
        debug_assert_eq(r, 0);
    }
#else
        : inner(pthread_mutex_initializer()) {}
#endif

public:
    ~Mutex() noexcept {
        // always destroy here even move is copy
        // only valid when destroy on not used mutex
#ifdef RSTD_OS_APPLE
        if (inited) pthread_mutex_destroy(&inner);
#else
        pthread_mutex_destroy(&inner);
#endif
    }

#ifdef RSTD_OS_APPLE
    // pthread_mutex_t is address-sensitive on macOS — bit-copying an
    // initialised mutex to a new address breaks it. On Apple a move
    // tears down the source's mutex (it was never used — fresh out of
    // make()/pin's placement-new) and freshly inits the destination at
    // its own address.
    Mutex(Mutex&& o) noexcept: inner{}, inited(false) {
        if (o.inited) {
            // The source was eager-inited but never locked — destroy
            // and re-init at *our* address.
            pthread_mutex_destroy(&o.inner);
            o.inited = false;
        }
        [[maybe_unused]] auto r = pthread_mutex_init(&inner, nullptr);
        debug_assert_eq(r, 0);
        inited = true;
    }
#else
    Mutex(Mutex&& o) noexcept: inner(o.inner) {}
#endif
    Mutex(const Mutex&)            = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex& operator=(Mutex&&)      = delete;

    static auto make() noexcept -> Mutex { return {}; }

    auto raw() noexcept -> pthread_mutex_t* { return &inner; }

    void lock() noexcept {
        auto r = pthread_mutex_lock(raw());
        if (r != 0) {
            // TODO: from_raw_parts_os_error
            // error = Error::from_raw_parts_os_error(r);
            panic { "failed to lock mutex" };
        }
    }

    auto try_lock() noexcept -> bool { return pthread_mutex_trylock(raw()) == 0; }

    void unlock() noexcept {
        [[maybe_unused]]
        auto r = pthread_mutex_unlock(raw());
        debug_assert_eq(r, 0);
    }
};

static_assert(mtp::triv_copy<mut_ptr<Mutex>>);

} // namespace rstd::sys::pal::unix::sync::mutex
#endif
