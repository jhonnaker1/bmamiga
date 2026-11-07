/*
 * net/if.h for Amiberry on Circle: newlib's, with the interface flags it
 * lacks.
 *
 * Amiberry's bsdsocket.library turns the host's interface flags into the
 * Amiga's (osdep/bsdsocket_ifinfo.h). The Pi lists no interfaces
 * (getifaddrs() fails, src/port/circle/posix.c), so these only need to
 * exist; they are Linux's values.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_CIRCLE_NET_IF_H_
#define BMAMIGA_CIRCLE_NET_IF_H_

#include_next <net/if.h>

#ifndef IFF_UP
#define IFF_UP          0x1
#define IFF_BROADCAST   0x2
#define IFF_DEBUG       0x4
#define IFF_LOOPBACK    0x8
#define IFF_POINTOPOINT 0x10
#define IFF_RUNNING     0x40
#define IFF_NOARP       0x80
#define IFF_PROMISC     0x100
#define IFF_ALLMULTI    0x200
#define IFF_MULTICAST   0x1000
#endif

#endif /* BMAMIGA_CIRCLE_NET_IF_H_ */
