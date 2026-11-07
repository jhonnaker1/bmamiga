/*
 * bmnet.h - the sockets of the Amiga's network, BMAmiga's (src/port/bmnet.c,
 * from BMNeXT).
 *
 * Included before every file of Amiberry's SLiRP (src/slirp/), slirp_uae.cpp,
 * which runs it, and osdep/bsdsocket_host.cpp, the Amiga's bsdsocket.library
 * (-include, through Amiberry's NET_CFLAGS): their
 * socket calls (socket(), connect(), select(), and the read(), write() and
 * close() they also use on sockets) become bmnet's. bmnet's sockets have
 * descriptors of their own (BMNET_FD_BASE and up); read(), write(),
 * close(), fcntl() and ioctl() on any other descriptor go to the C library
 * as before.
 *
 * Why not the C library's sockets: on the Pi they are circle-newlib's,
 * which wait in connect() (SLiRP must not) and hold the lock on the table
 * of open files while they wait (see src/host.h). bmnet goes to the
 * kernel's TCP and UDP (host.h); the PC simulator runs the same code, with
 * its host.h on the PC's own sockets.
 *
 * The system's headers are included first, so that their declarations
 * keep their names; the macros below only change the calls made after.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_BMNET_H_
#define BMAMIGA_BMNET_H_

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#ifdef __circle__
/* newlib for Circle's sockaddr_in has no sin_zero: 8 bytes, where the
   Amiga's (and every other system's) are 16, which bsdsocket.library
   refuses ("len > sizeof(struct sockaddr_in)"). The files bmnet.h is in
   only give their addresses to bmnet, so theirs is the full one. */
#define sockaddr_in bmnet_newlib_sockaddr_in
#include <netinet/in.h>
#undef sockaddr_in
struct sockaddr_in {
	sa_family_t sin_family;
	in_port_t sin_port;
	struct in_addr sin_addr;
	char sin_zero[8];
};
#else
#include <netinet/in.h>
#endif
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef __cplusplus
/* C++'s own, first: their std::bind(), read() members and the like would
   meet the macros below otherwise. */
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#endif

/* On the Pi, BMAmiga's (src/port/circle/include) and newlib's. */
#include <sys/ioctl.h>
#include <sys/uio.h>
#ifndef SHUT_RD
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2
#endif
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif
#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK ((in_addr_t) 0x7f000001)
#endif

#ifdef __circle__
/* What newlib for Circle's headers haven't, for bsdsocket.library (osdep/
   bsdsocket_host.cpp): names it maps the Amiga's to and from. The errors
   are never given here; the option and protocol numbers go to bmnet's
   setsockopt(), which takes any. */
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT 2001
#endif
#ifndef ESHUTDOWN
#define ESHUTDOWN 2002
#endif
#ifndef IPPROTO_IGMP
#define IPPROTO_IGMP 2
#endif
#ifndef IPPROTO_EGP
#define IPPROTO_EGP 8
#endif
#ifndef IPPROTO_PUP
#define IPPROTO_PUP 12
#endif
#ifndef IPPROTO_IDP
#define IPPROTO_IDP 22
#endif
#ifndef IPPROTO_ENCAP
#define IPPROTO_ENCAP 98
#endif
#ifndef IP_RECVRETOPTS
#define IP_RECVRETOPTS 6
#endif
#ifndef TCP_MAXSEG
#define TCP_MAXSEG 2
#endif
/* The network interfaces' list (SIOCGIFCONF, which bmnet's ioctl()
   refuses: the Amiga is told there are none). */
