module;
#include <rstd/macro.hpp>

#ifdef RSTD_OS_LINUX
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#endif

#ifdef RSTD_OS_UNIX
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <fcntl.h>
#include <dirent.h>
#endif

export module rstd:sys.libc.unix;

#ifdef RSTD_OS_UNIX

// ── Linux-only constants (futex syscall, sysmacros for dev_t) ────────────
#ifdef RSTD_OS_LINUX
inline constexpr auto _SYS_futex              = SYS_futex;
inline constexpr auto _FUTEX_WAIT_BITSET      = FUTEX_WAIT_BITSET;
inline constexpr auto _FUTEX_PRIVATE_FLAG     = FUTEX_PRIVATE_FLAG;
inline constexpr auto _FUTEX_WAKE             = FUTEX_WAKE;
inline constexpr auto _FUTEX_BITSET_MATCH_ANY = FUTEX_BITSET_MATCH_ANY;
#endif

// macOS expands CLOCK_MONOTONIC to enumerator `_CLOCK_MONOTONIC`, which
// would collide with the constexpr we declare next. Capture into an rstd-
// prefixed name first, then #undef the macro so subsequent uses are clean.
inline constexpr auto _RSTD_CLOCK_MONOTONIC = CLOCK_MONOTONIC;
inline constexpr auto _RSTD_CLOCK_REALTIME  = CLOCK_REALTIME;

inline constexpr auto _ETIMEDOUT   = ETIMEDOUT;
inline constexpr auto _EINTR       = EINTR;
inline constexpr auto _EINVAL      = EINVAL;
inline constexpr auto _EWOULDBLOCK = EWOULDBLOCK;
inline constexpr auto _EIO         = EIO;
inline constexpr auto _EAGAIN      = EAGAIN;
inline constexpr auto _ENOENT = ENOENT, _EACCES = EACCES, _EPERM = EPERM, _ECONNREFUSED = ECONNREFUSED;
inline constexpr auto _ECONNRESET = ECONNRESET, _EHOSTUNREACH = EHOSTUNREACH, _ENETUNREACH = ENETUNREACH;
inline constexpr auto _ECONNABORTED = ECONNABORTED, _ENOTCONN = ENOTCONN, _EADDRINUSE = EADDRINUSE;
inline constexpr auto _EADDRNOTAVAIL = EADDRNOTAVAIL, _ENETDOWN = ENETDOWN, _EPIPE = EPIPE, _EEXIST = EEXIST;
inline constexpr auto _ENOTDIR = ENOTDIR, _EISDIR = EISDIR, _ENOTEMPTY = ENOTEMPTY, _EROFS = EROFS;
inline constexpr auto _ELOOP = ELOOP, _ENOSPC = ENOSPC, _ESPIPE = ESPIPE, _EFBIG = EFBIG, _EBUSY = EBUSY;
inline constexpr auto _ETXTBSY = ETXTBSY, _EDEADLK = EDEADLK, _EXDEV = EXDEV, _EMLINK = EMLINK;
inline constexpr auto _ENAMETOOLONG = ENAMETOOLONG, _E2BIG = E2BIG, _ENOSYS = ENOSYS;
inline constexpr auto _EOPNOTSUPP = EOPNOTSUPP, _EAFNOSUPPORT = EAFNOSUPPORT, _ENOMEM = ENOMEM;
inline constexpr auto _ENOBUFS = ENOBUFS, _EINPROGRESS = EINPROGRESS;
#ifdef ESTALE
inline constexpr auto _ESTALE = ESTALE;
inline constexpr bool _HAS_ESTALE = true;
#else
inline constexpr auto _ESTALE = -1;
inline constexpr bool _HAS_ESTALE = false;
#endif
#ifdef EDQUOT
inline constexpr auto _EDQUOT = EDQUOT;
inline constexpr bool _HAS_EDQUOT = true;
#else
inline constexpr auto _EDQUOT = -1;
inline constexpr bool _HAS_EDQUOT = false;
#endif

