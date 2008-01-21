/**
 *  Copyright (C) 2007 iptego GmbH
 *
 *  This file is part of libbinrpc.
 *
 *  libbinrpc is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  libbinrpc is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __BRPC_NET_H__
#define __BRPC_NET_H__

#include <sys/un.h>
#include <netinet/in.h>
#include <stdbool.h>

#include "time.h"
#include "call.h"


typedef struct brpc_addr_s {
	sa_family_t domain;
	int socktype;
	union {
		struct sockaddr_un un;
		struct sockaddr_in in4;
		struct sockaddr_in6 in6;
	} sockaddr;
	socklen_t addrlen;
} brpc_addr_t;


#define BRPC_ADDR_DOMAIN(_addr_)	(_addr_)->domain
#define BRPC_ADDR_TYPE(_addr_)		(_addr_)->socktype
#define BRPC_ADDR_LEN(_addr_)		(_addr_)->addrlen
#define BRPC_ADDR_ADDR(_addr_)		((struct sockaddr *)&(_addr_)->sockaddr)
#define BRPC_ADDR_UN(_addr_)		(&(_addr_)->sockaddr.un)
#define BRPC_ADDR_INET(_addr_)		(&(_addr_)->sockaddr.in4)
#define BRPC_ADDR_INET6(_addr_)		(&(_addr_)->sockaddr.in6)

/**
 * Resolve a BINRPC uri to a socket address.
 * @param uri The BINRPC URI: brpcXY://address
 * 'X' stands for either `n', for internet address families, `l' for local
 * domain (unix sockets), `4' or `6' for IPv4 or IPv6 families (force choice).
 * 'Y' stands for `s' for stream oriented sockets, or `d' for datagram ones.
 * @return A static pointer to a socket address.
 */
brpc_addr_t *brpc_parse_uri(const char *uri);
char *brpc_print_addr(const brpc_addr_t *addr);
bool brpc_addr_eq(const brpc_addr_t *a, const brpc_addr_t *b);

int brpc_socket(brpc_addr_t *addr, bool blocking, bool named);
bool brpc_connect(brpc_addr_t *addr, int *sockfd, brpc_tv_t tout);
bool brpc_sendto(int sockfd, brpc_addr_t *dest, brpc_t *msg, brpc_tv_t tout);
brpc_t *brpc_recvfrom(int sockfd, brpc_addr_t *src, brpc_tv_t tout);

#define brpc_send(sockfd, msg, tout)	brpc_sendto(sockfd, NULL, msg, tout)
#define brpc_recv(sockfd, tout)			brpc_recvfrom(sockfd, NULL, tout)

#define BRPC_URI_PREFIX	"brpcXY://"

/* Maximum number of packets to cache in one read. */
#define BRPC_STRCV_MAX_PKT_CNT	16
#define BRPC_STRCV_BUFF_SIZE	BRPC_STRCV_MAX_PKT_CNT * BINRPC_MAX_PKT_LEN

typedef struct {
	int fd; /**< where to read from */
	/* TODO: enlarge also the socket recv buffer size */
	uint8_t buff[BRPC_STRCV_BUFF_SIZE]; /**< buffer to hold received data */
	ssize_t offset; /**< where to write rcvd data in buff */
	/** 
	 * How large is the BINRPC paket to be received, or, if negative, how many
	 * bytes more are needed to be read, to find the lenght 
	 * It must be init'ed to -MIN_PKT_LEN !
	 */
	ssize_t pkt_len;
} brpc_strd_t; /* stateful/stream read */

/**
 * Set a BINRPC read state object (~=declare: once per instance).
 * @param _state_ Reference to object.
 * @param fd File descriptor to read from.
 */
#define brpc_strd_init(_state_, _fd) \
	do { \
		(_state_)->fd = _fd; \
		(_state_)->offset = 0; \
		(_state_)->pkt_len = -MIN_PKT_LEN; \
	} while (0)

/**
 * Read available data from peer.
 * @param state BINRPC read state.
 * @return State of operation: true - something was read; otherwise ECONNRESET,
 * if 0 was read; system error on other cases.
 */
bool brpc_strd_read(brpc_strd_t *state);
/**
 * Get buffer holding a BINRPC packet.
 * @param state BINRPC read state
 * @param len Output parameter: lenght of packet
 * @return Packet buffer; NULL on unavailable or error (in the latter case, 
 * EMSGSIZE may be set if packet to large).
 */
uint8_t *brpc_strd_wirepkt(brpc_strd_t *state, size_t *len);

#endif /* __BRPC_NET_H__ */
