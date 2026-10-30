/*
 * posix.c - the POSIX functions Amiberry uses that newlib and
 * circle-stdlib don't have, for BMAmiga on Circle.
 *
 *  - Memory: mmap() of anonymous memory is the heap's (readable,
 *    writable and executable; there is no virtual memory). Amiberry reserves 4 GB for the Amiga's memory on 64-bit
 *    hosts (for its JIT, which BMAmiga doesn't build), then tries smaller
 *    sizes until one is given (osdep/amiberry_mem.cpp): reservations over
 *    MAP_LIMIT fail, so it gets that much, all real. mprotect() does
 *    nothing; munmap() frees a whole mapping. posix_memalign() with
 *    more than Circle's memalign() can align (see below), with free()
 *    wrapped (--wrap=free) to give the right block back.
 *  - Files: access(), pread(), pwrite(), realpath() (wrapped); chmod()
 *    and chown() do nothing (FAT has no owners and no modes).
 *  - Text: iconv() between UTF-8 and ISO-8859-1, the conversion Amiberry
 *    uses for the Amiga's file names (osdep/amiberry_filesys.cpp).
 *  - timegm(); gettimeofday() before Circle's timer exists (wrapped).
 *  - What there is none of, which fails: processes (popen(), execvp(),
 *    forkpty(), waitpid()), shared libraries (dlopen()), shared memory,
 *    terminals, signals (sigaction() does nothing), ioctl().
 *  - inet_addr(), inet_aton(), inet_ntoa(), gethostname(), getuid().
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <iconv.h>
#include <limits.h>
#include <pty.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

/* ---- Memory ---------------------------------------------------------------- */

/* Mappings are aligned to 64K, as a desktop's pages are at least. Circle's
   memalign() can't give that: it asserts that the alignment is at most a
   cache line (64 bytes), which on the Pi stops every core (BMNeXT's first
   Pi 5 start was a black screen from this). So a mapping is a block from
   malloc() 64K bigger, aligned inside it. */
#define MAP_ALIGN (64 * 1024)
#define MAP_LIMIT ((size_t) 320 * 1024 * 1024)

#define MAX_MAPPINGS 256
static struct {
	void *start;            /* what mmap() returned, aligned */
	size_t length;
	void *block;            /* what malloc() gave */
} mappings[MAX_MAPPINGS];

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	void *p;
	int i;
	(void) prot;
	(void) offset;
	if (!(flags & MAP_ANONYMOUS) || fd != -1 || length == 0) {
		errno = ENODEV;
		return MAP_FAILED;
	}
	/* Fixed: a part of a mapping already made (Amiberry commits parts of
	   its reservation this way). */
	if ((flags & MAP_FIXED) && addr != NULL) {
		for (i = 0; i < MAX_MAPPINGS; i++)
			if (mappings[i].start != NULL && (char *) addr >= (char *) mappings[i].start
			    && (char *) addr + length <= (char *) mappings[i].start + mappings[i].length)
				return addr;
		errno = ENOMEM;
		return MAP_FAILED;
	}
	if (length > MAP_LIMIT) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	for (i = 0; i < MAX_MAPPINGS; i++)
		if (mappings[i].start == NULL)
			break;
	if (i == MAX_MAPPINGS) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	mappings[i].block = malloc(length + MAP_ALIGN);
	if (mappings[i].block == NULL) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	p = (void *) (((uintptr_t) mappings[i].block + MAP_ALIGN - 1) & ~(uintptr_t) (MAP_ALIGN - 1));
	memset(p, 0, length);
	mappings[i].start = p;
	mappings[i].length = length;
	return p;
}

int munmap(void *addr, size_t length)
{
	int i;
	for (i = 0; i < MAX_MAPPINGS; i++)
		if (mappings[i].start == addr && mappings[i].length == length) {
			free(mappings[i].block);
			mappings[i].start = NULL;
			mappings[i].block = NULL;
			return 0;
		}
	/* Part of a mapping (decommitting): kept. */
	return 0;
}

int mprotect(void *addr, size_t length, int prot)
{
	(void) addr; (void) length; (void) prot;
	return 0;
}

int madvise(void *addr, size_t length, int advice)
{
	(void) addr; (void) length; (void) advice;
	return 0;
}

int msync(void *addr, size_t length, int flags)
{
	(void) addr; (void) length; (void) flags;
	return 0;
}

int mlock(const void *addr, size_t length)
{
	(void) addr; (void) length;
	return 0;
}

int munlock(const void *addr, size_t length)
{
	(void) addr; (void) length;
	return 0;
}

