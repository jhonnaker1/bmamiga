/*
 * bma_circle.h - included before each of Amiberry's files when it is built
 * for the Pi (-include, see src/port/circle/circle.mk), so that Amiberry builds
 * unmodified with newlib and the toolchain's libstdc++, which have no
 * threads and lack some Linux names.
 *
 * C:
 *  - Large file names (off64_t, fseeko64, stat64, ...): newlib's off_t is
 *    already 64 bits.
 *  - CLOCK_MONOTONIC and clock_gettime(), which newlib leaves out for
 *    Circle (Amiberry's is BMAmiga's, src/port/clock.c).
 *  - struct tm's tm_gmtoff, which newlib's hasn't: read as 0 (UTC; the
 *    Pi's clock is UTC).
 *
 * C++: std::mutex, std::recursive_mutex and std::condition_variable, which
 * libstdc++ only has with threads (_GLIBCXX_HAS_GTHREADS); BMAmiga's
 * cooperative threads (src/port/circle/threads.c) do the waiting.
 * std::thread can't be given
 * a way to start (libstdc++ defines the class), so Amiberry is built
 * with NO_STD_THREAD=1 (CHD's work queues run their work at once).
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_CIRCLE_H_
#define BMAMIGA_CIRCLE_H_

#include <sys/types.h>
#include <stdint.h>
#include <time.h>

/* ---- C --------------------------------------------------------------- */

typedef off_t off64_t;
#define fseeko64 fseeko
#define ftello64 ftello
#define fopen64 fopen
#define open64 open
#define lseek64 lseek
#define pread64 pread
#define pwrite64 pwrite
#define ftruncate64 ftruncate
#define truncate64 truncate
#define stat64 stat
#define fstat64 fstat
#define lstat64 lstat
#define dirent64 dirent
#define readdir64 readdir
#define statvfs64 statvfs

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 4
#endif
#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW 5
#endif

#ifdef __cplusplus
extern "C" {
#endif
/* Renamed to bma_clock_gettime() and the rest by the build (src/port/clock.c). */
int clock_gettime(clockid_t clock_id, struct timespec *tp);
int nanosleep(const struct timespec *req, struct timespec *rem);
int usleep(useconds_t usec);
#ifdef __cplusplus
}
#endif

/* lt->tm_gmtoff reads as 0. */
#define tm_gmtoff tm_isdst * 0

#ifdef __cplusplus
extern "C" {
#endif
time_t timegm(struct tm *tm);
#ifdef __cplusplus
}
#endif

/* Byte order (<endian.h>'s, which newlib hasn't): the Pi is little
   endian. */
#ifndef le32toh
#define htobe16(x) __builtin_bswap16((uint16_t) (x))
#define htole16(x) ((uint16_t) (x))
#define be16toh(x) __builtin_bswap16((uint16_t) (x))
#define le16toh(x) ((uint16_t) (x))
#define htobe32(x) __builtin_bswap32((uint32_t) (x))
#define htole32(x) ((uint32_t) (x))
#define be32toh(x) __builtin_bswap32((uint32_t) (x))
#define le32toh(x) ((uint32_t) (x))
#define htobe64(x) __builtin_bswap64((uint64_t) (x))
#define htole64(x) ((uint64_t) (x))
#define be64toh(x) __builtin_bswap64((uint64_t) (x))
#define le64toh(x) ((uint64_t) (x))
#endif

/* ---- C++ --------------------------------------------------------------- */

#ifdef __cplusplus

#include <bits/c++config.h>

#ifndef _GLIBCXX_HAS_GTHREADS

#include <chrono>
#include <bits/std_mutex.h>
#include <bits/unique_lock.h>

extern "C" {
/* src/port/circle/threads.c: locks and conditions keyed by the object. */
void bma_object_lock(const void *object);
int bma_object_trylock(const void *object);
void bma_object_unlock(const void *object);
void bma_object_forget(const void *object);
/* Waits for a notify on CONDITION, LOCK unlocked meanwhile; MS < 0 is no
   time limit. Returns 0 if notified, 1 on timeout. */
int bma_object_wait(const void *condition, const void *lock, long long ms);
void bma_object_notify(const void *condition, int all);
}

namespace std _GLIBCXX_VISIBILITY(default)
{
_GLIBCXX_BEGIN_NAMESPACE_VERSION

  class mutex
  {
  public:
    constexpr mutex() noexcept = default;
    ~mutex() { bma_object_forget(this); }
    mutex(const mutex &) = delete;
    mutex &operator=(const mutex &) = delete;
    void lock() { bma_object_lock(this); }
    bool try_lock() noexcept { return bma_object_trylock(this) != 0; }
    void unlock() { bma_object_unlock(this); }
  private:
    char _M_unused = 0;
  };

  /* BMAmiga's locks are recursive already (src/port/circle/threads.c). */
  class recursive_mutex
  {
  public:
    recursive_mutex() noexcept = default;
    ~recursive_mutex() { bma_object_forget(this); }
    recursive_mutex(const recursive_mutex &) = delete;
    recursive_mutex &operator=(const recursive_mutex &) = delete;
    void lock() { bma_object_lock(this); }
    bool try_lock() noexcept { return bma_object_trylock(this) != 0; }
    void unlock() { bma_object_unlock(this); }
  private:
    char _M_unused = 0;
  };

  enum class cv_status { no_timeout, timeout };

  class condition_variable
  {
  public:
    condition_variable() noexcept = default;
    ~condition_variable() { bma_object_forget(this); }
    condition_variable(const condition_variable &) = delete;
    condition_variable &operator=(const condition_variable &) = delete;

    void notify_one() noexcept { bma_object_notify(this, 0); }
    void notify_all() noexcept { bma_object_notify(this, 1); }

    void wait(unique_lock<mutex> &__lock) { bma_object_wait(this, __lock.mutex(), -1); }

    template<typename _Predicate>
      void wait(unique_lock<mutex> &__lock, _Predicate __p)
      {
	while (!__p())
	  wait(__lock);
      }

    template<typename _Rep, typename _Period>
      cv_status wait_for(unique_lock<mutex> &__lock,
			 const chrono::duration<_Rep, _Period> &__rtime)
      {
	long long __ms = chrono::duration_cast<chrono::milliseconds>(__rtime).count();
	return bma_object_wait(this, __lock.mutex(), __ms < 0 ? 0 : __ms)
	       ? cv_status::timeout : cv_status::no_timeout;
      }

    template<typename _Rep, typename _Period, typename _Predicate>
      bool wait_for(unique_lock<mutex> &__lock,
		    const chrono::duration<_Rep, _Period> &__rtime, _Predicate __p)
      {
	auto __end = chrono::steady_clock::now() + __rtime;
	while (!__p())
	  if (wait_until(__lock, __end) == cv_status::timeout)
	    return __p();
	return true;
      }

    template<typename _Clock, typename _Duration>
      cv_status wait_until(unique_lock<mutex> &__lock,
			   const chrono::time_point<_Clock, _Duration> &__atime)
      {
	return wait_for(__lock, __atime - _Clock::now());
      }

    template<typename _Clock, typename _Duration, typename _Predicate>
      bool wait_until(unique_lock<mutex> &__lock,
		      const chrono::time_point<_Clock, _Duration> &__atime, _Predicate __p)
      {
	while (!__p())
	  if (wait_until(__lock, __atime) == cv_status::timeout)
	    return __p();
	return true;
      }
  };

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std

#endif /* !_GLIBCXX_HAS_GTHREADS */
#endif /* __cplusplus */

#endif /* BMAMIGA_CIRCLE_H_ */
