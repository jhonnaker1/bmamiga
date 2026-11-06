/*
 * bmnet.c - the sockets of the Amiga's network, for BMAmiga (from BMNeXT).
 *
 * Amiberry's SLiRP (src/slirp/) turns the Amiga's connections (from
 * uaenet.device or an emulated network card) into the host's, with BSD
 * sockets; here they are BMAmiga's (bmnet.h says why, and how the calls
 * get here):
 *
 *   127.0.0.1   in memory: UDP datagrams go straight to the socket bound to
 *               their port; a TCP connect() to a listening socket makes a
 *               pair of sockets, each with a buffer the other writes into
 *   the rest    the kernel's TCP and UDP (host.h): UDP sends and receives
 *               without waiting; a TCP connect, which can take seconds,
 *               runs in the background (host_run_in_background()), and the
 *               socket turns writable when it is done, as SLiRP's
 *               non-blocking connect() expects
 *
 * select() waits as long as it is asked to (bsdsocket.library's
 * WaitSelect() needs that), letting the emulator run meanwhile, 10 ms at a
 * time. Names are looked up with the network's DNS server (host.h's
 * host_dns_server()), for bsdsocket.library's gethostbyname(). All on the emulator's core, on its threads
 * (src/port/circle/threads.c), but for the background connects, which only
 * fill in their own job.
 *
 * Nothing comes in from outside: a listening socket bound to all addresses
 * (SLiRP's redirections of the host's ports to the Amiga's) is only ever
 * reached from 127.0.0.1.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#define BMNET_IMPLEMENTATION
#include "bmnet.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "../host.h"

int bma_usleep(useconds_t us);   /* clock.c: lets the other threads run */

#define RING_SIZE   (64 * 1024)   /* a 127.0.0.1 TCP socket's buffer */
#define MAX_QUEUED  32            /* datagrams waiting on a UDP socket */
#define MAX_PENDING 16            /* connections waiting to be accepted */
#define XBUF_SIZE   2048          /* read ahead from the kernel's TCP */

enum {
	S_FREE,
	S_NEW,         /* made, not connected or listening */
	S_LISTEN,
	S_CONNECTING,  /* the kernel's connect in the background */
	S_CONNECTED,
	S_FAILED,      /* the connect failed (err) */
	S_CLOSED,      /* the other end has gone */
	S_ORPHAN       /* closed while connecting: free once the connect ends */
};

struct dgram {
	struct dgram *next;
	uint32_t ip;          /* where from, network byte order */
	uint16_t port;
	size_t len;
	uint8_t data[];
};

struct connect_job {
	volatile int done;
	int result;           /* host_tcp_connect()'s */
	char host[16], port[8];
};

struct bsock {
	int state;
	int type;             /* SOCK_STREAM or SOCK_DGRAM */
	int err;              /* for SO_ERROR */
	uint32_t lip, rip;    /* network byte order */
	uint16_t lport, rport;
	/* 127.0.0.1 */
	int lo;
	int peer;             /* the other end's index, -1 */
	int peer_gone;        /* the other end has closed (or shut down) */
	uint8_t *ring;
	size_t ring_head, ring_len;
	struct dgram *dq_head, *dq_tail;
	int dq_count;
	int pending[MAX_PENDING];
	int npending;
	/* the kernel's */
	int tcp, udp;
	struct connect_job *job;
	uint8_t *xbuf;
	size_t xpos, xlen;
	struct dgram *xdg;
	/* made with BSD's numbers (the Amiga's, see bmnet_socket()): its
	   addresses go back with BSD's AF_INET too */
	int bsd;
};

static struct bsock socks[BMNET_MAX];
static uint16_t next_port = 49152;

/* ---- Helpers --------------------------------------------------------------- */

static struct bsock *get(int fd)
{
	int i = bmnet_fd_index(fd);
	if (i < 0 || socks[i].state == S_FREE || socks[i].state == S_ORPHAN)
		return NULL;
	return &socks[i];
}

static int fd_of(struct bsock *s)
{
	return (int) (s - socks) + BMNET_FD_BASE;
}

static int fail(int e)
{
	errno = e;
	return -1;
}

static int is_loopback(uint32_t ip)
{
	return (ntohl(ip) & 0xff000000u) == 0x7f000000u;
}

static int port_in_use(int type, uint16_t port)
{
	int i;
	for (i = 0; i < BMNET_MAX; i++)
		if (socks[i].state != S_FREE && socks[i].type == type && socks[i].lport == port)
			return 1;
	return 0;
}

static uint16_t ephemeral_port(int type)
{
	int tries;
	for (tries = 0; tries < 16384; tries++) {
		uint16_t p = htons(next_port);
		next_port = next_port >= 65535 ? 49152 : next_port + 1;
		if (!port_in_use(type, p))
			return p;
	}
	return 0;
}

/* BSD's AF_INET, the Amiga's: bsdsocket.library hands the family on as it
   is (only Haiku's build turns it into 2), and the Amiga checks it. */
#define BSD_AF_INET 2

static void fill_addr(const struct bsock *s, struct sockaddr *addr, socklen_t *len, uint32_t ip,
                      uint16_t port)
{
	struct sockaddr_in in;
	if (addr == NULL || len == NULL)
		return;
	memset(&in, 0, sizeof in);
	in.sin_family = s != NULL && s->bsd ? BSD_AF_INET : AF_INET;
	in.sin_addr.s_addr = ip;
	in.sin_port = port;
	if (*len > sizeof in)
		*len = sizeof in;
	memcpy(addr, &in, *len);
	*len = sizeof in;
}