inline constexpr auto _SIGKILL = SIGKILL;

inline constexpr auto _O_CLOEXEC   = O_CLOEXEC;
inline constexpr auto _O_RDONLY    = O_RDONLY;
inline constexpr auto _O_WRONLY    = O_WRONLY;
inline constexpr auto _O_RDWR      = O_RDWR;
inline constexpr auto _O_CREAT     = O_CREAT;
inline constexpr auto _O_EXCL      = O_EXCL;
inline constexpr auto _O_TRUNC     = O_TRUNC;
inline constexpr auto _O_APPEND    = O_APPEND;
inline constexpr auto _O_NOFOLLOW  = O_NOFOLLOW;
inline constexpr auto _O_DIRECTORY = O_DIRECTORY;
inline constexpr auto _F_DUPFD_CLOEXEC = F_DUPFD_CLOEXEC;
inline constexpr auto _SEEK_SET = SEEK_SET;
inline constexpr auto _SEEK_CUR = SEEK_CUR;
inline constexpr auto _SEEK_END = SEEK_END;

inline constexpr auto _S_IFMT   = S_IFMT;
inline constexpr auto _S_IFREG  = S_IFREG;
inline constexpr auto _S_IFDIR  = S_IFDIR;
inline constexpr auto _S_IFLNK  = S_IFLNK;
inline constexpr auto _S_IFIFO  = S_IFIFO;
inline constexpr auto _S_IFBLK  = S_IFBLK;
inline constexpr auto _S_IFCHR  = S_IFCHR;
inline constexpr auto _S_IFSOCK = S_IFSOCK;

inline constexpr auto _DT_REG  = DT_REG;
inline constexpr auto _DT_DIR  = DT_DIR;
inline constexpr auto _DT_LNK  = DT_LNK;
inline constexpr auto _DT_FIFO = DT_FIFO;
inline constexpr auto _DT_BLK  = DT_BLK;
inline constexpr auto _DT_CHR  = DT_CHR;
inline constexpr auto _DT_SOCK = DT_SOCK;

inline constexpr auto _LOCK_SH = LOCK_SH;
inline constexpr auto _LOCK_EX = LOCK_EX;
inline constexpr auto _LOCK_NB = LOCK_NB;
inline constexpr auto _LOCK_UN = LOCK_UN;

inline constexpr auto _UTIME_OMIT = UTIME_OMIT;

#ifdef RSTD_OS_LINUX
#undef SYS_futex
#undef FUTEX_WAIT_BITSET
#undef FUTEX_PRIVATE_FLAG
#undef FUTEX_WAKE
#undef FUTEX_BITSET_MATCH_ANY
#endif
#undef CLOCK_MONOTONIC
#undef CLOCK_REALTIME
#undef ETIMEDOUT
#undef EINTR
#undef EINVAL
#undef EWOULDBLOCK
#undef EIO
#undef EAGAIN
#undef ENOENT
#undef EACCES
#undef EPERM
#undef ECONNREFUSED
#undef ECONNRESET
#undef EHOSTUNREACH
#undef ENETUNREACH
#undef ECONNABORTED
#undef ENOTCONN
#undef EADDRINUSE
#undef EADDRNOTAVAIL
#undef ENETDOWN
#undef EPIPE
#undef EEXIST
#undef ENOTDIR
#undef EISDIR
#undef ENOTEMPTY
#undef EROFS
#undef ELOOP
#undef ENOSPC
#undef ESPIPE
#undef EFBIG
#undef EBUSY
#undef ETXTBSY
#undef EDEADLK
#undef EXDEV
#undef EMLINK
#undef ENAMETOOLONG
#undef E2BIG
#undef ENOSYS
#undef EOPNOTSUPP
#undef EAFNOSUPPORT
#undef ENOMEM
#undef ENOBUFS
#undef EINPROGRESS
#undef ESTALE
#undef EDQUOT
#undef SIGKILL
#undef O_CLOEXEC
#undef O_RDONLY
#undef O_WRONLY
#undef O_RDWR
#undef O_CREAT
#undef O_EXCL
#undef O_TRUNC
#undef O_APPEND
#undef O_NOFOLLOW
#undef O_DIRECTORY
#undef F_DUPFD_CLOEXEC
#undef SEEK_SET
#undef SEEK_CUR
#undef SEEK_END
#undef S_IFMT
#undef S_IFREG
#undef S_IFDIR
#undef S_IFLNK
#undef S_IFIFO
#undef S_IFBLK
#undef S_IFCHR
#undef S_IFSOCK
#undef DT_REG
#undef DT_DIR
#undef DT_LNK
#undef DT_FIFO
#undef DT_BLK
#undef DT_CHR
#undef DT_SOCK
#undef LOCK_SH
#undef LOCK_EX
#undef LOCK_NB
#undef LOCK_UN
#undef UTIME_OMIT