/* posix_memalign(): Amiberry aligns each of the Amiga's memory banks to a
   page (sysconf(_SC_PAGESIZE), 4096 here) and frees them with free(). Up
   to a cache line (64 bytes, which Circle's malloc() always gives) it is
   malloc(); more is a bigger block, aligned inside it and noted here, so
   that free() (wrapped, below) frees the block. A few dozen at most. */
#define HEAP_ALIGN 64
#define MAX_ALIGNED 256
static struct {
	void *aligned;
	void *block;
} aligned_blocks[MAX_ALIGNED];
static volatile unsigned aligned_count;

void __real_free(void *p);

void __wrap_free(void *p)
{
	unsigned i, n = aligned_count;
	for (i = 0; i < n && p != NULL; i++)
		if (aligned_blocks[i].aligned == p) {
			void *block = aligned_blocks[i].block;
			aligned_blocks[i] = aligned_blocks[n - 1];
			aligned_count = n - 1;
			__real_free(block);
			return;
		}
	__real_free(p);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	void *block;
	if (alignment == 0 || (alignment & (alignment - 1)) != 0)
		return EINVAL;
	if (size == 0)
		size = 1;
	if (alignment <= HEAP_ALIGN) {
		block = malloc(size);
		if (block == NULL)
			return ENOMEM;
		*memptr = block;
		return 0;
	}
	if (aligned_count == MAX_ALIGNED)
		return ENOMEM;
	block = malloc(size + alignment);
	if (block == NULL)
		return ENOMEM;
	*memptr = (void *) (((uintptr_t) block + alignment - 1) & ~(uintptr_t) (alignment - 1));
	aligned_blocks[aligned_count].aligned = *memptr;
	aligned_blocks[aligned_count].block = block;
	aligned_count++;
	return 0;
}

int shm_open(const char *name, int oflag, mode_t mode)
{
	(void) name; (void) oflag; (void) mode;
	errno = ENOSYS;
	return -1;
}

int shm_unlink(const char *name)
{
	(void) name;
	errno = ENOSYS;
	return -1;
}

/* ---- Files ----------------------------------------------------------------- */

int access(const char *path, int mode)
{
	struct stat st;
	(void) mode;
	return stat(path, &st);
}

int chmod(const char *path, mode_t mode)
{
	(void) path; (void) mode;
	return 0;
}

int chown(const char *path, uid_t owner, gid_t group)
{
	(void) path; (void) owner; (void) group;
	return 0;
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
	off_t was = lseek(fd, 0, SEEK_CUR);
	ssize_t r;
	if (was < 0 || lseek(fd, offset, SEEK_SET) < 0)
		return -1;
	r = read(fd, buf, count);
	lseek(fd, was, SEEK_SET);
	return r;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
	off_t was = lseek(fd, 0, SEEK_CUR);
	ssize_t r;
	if (was < 0 || lseek(fd, offset, SEEK_SET) < 0)
		return -1;
	r = write(fd, buf, count);
	lseek(fd, was, SEEK_SET);
	return r;
}

/* realpath() (wrapped, --wrap=realpath): circle-newlib's always fails
   (ENOSYS), and Amiberry only scans a folder for Kickstarts if realpath()
   gives it one, so it found none on the Pi and started AROS. FAT has no
   links, so this only makes the path absolute (from the current folder)
   and takes out ".", ".." and repeated '/', then checks that it exists. */
char *__wrap_realpath(const char *path, char *resolved)
{
	char full[PATH_MAX], out[PATH_MAX];
	const char *p;
	size_t len = 0;
	struct stat st;

	if (path == NULL || *path == '\0') {
		errno = path == NULL ? EINVAL : ENOENT;
		return NULL;
	}
	if (*path == '/') {
		full[0] = '\0';
	} else if (getcwd(full, sizeof full) == NULL) {
		return NULL;
	}
	if (strlen(full) + 1 + strlen(path) >= sizeof full) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	strcat(full, "/");
	strcat(full, path);

	/* Keep a volume name ("SD:") as it is. */
	p = full;
	if (strchr(full, ':') != NULL && strchr(full, ':') < strchr(full, '/')) {
		len = (size_t) (strchr(full, ':') + 1 - full);
		memcpy(out, full, len);
		p = full + len;
	}
	while (*p != '\0') {
		const char *end;
		size_t n;
		while (*p == '/')
			p++;
		end = p;
		while (*end != '\0' && *end != '/')
			end++;
		n = (size_t) (end - p);
		if (n == 0 || (n == 1 && p[0] == '.')) {
			/* nothing */
		} else if (n == 2 && p[0] == '.' && p[1] == '.') {
			while (len > 0 && out[len - 1] != '/' && out[len - 1] != ':')
				len--;
			if (len > 0 && out[len - 1] == '/')
				len--;
		} else {
			if (len + 1 + n >= sizeof out) {
				errno = ENAMETOOLONG;
				return NULL;
			}
			out[len++] = '/';
			memcpy(out + len, p, n);
			len += n;
		}
		p = end;
	}
	if (len == 0 || out[len - 1] == ':')
		out[len++] = '/';
	out[len] = '\0';

	if (stat(out, &st) != 0)
		return NULL;
	if (resolved == NULL) {
		resolved = malloc(len + 1);
		if (resolved == NULL) {
			errno = ENOMEM;
			return NULL;
		}
	}
	memcpy(resolved, out, len + 1);
	return resolved;
}