static int read_addr(const struct sockaddr *addr, socklen_t len, uint32_t *ip, uint16_t *port)
{
	const struct sockaddr_in *in = (const struct sockaddr_in *) addr;
	if (addr == NULL || len < sizeof(struct sockaddr_in)
	    || (in->sin_family != AF_INET && in->sin_family != BSD_AF_INET))   /* the Amiga's */
		return -1;
	*ip = in->sin_addr.s_addr;
	*port = in->sin_port;
	return 0;
}

static void free_queue(struct dgram **head)
{
	while (*head) {
		struct dgram *d = *head;
		*head = d->next;
		free(d);
	}
}

static void release(struct bsock *s)
{
	free(s->ring);
	free(s->xbuf);
	free(s->xdg);
	free_queue(&s->dq_head);
	memset(s, 0, sizeof *s);
	s->state = S_FREE;
	s->peer = -1;
	s->tcp = s->udp = -1;
}

/* ---- The ring of a 127.0.0.1 TCP socket ------------------------------------ */

static size_t ring_put(struct bsock *s, const uint8_t *data, size_t len)
{
	size_t n = 0;
	if (s->ring == NULL) {
		s->ring = malloc(RING_SIZE);
		if (s->ring == NULL)
			return 0;
	}
	while (n < len && s->ring_len < RING_SIZE) {
		size_t tail = (s->ring_head + s->ring_len) % RING_SIZE;
		size_t chunk = RING_SIZE - tail;
		if (chunk > RING_SIZE - s->ring_len)
			chunk = RING_SIZE - s->ring_len;
		if (chunk > len - n)
			chunk = len - n;
		memcpy(s->ring + tail, data + n, chunk);
		s->ring_len += chunk;
		n += chunk;
	}
	return n;
}

static size_t ring_get(struct bsock *s, uint8_t *data, size_t len)
{
	size_t n = 0;
	while (n < len && s->ring_len > 0) {
		size_t chunk = RING_SIZE - s->ring_head;
		if (chunk > s->ring_len)
			chunk = s->ring_len;
		if (chunk > len - n)
			chunk = len - n;
		memcpy(data + n, s->ring + s->ring_head, chunk);
		s->ring_head = (s->ring_head + chunk) % RING_SIZE;
		s->ring_len -= chunk;
		n += chunk;
	}
	return n;
}

/* ---- Background connects ------------------------------------------------- */

static void connect_job_run(void *arg)
{
	struct connect_job *job = arg;
	job->result = host_tcp_connect(job->host, job->port);
	__sync_synchronize();
	job->done = 1;
}

/* Connects that have finished: their sockets connected or failed; a socket
   closed meanwhile is freed now. */
static void reap_jobs(void)
{
	int i;
	for (i = 0; i < BMNET_MAX; i++) {
		struct bsock *s = &socks[i];
		if (s->job == NULL || !s->job->done)
			continue;
		__sync_synchronize();
		if (s->state == S_ORPHAN) {
			if (s->job->result >= 0)
				host_tcp_close(s->job->result);
			free(s->job);
			s->job = NULL;
			release(s);
			continue;
		}
		if (s->job->result >= 0) {
			s->tcp = s->job->result;
			s->state = S_CONNECTED;
		} else {
			s->state = S_FAILED;
			s->err = s->job->result == -2 ? EHOSTUNREACH : ECONNREFUSED;
		}
		free(s->job);
		s->job = NULL;
	}
}

/* ---- What has arrived from the kernel ------------------------------------- */

/* A TCP socket's read ahead: >0 bytes waiting, 0 none, -1 closed. */
static int tcp_fill(struct bsock *s)
{
	int got;
	if (s->xpos < s->xlen)
		return 1;
	if (s->tcp < 0)
		return -1;
	if (s->xbuf == NULL) {
		s->xbuf = malloc(XBUF_SIZE);
		if (s->xbuf == NULL)
			return 0;
	}
	got = host_tcp_recv(s->tcp, s->xbuf, XBUF_SIZE);
	if (got < 0) {
		host_tcp_close(s->tcp);
		s->tcp = -1;
		s->state = S_CLOSED;
		return -1;
	}
	s->xpos = 0;
	s->xlen = (size_t) got;
	return got > 0;
}

/* A UDP socket's datagram from outside, kept until read. */
static int udp_fill(struct bsock *s)
{
	uint8_t buf[2048];
	uint32_t ip;
	unsigned port;
	int got;
	if (s->xdg != NULL)
		return 1;
	if (s->udp < 0)
		return 0;
	got = host_udp_recv(s->udp, buf, sizeof buf, &ip, &port);
	if (got <= 0)
		return 0;
	s->xdg = malloc(sizeof(struct dgram) + (size_t) got);
	if (s->xdg == NULL)
		return 0;
	s->xdg->next = NULL;
	s->xdg->ip = ip;
	s->xdg->port = htons((uint16_t) port);
	s->xdg->len = (size_t) got;
	memcpy(s->xdg->data, buf, (size_t) got);
	return 1;
}

/* ---- Sockets ------------------------------------------------------------- */

int bmnet_socket(int domain, int type, int protocol)
{
	int i;
#if defined(__circle__)
	/* BSD's numbers, which the Amiga's are and bsdsocket.library passes
	   on as they are: AF_INET 2, SOCK_STREAM 1, SOCK_DGRAM 2. Circle's C
	   library has AF_INET 1 (2 is its AF_INET6, which bmnet hasn't),
	   SOCK_STREAM 4 and SOCK_DGRAM 1: the Amiga's every socket() failed
	   (errno 47, EAFNOSUPPORT: Amelinium's "no socket"). */
	int bsd = 0;
	if (domain == 2) {
		domain = AF_INET;
		type = type == 1 ? SOCK_STREAM : type == 2 ? SOCK_DGRAM : -1;
		bsd = 1;
	}
#else
	int bsd = 1;
#endif
	if (domain != AF_INET || (type != SOCK_STREAM && type != SOCK_DGRAM))
		return fail(EAFNOSUPPORT);   /* no raw (ICMP) sockets: no ping */
	for (i = 0; i < BMNET_MAX; i++) {
		if (socks[i].state == S_FREE) {
			release(&socks[i]);
			socks[i].state = S_NEW;
			socks[i].type = type;
			socks[i].bsd = bsd;
			return i + BMNET_FD_BASE;
		}
	}
	return fail(EMFILE);
}

