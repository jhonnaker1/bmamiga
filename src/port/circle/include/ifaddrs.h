/*
 * ifaddrs.h for Amiberry on Circle: Amiberry's bsdsocket.library asks the
 * host for its network interfaces (osdep/bsdsocket_ifinfo.h). There are
 * none to list, so getifaddrs() fails (src/port/circle/posix.c), and the
 * Amiga's interface ioctls with it.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_CIRCLE_IFADDRS_H_
#define BMAMIGA_CIRCLE_IFADDRS_H_

#include <sys/socket.h>

struct ifaddrs {
	struct ifaddrs *ifa_next;
	char *ifa_name;
	unsigned int ifa_flags;
	struct sockaddr *ifa_addr;
	struct sockaddr *ifa_netmask;
	union {
		struct sockaddr *ifu_broadaddr;
		struct sockaddr *ifu_dstaddr;
	} ifa_ifu;
	void *ifa_data;
};

#define ifa_broadaddr ifa_ifu.ifu_broadaddr
#define ifa_dstaddr   ifa_ifu.ifu_dstaddr

#ifdef __cplusplus
extern "C" {
#endif

int getifaddrs(struct ifaddrs **ifap);
void freeifaddrs(struct ifaddrs *ifa);

#ifdef __cplusplus
}
#endif

#endif /* BMAMIGA_CIRCLE_IFADDRS_H_ */