/* ---- Text ------------------------------------------------------------------ */

enum { TO_LATIN1 = 1, TO_UTF8 = 2 };

static int is_utf8(const char *name)
{
	return strncasecmp(name, "UTF-8", 5) == 0 || strncasecmp(name, "UTF8", 4) == 0;
}

static int is_latin1(const char *name)
{
	return strcasecmp(name, "ISO-8859-1") == 0 || strcasecmp(name, "LATIN1") == 0
	    || strcasecmp(name, "ISO8859-1") == 0;
}

static int iconv_kinds[2] = {TO_LATIN1, TO_UTF8};

iconv_t iconv_open(const char *tocode, const char *fromcode)
{
	if (is_latin1(tocode) && is_utf8(fromcode))
		return (iconv_t) &iconv_kinds[0];
	if (is_utf8(tocode) && is_latin1(fromcode))
		return (iconv_t) &iconv_kinds[1];
	errno = EINVAL;
	return (iconv_t) -1;
}

int iconv_close(iconv_t cd)
{
	(void) cd;
	return 0;
}

size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft)
{
	int kind = *(int *) cd;
	size_t done = 0;
	if (inbuf == NULL || *inbuf == NULL)
		return 0;               /* reset: there is no state */
	while (*inbytesleft > 0) {
		unsigned char c = (unsigned char) **inbuf;
		if (kind == TO_LATIN1) {
			unsigned v;
			size_t n;
			if (c < 0x80) {
				v = c;
				n = 1;
			} else if ((c & 0xe0) == 0xc0 && *inbytesleft >= 2) {
				v = ((c & 0x1fu) << 6) | ((unsigned char) (*inbuf)[1] & 0x3fu);
				n = 2;
			} else if ((c & 0xf0) == 0xe0 && *inbytesleft >= 3) {
				v = '?';
				n = 3;
			} else if ((c & 0xf8) == 0xf0 && *inbytesleft >= 4) {
				v = '?';
				n = 4;
			} else {
				errno = (c & 0xc0) == 0xc0 ? EINVAL : EILSEQ;
				return (size_t) -1;
			}
			if (v > 0xff)
				v = '?';
			if (*outbytesleft < 1) {
				errno = E2BIG;
				return (size_t) -1;
			}
			*(*outbuf)++ = (char) v;
			(*outbytesleft)--;
			*inbuf += n;
			*inbytesleft -= n;
		} else {
			size_t need = c < 0x80 ? 1 : 2;
			if (*outbytesleft < need) {
				errno = E2BIG;
				return (size_t) -1;
			}
			if (need == 1) {
				*(*outbuf)++ = (char) c;
			} else {
				*(*outbuf)++ = (char) (0xc0 | (c >> 6));
				*(*outbuf)++ = (char) (0x80 | (c & 0x3f));
			}
			*outbytesleft -= need;
			(*inbuf)++;
			(*inbytesleft)--;
		}
		done++;
	}
	(void) done;
	return 0;
}

/* ---- Time ------------------------------------------------------------------ */