int bmnet_bind(int fd, const struct sockaddr *addr, socklen_t len)
{
	struct bsock *s = get(fd);
	uint32_t ip;
	uint16_t port;
	if (s == NULL)
		return fail(EBADF);
	if (read_addr(addr, len, &ip, &port) < 0)
		return fail(EINVAL);
	if (port == 0)
		port = ephemeral_port(s->type);
	else if (port_in_use(s->type, port))
		return fail(EADDRINUSE);
	s->lip = ip;
	s->lport = port;
	s->lo = is_loopback(ip);
	return 0;
}

int bmnet_listen(int fd, int backlog)
{
	struct bsock *s = get(fd);
	if (s == NULL)
		return fail(EBADF);
	if (s->type != SOCK_STREAM)
		return fail(EOPNOTSUPP);
	if (s->lport == 0)
		s->lport = ephemeral_port(SOCK_STREAM);
	s->state = S_LISTEN;
	return 0;
}

int bmnet_accept(int fd, struct sockaddr *addr, socklen_t *len)
{
	struct bsock *s = get(fd), *c;
	int i;
	if (s == NULL)
		return fail(EBADF);
	if (s->state != S_LISTEN)
		return fail(EINVAL);
	if (s->npending == 0)
		return fail(EAGAIN);
	i = s->pending[0];
	memmove(s->pending, s->pending + 1, (size_t) (s->npending - 1) * sizeof s->pending[0]);
	s->npending--;
	c = &socks[i];
	c->bsd = s->bsd;
	fill_addr(c, addr, len, c->rip, c->rport);
	return fd_of(c);
}

/* A TCP connect to 127.0.0.1: to a listening socket's waiting list. */
static int connect_loopback(struct bsock *s, uint32_t ip, uint16_t port)
{
	int i, j;
	for (i = 0; i < BMNET_MAX; i++) {
		struct bsock *l = &socks[i];
		if (l->state != S_LISTEN || l->lport != port || !(l->lo || l->lip == INADDR_ANY))
			continue;
		if (l->npending >= MAX_PENDING)
			break;
		for (j = 0; j < BMNET_MAX; j++)
			if (socks[j].state == S_FREE)
				break;
		if (j == BMNET_MAX)
			return fail(EMFILE);
		release(&socks[j]);
		if (s->lport == 0)
			s->lport = ephemeral_port(SOCK_STREAM);
		s->lip = htonl(INADDR_LOOPBACK);
		s->lo = 1;
		s->rip = ip;
		s->rport = port;
		s->state = S_CONNECTED;
		s->peer = j;
		socks[j].state = S_CONNECTED;
		socks[j].type = SOCK_STREAM;
		socks[j].lo = 1;
		socks[j].lip = htonl(INADDR_LOOPBACK);
		socks[j].lport = port;
		socks[j].rip = s->lip;
		socks[j].rport = s->lport;
		socks[j].peer = (int) (s - socks);
		l->pending[l->npending++] = j;
		return 0;
	}
	s->state = S_FAILED;
	s->err = ECONNREFUSED;
	return fail(ECONNREFUSED);
}

int bmnet_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
	struct bsock *s = get(fd);
	uint32_t ip;
	uint16_t port;
	struct connect_job *job;
	struct in_addr a;
	if (s == NULL)
		return fail(EBADF);
	if (read_addr(addr, len, &ip, &port) < 0)
		return fail(EINVAL);
	if (s->type == SOCK_DGRAM) {
		/* A default destination for send(). */
		s->rip = ip;
		s->rport = port;
		return 0;
	}
	if (s->state != S_NEW)
		return fail(s->state == S_CONNECTING ? EALREADY : EISCONN);
	if (is_loopback(ip))
		return connect_loopback(s, ip, port);
	job = calloc(1, sizeof *job);
	if (job == NULL)
		return fail(ENOMEM);
	a.s_addr = ip;
	snprintf(job->host, sizeof job->host, "%s", inet_ntoa(a));
	snprintf(job->port, sizeof job->port, "%u", (unsigned) ntohs(port));
	s->rip = ip;
	s->rport = port;
	s->job = job;
	s->state = S_CONNECTING;
	if (!host_run_in_background(connect_job_run, job)) {
		free(job);
		s->job = NULL;
		s->state = S_FAILED;
		s->err = ENETUNREACH;
		return fail(ENETUNREACH);
	}
	return fail(EINPROGRESS);
}

/* ---- Sending and receiving ------------------------------------------------- */

static ssize_t stream_send(struct bsock *s, const void *buf, size_t len)
{
	int sent;
	switch (s->state) {
	case S_CONNECTING:
		return fail(EAGAIN);
	case S_FAILED: {
		int e = s->err ? s->err : ECONNREFUSED;
		return fail(e);
	}
	case S_CONNECTED:
		break;
	default:
		return fail(s->state == S_CLOSED ? EPIPE : ENOTCONN);
	}
	if (len == 0)
		return 0;
	if (s->lo) {
		size_t n;
		if (s->peer < 0 || s->peer_gone)
			return fail(EPIPE);
		n = ring_put(&socks[s->peer], buf, len);
		return n > 0 ? (ssize_t) n : fail(EAGAIN);
	}
	sent = host_tcp_send(s->tcp, buf, (unsigned) len);
	if (sent < 0) {
		s->state = S_CLOSED;
		return fail(EPIPE);
	}
	return sent > 0 ? sent : fail(EAGAIN);
}

