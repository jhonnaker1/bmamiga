/*
 * sys/uio.h for Amiberry on Circle.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_CIRCLE_SYS_UIO_H_
#define BMAMIGA_CIRCLE_SYS_UIO_H_

#include <sys/types.h>

struct iovec {
	void *iov_base;
	size_t iov_len;
};

#ifdef __cplusplus
extern "C" {
#endif

ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

#ifdef __cplusplus
}
#endif

#endif /* BMAMIGA_CIRCLE_SYS_UIO_H_ */