#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif
#ifndef SIOCGIFCONF
#define SIOCGIFCONF   0xc0106924
#define SIOCGIFADDR   0xc0206921
#define SIOCGIFFLAGS  0xc0206911
#endif
struct ifreq {
	char ifr_name[IFNAMSIZ];
	union {
		struct sockaddr ifru_addr;
		short ifru_flags;
	} ifr_ifru;
};
#define ifr_addr  ifr_ifru.ifru_addr
#define ifr_flags ifr_ifru.ifru_flags
struct ifconf {
	int ifc_len;
	union {
		char *ifcu_buf;
		struct ifreq *ifcu_req;
	} ifc_ifcu;
};
#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req
/* What newlib for Circle's netdb.h hasn't (bsdsocket.library's names). */
struct hostent {
	char *h_name;
	char **h_aliases;
	int h_addrtype;
	int h_length;
	char **h_addr_list;
};
#define h_addr h_addr_list[0]
struct protoent {
	char *p_name;
	char **p_aliases;
	int p_proto;
};
struct servent {
	char *s_name;
	char **s_aliases;
	int s_port;
	char *s_proto;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* bmnet's descriptors, and a set of them for select(). */
#define BMNET_FD_BASE 1024
#define BMNET_MAX     256

typedef struct {
	uint32_t bits[BMNET_MAX / 32];
} bmnet_fd_set;

static inline int bmnet_fd_index(int fd)
{
	return fd >= BMNET_FD_BASE && fd < BMNET_FD_BASE + BMNET_MAX ? fd - BMNET_FD_BASE : -1;
}

static inline void bmnet_fd_put(int fd, bmnet_fd_set *set, int on)
{
	int i = bmnet_fd_index(fd);
	if (set == NULL || i < 0)
		return;
	if (on)
		set->bits[i / 32] |= 1u << (i % 32);
	else
		set->bits[i / 32] &= ~(1u << (i % 32));
}

static inline int bmnet_fd_get(int fd, const bmnet_fd_set *set)
{
	int i = bmnet_fd_index(fd);
	return set != NULL && i >= 0 && (set->bits[i / 32] >> (i % 32)) & 1;
}

int bmnet_socket(int domain, int type, int protocol);
int bmnet_bind(int fd, const struct sockaddr *addr, socklen_t len);
int bmnet_listen(int fd, int backlog);
int bmnet_accept(int fd, struct sockaddr *addr, socklen_t *len);
int bmnet_connect(int fd, const struct sockaddr *addr, socklen_t len);
ssize_t bmnet_send(int fd, const void *buf, size_t len, int flags);
ssize_t bmnet_recv(int fd, void *buf, size_t len, int flags);
ssize_t bmnet_sendto(int fd, const void *buf, size_t len, int flags,
                     const struct sockaddr *to, socklen_t tolen);
ssize_t bmnet_recvfrom(int fd, void *buf, size_t len, int flags,
                       struct sockaddr *from, socklen_t *fromlen);
int bmnet_shutdown(int fd, int how);
int bmnet_setsockopt(int fd, int level, int name, const void *value, socklen_t len);
int bmnet_getsockopt(int fd, int level, int name, void *value, socklen_t *len);
int bmnet_getsockname(int fd, struct sockaddr *addr, socklen_t *len);
int bmnet_getpeername(int fd, struct sockaddr *addr, socklen_t *len);
int bmnet_select(int nfds, bmnet_fd_set *r, bmnet_fd_set *w, bmnet_fd_set *x,
                 struct timeval *timeout);
int bmnet_close(int fd);
ssize_t bmnet_read(int fd, void *buf, size_t len);
ssize_t bmnet_write(int fd, const void *buf, size_t len);
ssize_t bmnet_readv(int fd, const struct iovec *iov, int n);
ssize_t bmnet_writev(int fd, const struct iovec *iov, int n);
int bmnet_ioctl(int fd, unsigned long request, ...);
int bmnet_fcntl(int fd, int cmd, ...);
int bmnet_gethostname(char *name, size_t len);
int bmnet_getaddrinfo(const char *node, const char *service,
                      const struct addrinfo *hints, struct addrinfo **res);
/* fopen(), but /etc/resolv.conf (SLiRP's DNS server, slirp.cpp) is the
   DNS server the network gave (host_dns_server()): the Pi has no such
   file. */
FILE *bmnet_fopen(const char *path, const char *mode);
/* For bsdsocket.library (osdep/bsdsocket_host.cpp): pipes (a pair of
   127.0.0.1 sockets), and names through the network's DNS server. */
int bmnet_pipe(int fds[2]);
int bmnet_dup(int fd);
extern int bmnet_h_errno;
struct hostent *bmnet_gethostbyname(const char *name);
struct hostent *bmnet_gethostbyaddr(const void *addr, socklen_t len, int type);
struct protoent *bmnet_getprotobyname(const char *name);
struct protoent *bmnet_getprotobynumber(int number);
struct servent *bmnet_getservbyname(const char *name, const char *proto);
struct servent *bmnet_getservbyport(int port, const char *proto);

#ifdef __cplusplus
}
#endif