/* PEEK: MSG_PEEK, which leaves what it reads to be read again. */
static ssize_t stream_recv(struct bsock *s, void *buf, size_t len, int peek)
{
	size_t n;
	int r;
	if (s->state == S_CONNECTING)
		return fail(EAGAIN);
	if (s->state == S_FAILED)
		return fail(s->err ? s->err : ECONNREFUSED);
	if (len == 0)
		return 0;
	if (s->lo) {
		if (peek) {
			size_t head = s->ring_head, have = s->ring_len;
			n = ring_get(s, buf, len);
			s->ring_head = head;
			s->ring_len = have;
		} else {
			n = ring_get(s, buf, len);
		}
		if (n > 0)
			return (ssize_t) n;
		return s->peer_gone || s->peer < 0 ? 0 : fail(EAGAIN);
	}
	r = tcp_fill(s);
	if (r < 0)
		return 0;    /* closed: end of file */
	if (r == 0)
		return fail(EAGAIN);
	n = s->xlen - s->xpos;
	if (n > len)
		n = len;
	memcpy(buf, s->xbuf + s->xpos, n);
	if (!peek)
		s->xpos += n;
	return (ssize_t) n;
}

/* A datagram to 127.0.0.1:PORT: to the socket bound to it. */
static ssize_t loopback_sendto(struct bsock *s, const void *buf, size_t len, uint16_t port)
{
	int i;
	if (s->lport == 0)
		s->lport = ephemeral_port(SOCK_DGRAM);
	for (i = 0; i < BMNET_MAX; i++) {
		struct bsock *d = &socks[i];
		struct dgram *g;
		if (d->state == S_FREE || d->state == S_ORPHAN || d->type != SOCK_DGRAM
		    || d->lport != port || !(d->lo || d->lip == INADDR_ANY))
			continue;
		if (d->dq_count >= MAX_QUEUED)
			return (ssize_t) len;   /* dropped, as a full network would */
		g = malloc(sizeof *g + len);
		if (g == NULL)
			return fail(ENOMEM);
		g->next = NULL;
		g->ip = htonl(INADDR_LOOPBACK);
		g->port = s->lport;
		g->len = len;
		memcpy(g->data, buf, len);
		if (d->dq_tail)
			d->dq_tail->next = g;
		else
			d->dq_head = g;
		d->dq_tail = g;
		d->dq_count++;
		return (ssize_t) len;
	}
	return (ssize_t) len;              /* nobody there: lost, as UDP is */
}

ssize_t bmnet_sendto(int fd, const void *buf, size_t len, int flags,
                     const struct sockaddr *to, socklen_t tolen)
{
	struct bsock *s = get(fd);
	uint32_t ip;
	uint16_t port;
	int sent;
	if (s == NULL)
		return bmnet_write(fd, buf, len);
	if (s->type == SOCK_STREAM)
		return stream_send(s, buf, len);
	if (to == NULL) {
		ip = s->rip;
		port = s->rport;
	} else if (read_addr(to, tolen, &ip, &port) < 0) {
		return fail(EINVAL);
	}
	if (port == 0)
		return fail(EDESTADDRREQ);
	if (is_loopback(ip))
		return loopback_sendto(s, buf, len, port);
	if (s->udp < 0) {
		s->udp = host_udp_open();
		if (s->udp < 0)
			return fail(ENETUNREACH);
	}
	sent = host_udp_send(s->udp, ip, ntohs(port), buf, (unsigned) len);
	if (sent < 0)
		return fail(ENETUNREACH);
	return sent > 0 ? sent : (ssize_t) len;   /* a full queue drops it */
}

/* MSG_PEEK: also the Amiga's (BSD's, 2), which bsdsocket.library passes on
   as it is; on Circle 2 is MSG_DONTROUTE, which nothing here sends. */
#if defined(__circle__)
#define PEEK_FLAGS (MSG_PEEK | 2)
#else
#define PEEK_FLAGS MSG_PEEK
#endif

ssize_t bmnet_recvfrom(int fd, void *buf, size_t len, int flags,
                       struct sockaddr *from, socklen_t *fromlen)
{
	struct bsock *s = get(fd);
	struct dgram *g;
	size_t n;
	if (s == NULL)
		return bmnet_read(fd, buf, len);
	if (s->type == SOCK_STREAM) {
		ssize_t r = stream_recv(s, buf, len, (flags & PEEK_FLAGS) != 0);
		if (r >= 0)
			fill_addr(s, from, fromlen, s->rip, s->rport);
		return r;
	}
	if (flags & PEEK_FLAGS) {
		g = s->dq_head != NULL ? s->dq_head : udp_fill(s) ? s->xdg : NULL;
		if (g == NULL)
			return fail(EAGAIN);
		n = g->len < len ? g->len : len;
		memcpy(buf, g->data, n);
		fill_addr(s, from, fromlen, g->ip, g->port);
		return (ssize_t) n;
	}
	if (s->dq_head != NULL) {
		g = s->dq_head;
		s->dq_head = g->next;
		if (s->dq_head == NULL)
			s->dq_tail = NULL;
		s->dq_count--;
	} else if (udp_fill(s)) {
		g = s->xdg;
		s->xdg = NULL;
	} else {
		return fail(EAGAIN);
	}
	n = g->len < len ? g->len : len;
	memcpy(buf, g->data, n);
	fill_addr(s, from, fromlen, g->ip, g->port);
	free(g);
	return (ssize_t) n;
}

ssize_t bmnet_send(int fd, const void *buf, size_t len, int flags)
{
	return bmnet_sendto(fd, buf, len, flags, NULL, 0);
}

