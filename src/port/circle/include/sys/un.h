/*
 * sys/un.h for Amiberry on Circle (no local sockets: the address only).
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_CIRCLE_SYS_UN_H_
#define BMAMIGA_CIRCLE_SYS_UN_H_

#include <sys/socket.h>

struct sockaddr_un {
	sa_family_t sun_family;
	char sun_path[108];
};

#endif /* BMAMIGA_CIRCLE_SYS_UN_H_ */