inline auto _rstd_make_dev(unsigned int ma, unsigned int mi) noexcept -> ::dev_t {
    return makedev(ma, mi);
}
inline auto _rstd_dev_major(::dev_t d) noexcept -> unsigned int {
    return major(d);
}
inline auto _rstd_dev_minor(::dev_t d) noexcept -> unsigned int {
    return minor(d);
}
#undef makedev
#undef major
#undef minor

export namespace rstd::sys::libc
{

#ifdef RSTD_OS_LINUX
using ::syscall;
#endif
using ::sched_yield;
using ::posix_memalign;

#ifdef RSTD_OS_LINUX
inline constexpr auto SYS_futex              = _SYS_futex;
inline constexpr auto FUTEX_WAIT_BITSET      = _FUTEX_WAIT_BITSET;
inline constexpr auto FUTEX_PRIVATE_FLAG     = _FUTEX_PRIVATE_FLAG;
inline constexpr auto FUTEX_WAKE             = _FUTEX_WAKE;
inline constexpr auto FUTEX_BITSET_MATCH_ANY = _FUTEX_BITSET_MATCH_ANY;
#endif

using ::clock_gettime;
using ::nanosleep;
using ::gmtime_r;
#ifdef RSTD_OS_APPLE
// macOS's clock_gettime takes `clockid_t` (an enum), not `int`. Re-export
// the type so callers can cast properly.
using ::clockid_t;
#endif
inline constexpr auto CLOCK_MONOTONIC = _RSTD_CLOCK_MONOTONIC;
inline constexpr auto CLOCK_REALTIME  = _RSTD_CLOCK_REALTIME;
inline constexpr auto ETIMEDOUT       = _ETIMEDOUT;
inline constexpr auto EINTR           = _EINTR;
inline constexpr auto EINVAL          = _EINVAL;
inline constexpr auto EWOULDBLOCK     = _EWOULDBLOCK;
inline constexpr auto EIO             = _EIO;
inline constexpr auto EAGAIN          = _EAGAIN;

inline auto gmtime_utc(::time_t secs) noexcept -> ::tm {
    ::tm out {};
    ::gmtime_r(&secs, &out);
    return out;
}

using ::isatty;

// ── Process ──────────────────────────────────────────────────────────────
using ::pid_t;
using ::posix_spawn;
using ::posix_spawnp;
using ::posix_spawn_file_actions_t;
using ::posix_spawn_file_actions_init;
using ::posix_spawn_file_actions_destroy;
using ::posix_spawn_file_actions_addclose;
using ::posix_spawn_file_actions_adddup2;
using ::posix_spawn_file_actions_addopen;
using ::posix_spawnattr_t;
using ::posix_spawnattr_init;
using ::posix_spawnattr_destroy;
using ::waitpid;
#ifdef RSTD_OS_LINUX
using ::pipe2;
#else
// macOS has no pipe2(2). Emulate atomically-ish: pipe() then set O_CLOEXEC
// + O_NONBLOCK on each fd as requested. Good enough for our usage; there
// is a small window between pipe() and fcntl() where another thread that
// forks could leak the fds, but rstd's callers don't fork from worker
// threads.
inline int pipe2(int fds[2], int flags) noexcept {
    if (::pipe(fds) != 0) return -1;
    for (int i = 0; i < 2; ++i) {
        int got = ::fcntl(fds[i], F_GETFL);
        if (got < 0) { ::close(fds[0]); ::close(fds[1]); return -1; }
        // Match O_NONBLOCK / O_CLOEXEC against the captured ints because
        // the actual macros were #undef'd above. fcntl's F_SETFL/F_SETFD
        // take the system constants which still expand fine in the
        // global scope (sys/fcntl.h is included in the GMF).
        if (flags & _O_CLOEXEC) {
            int gd = ::fcntl(fds[i], F_GETFD);
            if (gd < 0) { ::close(fds[0]); ::close(fds[1]); return -1; }
            ::fcntl(fds[i], F_SETFD, gd | FD_CLOEXEC);
        }
        // No O_NONBLOCK equivalent captured; callers in this codebase
        // don't set it via pipe2 anyway. If they did we'd capture it
        // the same way as _O_CLOEXEC above.
        (void)got;
    }
    return 0;
}
#endif
using ::close;
using ::dup;
using ::dup2;
using ::read;
using ::write;
using ::kill;
using ::lseek;
using ::pread;
using ::pwrite;
using ::fsync;
#ifdef RSTD_OS_APPLE
// macOS has no fdatasync(2); fsync(2) is the equivalent durability point
// for our callers (no journaled-metadata distinction is exposed).
inline int fdatasync(int fd) noexcept { return ::fsync(fd); }
#else
using ::fdatasync;
#endif
using ::ftruncate;
using ::unlink;
using ::rmdir;
using ::link;
using ::symlink;
using ::readlink;
using ::realpath;
using ::open;
using ::fcntl;
using ::chmod;
using ::fchmod;
using ::mkdir;
using ::futimens;
using ::utimensat;
using ::flock;
using ::stat;
using ::fstat;
using ::lstat;
using ::opendir;
using ::readdir;
using ::closedir;
using ::rename;
using ::free;

// ── Type aliases ─────────────────────────────────────────────────────────
using ::mode_t;
using ::off_t;
using ::ssize_t;
using ::time_t;
using ::dev_t;
using ::DIR;
using ::dirent;
/// `struct stat` aliased to avoid clash with the `::stat()` function.
using stat_t     = struct ::stat;
/// `struct timespec` aliased to avoid the `struct` keyword leaking into call sites.
using timespec_t = struct ::timespec;

inline constexpr auto SIGKILL   = _SIGKILL;
inline constexpr auto O_CLOEXEC = _O_CLOEXEC;

// ── Open flags / seek whence ─────────────────────────────────────────────
inline constexpr auto O_RDONLY    = _O_RDONLY;
inline constexpr auto O_WRONLY    = _O_WRONLY;
inline constexpr auto O_RDWR      = _O_RDWR;
inline constexpr auto O_CREAT     = _O_CREAT;
inline constexpr auto O_EXCL      = _O_EXCL;
inline constexpr auto O_TRUNC     = _O_TRUNC;
inline constexpr auto O_APPEND    = _O_APPEND;
inline constexpr auto O_NOFOLLOW  = _O_NOFOLLOW;
inline constexpr auto O_DIRECTORY = _O_DIRECTORY;
inline constexpr auto F_DUPFD_CLOEXEC = _F_DUPFD_CLOEXEC;
inline constexpr auto SEEK_SET    = _SEEK_SET;
inline constexpr auto SEEK_CUR    = _SEEK_CUR;
inline constexpr auto SEEK_END    = _SEEK_END;

// ── Stat mode masks ──────────────────────────────────────────────────────
inline constexpr auto S_IFMT   = _S_IFMT;
inline constexpr auto S_IFREG  = _S_IFREG;
inline constexpr auto S_IFDIR  = _S_IFDIR;
inline constexpr auto S_IFLNK  = _S_IFLNK;
inline constexpr auto S_IFIFO  = _S_IFIFO;
inline constexpr auto S_IFBLK  = _S_IFBLK;
inline constexpr auto S_IFCHR  = _S_IFCHR;
inline constexpr auto S_IFSOCK = _S_IFSOCK;

// ── Dirent type tags ─────────────────────────────────────────────────────
inline constexpr auto DT_REG  = _DT_REG;
inline constexpr auto DT_DIR  = _DT_DIR;
inline constexpr auto DT_LNK  = _DT_LNK;
inline constexpr auto DT_FIFO = _DT_FIFO;
inline constexpr auto DT_BLK  = _DT_BLK;
inline constexpr auto DT_CHR  = _DT_CHR;
inline constexpr auto DT_SOCK = _DT_SOCK;

// ── flock ────────────────────────────────────────────────────────────────
inline constexpr auto LOCK_SH = _LOCK_SH;
inline constexpr auto LOCK_EX = _LOCK_EX;
inline constexpr auto LOCK_NB = _LOCK_NB;
inline constexpr auto LOCK_UN = _LOCK_UN;

// ── utimensat sentinel ───────────────────────────────────────────────────
inline constexpr auto UTIME_OMIT = _UTIME_OMIT;

inline constexpr auto ENOENT = _ENOENT, EACCES = _EACCES, EPERM = _EPERM, ECONNREFUSED = _ECONNREFUSED;
inline constexpr auto ECONNRESET = _ECONNRESET, EHOSTUNREACH = _EHOSTUNREACH, ENETUNREACH = _ENETUNREACH;
inline constexpr auto ECONNABORTED = _ECONNABORTED, ENOTCONN = _ENOTCONN, EADDRINUSE = _EADDRINUSE;
inline constexpr auto EADDRNOTAVAIL = _EADDRNOTAVAIL, ENETDOWN = _ENETDOWN, EPIPE = _EPIPE, EEXIST = _EEXIST;
inline constexpr auto ENOTDIR = _ENOTDIR, EISDIR = _EISDIR, ENOTEMPTY = _ENOTEMPTY, EROFS = _EROFS;
inline constexpr auto ELOOP = _ELOOP, ENOSPC = _ENOSPC, ESPIPE = _ESPIPE, EFBIG = _EFBIG, EBUSY = _EBUSY;
inline constexpr auto ETXTBSY = _ETXTBSY, EDEADLK = _EDEADLK, EXDEV = _EXDEV, EMLINK = _EMLINK;
inline constexpr auto ENAMETOOLONG = _ENAMETOOLONG, E2BIG = _E2BIG, ENOSYS = _ENOSYS;
inline constexpr auto EOPNOTSUPP = _EOPNOTSUPP, EAFNOSUPPORT = _EAFNOSUPPORT, ENOMEM = _ENOMEM;
inline constexpr auto ENOBUFS = _ENOBUFS, EINPROGRESS = _EINPROGRESS;
inline constexpr auto ESTALE = _ESTALE, EDQUOT = _EDQUOT;
inline constexpr bool HAS_ESTALE = _HAS_ESTALE, HAS_EDQUOT = _HAS_EDQUOT;

/// Returns an lvalue reference to the platform `errno`. Use to read and write.
inline auto get_errno() noexcept -> int& { return errno; }

inline auto makedev(unsigned int ma, unsigned int mi) noexcept -> ::dev_t {
    return _rstd_make_dev(ma, mi);
}
inline auto major(::dev_t d) noexcept -> unsigned int { return _rstd_dev_major(d); }
inline auto minor(::dev_t d) noexcept -> unsigned int { return _rstd_dev_minor(d); }

inline auto wait_exited(int status) -> bool  { return WIFEXITED(status); }
inline auto wait_exitstatus(int status) -> int { return WEXITSTATUS(status); }
inline auto wait_signaled(int status) -> bool { return WIFSIGNALED(status); }
inline auto wait_termsig(int status) -> int  { return WTERMSIG(status); }

} // namespace rstd::sys::libc
#endif