ssize_t bmnet_recv(int fd, void *buf, size_t len, int flags)
{
	return bmnet_recvfrom(fd, buf, len, flags, NULL, NULL);
}

ssize_t bmnet_read(int fd, void *buf, size_t len)
{
	if (bmnet_fd_index(fd) < 0)
		return read(fd, buf, len);
	return bmnet_recvfrom(fd, buf, len, 0, NULL, NULL);
}

ssize_t bmnet_write(int fd, const void *buf, size_t len)
{
	struct bsock *s;
	if (bmnet_fd_index(fd) < 0)
		return write(fd, buf, len);
	s = get(fd);
	if (s == NULL)
		return fail(EBADF);
	if (s->type == SOCK_STREAM)
		return stream_send(s, buf, len);
	return bmnet_sendto(fd, buf, len, 0, NULL, 0);
}

ssize_t bmnet_readv(int fd, const struct iovec *iov, int n)
{
	ssize_t total = 0;
	int i;
	for (i = 0; i < n; i++) {
		ssize_t r;
		if (iov[i].iov_len == 0)
			continue;
		r = bmnet_read(fd, iov[i].iov_base, iov[i].iov_len);
		if (r < 0)
			return total > 0 ? total : r;
		total += r;
		if ((size_t) r < iov[i].iov_len)
			break;
	}
	return total;
}

ssize_t bmnet_writev(int fd, const struct iovec *iov, int n)
{
	ssize_t total = 0;
	int i;
	for (i = 0; i < n; i++) {
		ssize_t r;
		if (iov[i].iov_len == 0)
			continue;
		r = bmnet_write(fd, iov[i].iov_base, iov[i].iov_len);
		if (r < 0)
			return total > 0 ? total : r;
		total += r;
		if ((size_t) r < iov[i].iov_len)
			break;
	}
	return total;
}

/* ---- Closing ---------------------------------------------------------------- */

int bmnet_shutdown(int fd, int how)
{
	struct bsock *s = get(fd);
	if (s == NULL)
		return fail(EBADF);
	if (s->lo && s->peer >= 0 && (how == SHUT_WR || how == SHUT_RDWR))
		socks[s->peer].peer_gone = 1;   /* the other end reads to its end */
	return 0;
}

int bmnet_close(int fd)
{
	struct bsock *s;
	int i;
	if (bmnet_fd_index(fd) < 0)
		return close(fd);
	s = get(fd);
	if (s == NULL)
		return fail(EBADF);
	if (s->lo && s->peer >= 0) {
		socks[s->peer].peer_gone = 1;
		socks[s->peer].peer = -1;
	}
	if (s->state == S_LISTEN) {
		/* Connections nobody accepted: their other ends see the end. */
		for (i = 0; i < s->npending; i++) {
			struct bsock *c = &socks[s->pending[i]];
			if (c->peer >= 0) {
				socks[c->peer].peer_gone = 1;
				socks[c->peer].peer = -1;
			}
			release(c);
		}
	}
	if (s->tcp >= 0)
		host_tcp_close(s->tcp);
	if (s->udp >= 0)
		host_udp_close(s->udp);
	if (s->state == S_CONNECTING && s->job != NULL) {
		/* The connect goes on in the background: freed when it ends. */
		s->state = S_ORPHAN;
		s->tcp = s->udp = -1;
		return 0;
	}
	release(s);
	return 0;
}

/* ---- Waiting (not) ---------------------------------------------------------- */

static int readable(struct bsock *s)
{
	switch (s->state) {
	case S_LISTEN:
		return s->npending > 0;
	case S_FAILED:
	case S_CLOSED:
		return 1;
	case S_CONNECTING:
		return 0;
	default:
		break;
	}
	if (s->type == SOCK_DGRAM)
		return s->dq_count > 0 || udp_fill(s);
	if (s->state != S_CONNECTED)
		return 0;
	if (s->lo)
		return s->ring_len > 0 || s->peer_gone || s->peer < 0;
	return tcp_fill(s) != 0;
}

static int writable(struct bsock *s)
{
	if (s->type == SOCK_DGRAM)
		return 1;
	if (s->state == S_FAILED)
		return 1;
	if (s->state != S_CONNECTED)
		return 0;
	if (s->lo)
		return s->peer >= 0 && !s->peer_gone && socks[s->peer].ring_len < RING_SIZE;
	return 1;
}

int bmnet_select(int nfds, bmnet_fd_set *r, bmnet_fd_set *w, bmnet_fd_set *x,
                 struct timeval *timeout)
{
	bmnet_fd_set rr, ww;
	int fd, count;
	/* Until something is ready or the timeout is over (none: for ever),
	   letting the emulator run meanwhile, 10 ms at a time. */
	double until = timeout ? host_time() + timeout->tv_sec + timeout->tv_usec / 1e6 : 0;
	for (;;) {
		count = 0;
		reap_jobs();
		memset(&rr, 0, sizeof rr);
		memset(&ww, 0, sizeof ww);
		for (fd = BMNET_FD_BASE; fd < nfds && fd < BMNET_FD_BASE + BMNET_MAX; fd++) {
			struct bsock *s = get(fd);
			if (s == NULL)
				continue;
			if (r && bmnet_fd_get(fd, r) && readable(s)) {
				bmnet_fd_put(fd, &rr, 1);
				count++;
			}
			if (w && bmnet_fd_get(fd, w) && writable(s)) {
				bmnet_fd_put(fd, &ww, 1);
				count++;
			}
		}
		if (count > 0)
			break;
		if (timeout) {
			double left = until - host_time();
			if (left <= 0)
				break;
			bma_usleep(left > 0.01 ? 10000 : (useconds_t) (left * 1e6) + 1);
		} else {
			bma_usleep(10000);
		}
	}
	if (r)
		*r = rr;
	if (w)
		*w = ww;
	if (x)
		memset(x, 0, sizeof *x);
	return count;
}

