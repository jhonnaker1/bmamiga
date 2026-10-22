/*
 * nosockets.cpp - the Amiga's bsdsocket.library without a network, for
 * Amiberry on Circle, in place of src/osdep/bsdsocket_host.cpp.
 *
 * Amiberry's runs the Amiga's sockets on the host's (BSD sockets, select(),
 * a thread per blocking call). On the Pi the C library's sockets would
 * block the emulator while they wait (circle-stdlib's hold its file table
 * meanwhile), so BMAmiga has no Amiga networking yet: init_socket_layer()
 * fails, and bsdsocket.library isn't offered to the Amiga
 * (bsdlib_install()); the rest is never called, and fails if it is.
 *
 * Compiled with Amiberry's flags (src/port/amiberry.mk, BMA_EXTRA).
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include "sysconfig.h"
#include "sysdeps.h"
#include "traps.h"
#include "bsdsocket.h"

int init_socket_layer(void)
{
	write_log(_T("BSDSOCK: no network on the Pi yet\n"));
	return 0;
}

void deinit_socket_layer(void)
{
}

void locksigqueue(void)
{
}

void unlocksigqueue(void)
{
}

void sockabort(SB)
{
	(void) sb;
}

int host_sbinit(TrapContext *ctx, SB)
{
	(void) ctx;
	(void) sb;
	return 0;
}

void host_sbcleanup(SB)
{
	(void) sb;
}

void host_sbreset(void)
{
}

void host_closesocketquick(SOCKET_TYPE s)
{
	(void) s;
}

int host_dup2socket(TrapContext *ctx, SB, int fd1, int fd2)
{
	(void) ctx; (void) sb; (void) fd1; (void) fd2;
	return -1;
}

int host_socket(TrapContext *ctx, SB, int af, int type, int protocol)
{
	(void) ctx; (void) sb; (void) af; (void) type; (void) protocol;
	return -1;
}

uae_u32 host_bind(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	(void) ctx; (void) sb; (void) sd; (void) name; (void) namelen;
	return (uae_u32) -1;
}

uae_u32 host_listen(TrapContext *ctx, SB, uae_u32 sd, uae_u32 backlog)
{
	(void) ctx; (void) sb; (void) sd; (void) backlog;
	return (uae_u32) -1;
}

void host_accept(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	(void) ctx; (void) sb; (void) sd; (void) name; (void) namelen;
}

void host_sendto(TrapContext *ctx, SB, uae_u32 sd, uae_u32 msg, uae_u8 *hmsg, uae_u32 len,
                 uae_u32 flags, uae_u32 to, uae_u32 tolen)
{
	(void) ctx; (void) sb; (void) sd; (void) msg; (void) hmsg; (void) len;
	(void) flags; (void) to; (void) tolen;
}

void host_recvfrom(TrapContext *ctx, SB, uae_u32 sd, uae_u32 msg, uae_u8 *hmsg, uae_u32 len,
                   uae_u32 flags, uae_u32 addr, uae_u32 addrlen)
{
	(void) ctx; (void) sb; (void) sd; (void) msg; (void) hmsg; (void) len;
	(void) flags; (void) addr; (void) addrlen;
}

uae_u32 host_shutdown(SB, uae_u32 sd, uae_u32 how)
{
	(void) sb; (void) sd; (void) how;
	return (uae_u32) -1;
}

void host_setsockopt(SB, uae_u32 sd, uae_u32 level, uae_u32 optname, uae_u32 optval, uae_u32 len)
{
	(void) sb; (void) sd; (void) level; (void) optname; (void) optval; (void) len;
}

uae_u32 host_getsockopt(TrapContext *ctx, SB, uae_u32 sd, uae_u32 level, uae_u32 optname,
                        uae_u32 optval, uae_u32 optlen)
{
	(void) ctx; (void) sb; (void) sd; (void) level; (void) optname; (void) optval; (void) optlen;
	return (uae_u32) -1;
}

uae_u32 host_getsockname(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	(void) ctx; (void) sb; (void) sd; (void) name; (void) namelen;
	return (uae_u32) -1;
}

uae_u32 host_getpeername(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	(void) ctx; (void) sb; (void) sd; (void) name; (void) namelen;
	return (uae_u32) -1;
}

uae_u32 host_IoctlSocket(TrapContext *ctx, SB, uae_u32 sd, uae_u32 request, uae_u32 arg)
{
	(void) ctx; (void) sb; (void) sd; (void) request; (void) arg;
	return (uae_u32) -1;
}

int host_CloseSocket(TrapContext *ctx, SB, int sd)
{
	(void) ctx; (void) sb; (void) sd;
	return -1;
}

void host_connect(TrapContext *ctx, SB, uae_u32 sd, uae_u32 name, uae_u32 namelen)
{
	(void) ctx; (void) sb; (void) sd; (void) name; (void) namelen;
}

void host_WaitSelect(TrapContext *ctx, SB, uae_u32 nfds, uae_u32 readfds, uae_u32 writefds,
                     uae_u32 exceptfds, uae_u32 timeout, uae_u32 sigmp)
{
	(void) ctx; (void) sb; (void) nfds; (void) readfds; (void) writefds;
	(void) exceptfds; (void) timeout; (void) sigmp;
}

void host_gethostbynameaddr(TrapContext *ctx, SB, uae_u32 name, uae_u32 namelen, long addrtype)
{
	(void) ctx; (void) sb; (void) name; (void) namelen; (void) addrtype;
}

void host_getservbynameport(TrapContext *ctx, SB, uae_u32 name, uae_u32 proto, uae_u32 type)
{
	(void) ctx; (void) sb; (void) name; (void) proto; (void) type;
}

void host_getprotobyname(TrapContext *ctx, SB, uae_u32 name)
{
	(void) ctx; (void) sb; (void) name;
}

void host_getprotobynumber(TrapContext *ctx, SB, uae_u32 number)
{
	(void) ctx; (void) sb; (void) number;
}