#ifndef BMNET_IMPLEMENTATION

#define fd_set bmnet_fd_set
#undef FD_ZERO
#undef FD_SET
#undef FD_CLR
#undef FD_ISSET
#define FD_ZERO(set)      memset((set), 0, sizeof(bmnet_fd_set))
#define FD_SET(fd, set)   bmnet_fd_put((fd), (set), 1)
#define FD_CLR(fd, set)   bmnet_fd_put((fd), (set), 0)
#define FD_ISSET(fd, set) bmnet_fd_get((fd), (set))

#define socket(d, t, p)            bmnet_socket(d, t, p)
#define bind(f, a, l)              bmnet_bind(f, a, l)
#define listen(f, b)               bmnet_listen(f, b)
#define accept(f, a, l)            bmnet_accept(f, a, l)
#define connect(f, a, l)           bmnet_connect(f, a, l)
#define send(f, b, l, fl)          bmnet_send(f, b, l, fl)
#define recv(f, b, l, fl)          bmnet_recv(f, b, l, fl)
#define sendto(f, b, l, fl, t, tl) bmnet_sendto(f, b, l, fl, t, tl)
#define recvfrom(f, b, l, fl, s, sl) bmnet_recvfrom(f, b, l, fl, s, sl)
#define shutdown(f, h)             bmnet_shutdown(f, h)
#define setsockopt(f, lv, n, v, l) bmnet_setsockopt(f, lv, n, v, l)
#define getsockopt(f, lv, n, v, l) bmnet_getsockopt(f, lv, n, v, l)
#define getsockname(f, a, l)       bmnet_getsockname(f, a, l)
#define getpeername(f, a, l)       bmnet_getpeername(f, a, l)
#define select(n, r, w, x, t)      bmnet_select(n, r, w, x, t)
#define close(f)                   bmnet_close(f)
#define read(f, b, l)              bmnet_read(f, b, l)
#define write(f, b, l)             bmnet_write(f, b, l)
#define readv(f, v, n)             bmnet_readv(f, v, n)
#define writev(f, v, n)            bmnet_writev(f, v, n)
#define ioctl(...)                 bmnet_ioctl(__VA_ARGS__)
#define fcntl(...)                 bmnet_fcntl(__VA_ARGS__)
#define gethostname(n, l)          bmnet_gethostname(n, l)
#define getaddrinfo(n, s, h, r)    bmnet_getaddrinfo(n, s, h, r)
#define fopen(p, m)                bmnet_fopen(p, m)
#define pipe(f)                    bmnet_pipe(f)
#define dup(f)                     bmnet_dup(f)
#undef h_errno
#define h_errno                    bmnet_h_errno
#define gethostbyname(n)           bmnet_gethostbyname(n)
#define gethostbyaddr(a, l, t)     bmnet_gethostbyaddr(a, l, t)
#define getprotobyname(n)          bmnet_getprotobyname(n)
#define getprotobynumber(n)        bmnet_getprotobynumber(n)
#define getservbyname(n, p)        bmnet_getservbyname(n, p)
#define getservbyport(n, p)        bmnet_getservbyport(n, p)
/* bmnet's descriptors are 1024 and up. */
#undef FD_SETSIZE
#define FD_SETSIZE                 (BMNET_FD_BASE + BMNET_MAX)

#endif /* BMNET_IMPLEMENTATION */

#endif /* BMAMIGA_BMNET_H_ */