/* ---- Options ---------------------------------------------------------------- */

int bmnet_setsockopt(int fd, int level, int name, const void *value, socklen_t len)
{
	return get(fd) ? 0 : fail(EBADF);
}

int bmnet_getsockopt(int fd, int level, int name, void *value, socklen_t *len)
{
	struct bsock *s = get(fd);
	if (s == NULL)
		return fail(EBADF);
	if (level == SOL_SOCKET && name == SO_ERROR && value && len && *len >= sizeof(int)) {
		reap_jobs();
		*(int *) value = s->state == S_FAILED ? s->err : 0;
		*len = sizeof(int);
		return 0;
	}
	if (level == SOL_SOCKET && name == SO_TYPE && value && len && *len >= sizeof(int)) {
		*(int *) value = s->type;
		*len = sizeof(int);
		return 0;
	}
	/* Anything else (SO_RCVTIMEO's struct timeval too): zero. */
	if (value && len && *len > 0)
		memset(value, 0, *len);
	return 0;
}

int bmnet_getsockname(int fd, struct sockaddr *addr, socklen_t *len)
{
	struct bsock *s = get(fd);
	if (s == NULL)
		return fail(EBADF);
	fill_addr(s, addr, len, s->lip, s->lport);
	return 0;
}

int bmnet_getpeername(int fd, struct sockaddr *addr, socklen_t *len)
{
	struct bsock *s = get(fd);
	if (s == NULL)
		return fail(EBADF);
	if (s->state != S_CONNECTED)
		return fail(ENOTCONN);
	fill_addr(s, addr, len, s->rip, s->rport);
	return 0;
}

int bmnet_ioctl(int fd, unsigned long request, ...)
{
	struct bsock *s;
	va_list ap;
	void *arg;
	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);
	if (bmnet_fd_index(fd) < 0)
		return fail(ENOTTY);      /* nothing here asks it of a file */
	s = get(fd);
	if (s == NULL)
		return fail(EBADF);
	if (request == FIONBIO)
		return 0;                 /* bmnet's sockets never wait anyway */
	if (request == FIONREAD && arg != NULL) {
		size_t n = 0;
		if (s->type == SOCK_DGRAM) {
			if (s->dq_head)
				n = s->dq_head->len;
			else if (udp_fill(s))
				n = s->xdg->len;
		} else if (s->lo) {
			n = s->ring_len;
		} else if (s->state == S_CONNECTED && tcp_fill(s) > 0) {
			n = s->xlen - s->xpos;
		}
		*(int *) arg = (int) n;
		return 0;
	}
	return fail(EINVAL);
}

int bmnet_fcntl(int fd, int cmd, ...)
{
	va_list ap;
	int arg;
	va_start(ap, cmd);
	arg = va_arg(ap, int);
	va_end(ap);
	if (bmnet_fd_index(fd) < 0)
		return fcntl(fd, cmd, arg);
	if (get(fd) == NULL)
		return fail(EBADF);
	if (cmd == F_GETFL)
		return O_RDWR | O_NONBLOCK;
	return 0;                     /* F_SETFL O_NONBLOCK: never waits anyway */
}

int bmnet_gethostname(char *name, size_t len)
{
	snprintf(name, len, "bmamiga");
	return 0;
}

/* Names aren't looked up here: SLiRP asks for its own address this way,
   and does without it (127.0.0.1). */
int bmnet_getaddrinfo(const char *node, const char *service,
                      const struct addrinfo *hints, struct addrinfo **res)
{
	return EAI_FAIL;
}

/* ---- The DNS server ---------------------------------------------------------- */

FILE *bmnet_fopen(const char *path, const char *mode)
{
	static char resolv[48];
	uint32_t ip;
	if (path != NULL && strcmp(path, "/etc/resolv.conf") == 0) {
		struct in_addr a;
		if (!host_dns_server(&ip)) {
			errno = ENOENT;
			return NULL;
		}
		a.s_addr = ip;
		snprintf(resolv, sizeof resolv, "nameserver %s\n", inet_ntoa(a));
		return fmemopen(resolv, strlen(resolv), "r");
	}
	return fopen(path, mode);
}

/* ---- Pipes ---------------------------------------------------------------- */

/* A pair of 127.0.0.1 TCP sockets: bsdsocket.library's (osdep/
   bsdsocket_host.cpp) wake-up and abort pipes, which it select()s on with
   its sockets. */
int bmnet_pipe(int fds[2])
{
	struct sockaddr_in in;
	socklen_t len = sizeof in;
	int l = bmnet_socket(AF_INET, SOCK_STREAM, 0), w, r;
	if (l < 0)
		return -1;
	memset(&in, 0, sizeof in);
	in.sin_family = AF_INET;
	in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bmnet_bind(l, (struct sockaddr *) &in, sizeof in) < 0 || bmnet_listen(l, 1) < 0
	    || bmnet_getsockname(l, (struct sockaddr *) &in, &len) < 0) {
		bmnet_close(l);
		return -1;
	}
	w = bmnet_socket(AF_INET, SOCK_STREAM, 0);
	if (w < 0 || bmnet_connect(w, (struct sockaddr *) &in, sizeof in) < 0) {
		if (w >= 0)
			bmnet_close(w);
		bmnet_close(l);
		return -1;
	}
	r = bmnet_accept(l, NULL, NULL);
	bmnet_close(l);
	if (r < 0) {
		bmnet_close(w);
		return -1;
	}
	fds[0] = r;
	fds[1] = w;
	return 0;
}

