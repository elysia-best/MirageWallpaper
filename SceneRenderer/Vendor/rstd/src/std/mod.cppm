/// The rstd standard library module, re-exporting all public submodules.
module;
#include <rstd/macro.hpp>

export module rstd;
export import rstd.core;
export import :time;
export import :forward;
export import :sys;
export import :sync;
export import :thread;
export import :async;
export import :io;
export import :bytes;
#if !defined(RSTD_OS_APPLE)
export import :net;
#endif
export import :process;
export import :env;
export import :path;
export import :panicking;
export import :alloc;
export import :fs;