time_t timegm(struct tm *tm)
{
	static const int days_before[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
	long long year = tm->tm_year + 1900LL;
	int month = tm->tm_mon;
	long long days;
	while (month < 0) {
		month += 12;
		year--;
	}
	while (month > 11) {
		month -= 12;
		year++;
	}
	days = (year - 1970) * 365 + (year - 1969) / 4 - (year - 1901) / 100 + (year - 1601) / 400;
	days += days_before[month] + tm->tm_mday - 1;
	if (month > 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
		days++;
	return (time_t) (((days * 24 + tm->tm_hour) * 60 + tm->tm_min) * 60 + tm->tm_sec);
}

/* gettimeofday() (wrapped, --wrap=gettimeofday) is Circle's timer, which
   asserts if it doesn't exist yet, and it doesn't while Circle runs the
   constructors of static objects: one of Amiberry's (amiberry_gfx.cpp's
   gui_geometry_state) reads std::chrono::steady_clock, which libstdc++
   reads with gettimeofday(). That assert stopped every core before the
   serial port or the log were set up, so the Pi stayed black. Until the
   kernel has its timer (bma_timer_ready, set in CKernel's constructor)
   the time is 0. */
volatile int bma_timer_ready;

int __real_gettimeofday(struct timeval *tv, void *tz);

int __wrap_gettimeofday(struct timeval *tv, void *tz)
{
	if (!bma_timer_ready) {
		if (tv != NULL) {
			tv->tv_sec = 0;
			tv->tv_usec = 0;
		}
		return 0;
	}
	return __real_gettimeofday(tv, tz);
}

/* libstdc++'s std::this_thread::sleep_for() (built without nanosleep())
   calls these, which newlib hasn't: BMAmiga's sleep (src/port/clock.c),
   which lets the other threads run. */
int bma_usleep(useconds_t us);

int usleep(useconds_t us)
{
	return bma_usleep(us);
}

unsigned sleep(unsigned seconds)
{
	while (seconds-- > 0)
		bma_usleep(1000000);
	return 0;
}

/* ---- What there is none of ----------------------------------------------- */

FILE *popen(const char *command, const char *type)
{
	(void) command; (void) type;
	errno = ENOSYS;
	return NULL;
}

int pclose(FILE *stream)
{
	(void) stream;
	errno = ECHILD;
	return -1;
}

int execl(const char *path, const char *arg, ...)
{
	(void) path; (void) arg;
	errno = ENOSYS;
	return -1;
}

int execvp(const char *file, char *const argv[])
{
	(void) file; (void) argv;
	errno = ENOSYS;
	return -1;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
	(void) pid; (void) status; (void) options;
	errno = ECHILD;
	return -1;
}

int forkpty(int *amaster, char *name, const struct termios *termp, const struct winsize *winp)
{
	(void) amaster; (void) name; (void) termp; (void) winp;
	errno = ENOSYS;
	return -1;
}

int openpty(int *amaster, int *aslave, char *name, const struct termios *termp, const struct winsize *winp)
{
	(void) amaster; (void) aslave; (void) name; (void) termp; (void) winp;
	errno = ENOSYS;
	return -1;
}

void *dlopen(const char *file, int mode)
{
	(void) file; (void) mode;
	return NULL;
}

void *dlsym(void *handle, const char *name)
{
	(void) handle; (void) name;
	return NULL;
}

int dlclose(void *handle)
{
	(void) handle;
	return 0;
}

char *dlerror(void)
{
	return (char *) "no shared libraries on the Pi";
}

int dladdr(const void *addr, Dl_info *info)
{
	(void) addr; (void) info;
	return 0;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
	(void) sig; (void) act;
	if (oact != NULL)
		memset(oact, 0, sizeof *oact);
	return 0;
}

int ioctl(int fd, unsigned long request, ...)
{
	(void) fd; (void) request;
	errno = ENOTTY;
	return -1;
}

int tcgetattr(int fd, struct termios *termios_p)
{
	(void) fd; (void) termios_p;
	errno = ENOTTY;
	return -1;
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p)
{
	(void) fd; (void) optional_actions; (void) termios_p;
	errno = ENOTTY;
	return -1;
}

int gethostname(char *name, size_t len)
{
	snprintf(name, len, "bmamiga");
	return 0;
}

uid_t getuid(void)
{
	return 0;
}

/* ---- Addresses --------------------------------------------------------------- */

int inet_aton(const char *cp, struct in_addr *inp)
{
	unsigned a, b, c, d;
	char extra;
	if (sscanf(cp, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 || a > 255 || b > 255
	    || c > 255 || d > 255)
		return 0;
	inp->s_addr = htonl((a << 24) | (b << 16) | (c << 8) | d);
	return 1;
}

in_addr_t inet_addr(const char *cp)
{
	struct in_addr a;
	return inet_aton(cp, &a) ? a.s_addr : (in_addr_t) -1;
}

char *inet_ntoa(struct in_addr in)
{
	static char text[16];
	uint32_t v = ntohl(in.s_addr);
	snprintf(text, sizeof text, "%u.%u.%u.%u", (unsigned) (v >> 24), (unsigned) (v >> 16) & 255,
	         (unsigned) (v >> 8) & 255, (unsigned) v & 255);
	return text;
}