/* Not for bmnet's sockets (bsdsocket.library's Dup2Socket() fails then);
   a file's goes to the C library. */
int bmnet_dup(int fd)
{
	if (bmnet_fd_index(fd) < 0)
		return dup(fd);
	return fail(EOPNOTSUPP);
}

/* ---- Names: DNS, through the network's DNS server ------------------------- */

int bmnet_h_errno;

#define DNS_TRIES   3
#define DNS_WAIT    1.5    /* seconds a try */
#define MAX_ADDRS   8

static char host_name[256];
static uint32_t host_addrs[MAX_ADDRS];
static char *host_addr_ptrs[MAX_ADDRS + 1];
static char *host_no_aliases[1];
static struct hostent host_entry;

static struct hostent *host_result(const char *name, const uint32_t *addrs, int n)
{
	int i;
	snprintf(host_name, sizeof host_name, "%s", name);
	for (i = 0; i < n && i < MAX_ADDRS; i++) {
		host_addrs[i] = addrs[i];
		host_addr_ptrs[i] = (char *) &host_addrs[i];
	}
	host_addr_ptrs[i] = NULL;
	host_no_aliases[0] = NULL;
	host_entry.h_name = host_name;
	host_entry.h_aliases = host_no_aliases;
	host_entry.h_addrtype = BSD_AF_INET;   /* only bsdsocket.library asks */
	host_entry.h_length = 4;
	host_entry.h_addr_list = host_addr_ptrs;
	bmnet_h_errno = 0;
	return &host_entry;
}

/* Puts NAME in DNS form ("example.com" as 7example3com0); returns its
   length, or 0 if it can't be. */
static size_t dns_name(uint8_t *out, size_t size, const char *name)
{
	size_t n = 0;
	while (*name) {
		const char *dot = strchr(name, '.');
		size_t len = dot ? (size_t) (dot - name) : strlen(name);
		if (len == 0 || len > 63 || n + len + 2 > size)
			return 0;
		out[n++] = (uint8_t) len;
		memcpy(out + n, name, len);
		n += len;
		name += len + (dot ? 1 : 0);
	}
	if (n + 1 > size)
		return 0;
	out[n++] = 0;
	return n;
}

/* Skips a (maybe compressed) name at AT in MSG; returns where it ends, or
   0. If OUT, writes it there as text. */
static size_t dns_skip(const uint8_t *msg, size_t len, size_t at, char *out, size_t size)
{
	size_t end = 0, o = 0;
	int jumps = 0;
	while (at < len) {
		uint8_t c = msg[at];
		if (c == 0) {
			if (out && size)
				out[o > 0 ? o - 1 : 0] = '\0';
			return end ? end : at + 1;
		}
		if ((c & 0xc0) == 0xc0) {
			if (at + 1 >= len || ++jumps > 16)
				return 0;
			if (!end)
				end = at + 2;
			at = ((size_t) (c & 0x3f) << 8) | msg[at + 1];
			continue;
		}
		if (at + 1 + c > len)
			return 0;
		if (out && o + c + 1 < size) {
			memcpy(out + o, msg + at + 1, c);
			o += c;
			out[o++] = '.';
		}
		at += 1 + c;
	}
	return 0;
}

/* Asks the network's DNS server for QNAME's records of TYPE (1: A, 12:
   PTR), waiting (and letting the emulator run) for the answer. Returns
   the number of A addresses put in ADDRS, or with PTR the name in NAME
   (1); 0 if there is no such name, -1 if the server didn't answer. */
static int dns_query(const char *qname, int type, uint32_t *addrs, int max, char *name, size_t size)
{
	uint8_t q[300], a[1500];
	uint32_t server, ip;
	unsigned port;
	size_t qlen, at;
	int sock, tries, got, n = 0, i;
	static uint16_t next_id = 0x4242;
	uint16_t id = next_id++;
	if (!host_dns_server(&server))
		return -1;
	memset(q, 0, 12);
	q[0] = id >> 8, q[1] = id & 255;
	q[2] = 1;                       /* recursion desired */
	q[5] = 1;                       /* one question */
	qlen = dns_name(q + 12, sizeof q - 16, qname);
	if (qlen == 0)
		return 0;
	qlen += 12;
	q[qlen++] = 0, q[qlen++] = (uint8_t) type, q[qlen++] = 0, q[qlen++] = 1;
	sock = host_udp_open();
	if (sock < 0)
		return -1;
	for (tries = 0; tries < DNS_TRIES; tries++) {
		double until = host_time() + DNS_WAIT;
		host_udp_send(sock, server, 53, q, (unsigned) qlen);
		while (host_time() < until) {
			got = host_udp_recv(sock, a, sizeof a, &ip, &port);
			if (got <= 0) {
				bma_usleep(5000);
				continue;
			}
			if (got < 12 || a[0] != q[0] || a[1] != q[1])
				continue;
			host_udp_close(sock);
			if ((a[3] & 15) != 0)
				return 0;           /* NXDOMAIN and the like */
			at = dns_skip(a, (size_t) got, 12, NULL, 0);
			if (at == 0)
				return 0;
			at += 4;
			for (i = 0; i < ((a[6] << 8) | a[7]) && at < (size_t) got; i++) {
				size_t rd;
				int rtype, rlen;
				at = dns_skip(a, (size_t) got, at, NULL, 0);
				if (at == 0 || at + 10 > (size_t) got)
					break;
				rtype = (a[at] << 8) | a[at + 1];
				rlen = (a[at + 8] << 8) | a[at + 9];
				rd = at + 10;
				if (rd + (size_t) rlen > (size_t) got)
					break;
				if (type == 1 && rtype == 1 && rlen == 4 && n < max)
					memcpy(&addrs[n++], a + rd, 4);
				else if (type == 12 && rtype == 12 && name
				         && dns_skip(a, (size_t) got, rd, name, size) != 0)
					return 1;
				at = rd + (size_t) rlen;
			}
			return n;
		}
	}
	host_udp_close(sock);
	return -1;
}

struct hostent *bmnet_gethostbyname(const char *name)
{
	struct in_addr a;
	uint32_t addrs[MAX_ADDRS];
	int n;
	if (name == NULL || *name == '\0') {
		bmnet_h_errno = HOST_NOT_FOUND;
		return NULL;
	}
	if (inet_aton(name, &a))
		return host_result(name, &a.s_addr, 1);
	if (strcasecmp(name, "localhost") == 0) {
		uint32_t lo = htonl(INADDR_LOOPBACK);
		return host_result(name, &lo, 1);
	}
	n = dns_query(name, 1, addrs, MAX_ADDRS, NULL, 0);
	if (n <= 0) {
		bmnet_h_errno = n < 0 ? TRY_AGAIN : HOST_NOT_FOUND;
		return NULL;
	}
	return host_result(name, addrs, n);
}

struct hostent *bmnet_gethostbyaddr(const void *addr, socklen_t len, int type)
{
	char qname[64], name[256];
	const uint8_t *b = addr;
	uint32_t ip;
	if (addr == NULL || len != 4 || (type != AF_INET && type != BSD_AF_INET)) {
		bmnet_h_errno = HOST_NOT_FOUND;
		return NULL;
	}
	memcpy(&ip, addr, 4);
	if (ntohl(ip) == INADDR_LOOPBACK)
		return host_result("localhost", &ip, 1);
	snprintf(qname, sizeof qname, "%u.%u.%u.%u.in-addr.arpa", b[3], b[2], b[1], b[0]);
	if (dns_query(qname, 12, NULL, 0, name, sizeof name) != 1) {
		bmnet_h_errno = HOST_NOT_FOUND;
		return NULL;
	}
	return host_result(name, &ip, 1);
}

/* ---- Protocols and services: the common ones -------------------------------- */

static const struct {
	const char *name;
	int number;
} protocols[] = {{"ip", 0}, {"icmp", 1}, {"tcp", 6}, {"udp", 17}};

static const struct {
	const char *name;
	int port;
	const char *proto;   /* NULL: both */
} services[] = {
	{"echo", 7, NULL}, {"discard", 9, NULL}, {"daytime", 13, NULL}, {"ftp-data", 20, "tcp"},
	{"ftp", 21, "tcp"}, {"ssh", 22, "tcp"}, {"telnet", 23, "tcp"}, {"smtp", 25, "tcp"},
	{"time", 37, NULL}, {"domain", 53, NULL}, {"tftp", 69, "udp"}, {"gopher", 70, "tcp"},
	{"finger", 79, "tcp"}, {"http", 80, "tcp"}, {"www", 80, "tcp"}, {"pop3", 110, "tcp"},
	{"nntp", 119, "tcp"}, {"ntp", 123, "udp"}, {"imap", 143, "tcp"}, {"irc", 194, "tcp"},
	{"https", 443, "tcp"}, {"submission", 587, "tcp"}, {"imaps", 993, "tcp"},
	{"pop3s", 995, "tcp"}, {"ircd", 6667, "tcp"},
};

static char proto_name[16];
static char *proto_no_aliases[1];
static struct protoent proto_entry;

static struct protoent *proto_result(int i)
{
	snprintf(proto_name, sizeof proto_name, "%s", protocols[i].name);
	proto_no_aliases[0] = NULL;
	proto_entry.p_name = proto_name;
	proto_entry.p_aliases = proto_no_aliases;
	proto_entry.p_proto = protocols[i].number;
	return &proto_entry;
}

struct protoent *bmnet_getprotobyname(const char *name)
{
	unsigned i;
	for (i = 0; name && i < sizeof protocols / sizeof protocols[0]; i++)
		if (strcasecmp(name, protocols[i].name) == 0)
			return proto_result((int) i);
	return NULL;
}

struct protoent *bmnet_getprotobynumber(int number)
{
	unsigned i;
	for (i = 0; i < sizeof protocols / sizeof protocols[0]; i++)
		if (protocols[i].number == number)
			return proto_result((int) i);
	return NULL;
}

static char serv_name[16], serv_proto[8];
static char *serv_no_aliases[1];
static struct servent serv_entry;

static struct servent *serv_result(int i, const char *proto)
{
	snprintf(serv_name, sizeof serv_name, "%s", services[i].name);
	snprintf(serv_proto, sizeof serv_proto, "%s",
	         proto ? proto : services[i].proto ? services[i].proto : "tcp");
	serv_no_aliases[0] = NULL;
	serv_entry.s_name = serv_name;
	serv_entry.s_aliases = serv_no_aliases;
	serv_entry.s_port = htons((uint16_t) services[i].port);
	serv_entry.s_proto = serv_proto;
	return &serv_entry;
}

static int serv_proto_ok(int i, const char *proto)
{
	return proto == NULL || services[i].proto == NULL || strcasecmp(proto, services[i].proto) == 0;
}

struct servent *bmnet_getservbyname(const char *name, const char *proto)
{
	unsigned i;
	for (i = 0; name && i < sizeof services / sizeof services[0]; i++)
		if (strcasecmp(name, services[i].name) == 0 && serv_proto_ok((int) i, proto))
			return serv_result((int) i, proto);
	return NULL;
}

struct servent *bmnet_getservbyport(int port, const char *proto)
{
	unsigned i;
	for (i = 0; i < sizeof services / sizeof services[0]; i++)
		if (htons((uint16_t) services[i].port) == (uint16_t) port && serv_proto_ok((int) i, proto))
			return serv_result((int) i, proto);
	return NULL;
}
