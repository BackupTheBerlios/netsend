/*
** $Id$
**
** netsend - a high performance filetransfer and diagnostic tool
** http://netsend.berlios.de
**
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* all values in network byte order */

#define	NS_MAGIC 0x67

enum ns_nse_nxt { NSE_NXT_DATA, NSE_NXT_DIGEST, NSE_NXT_RTT_PROBE,
		NSE_NXT_NONXT, NSE_NXT_RTT_INFO
};

struct ns_hdr {
	uint16_t magic;
	uint16_t version;
	uint32_t data_size; /* purely data, without netsend header */
	uint16_t nse_nxt_hdr; /* NSE_NXT_DATA for no header */
	uint16_t unused;
} __attribute__((packed));

/*
** netsend chaining header fields for ancillary information.
** This is a similar mechanism like the ipv6 extension header.
** This information can be used to exchange information like
** data digest, buffersize, etc.
** Like ipv6: each extension header appears only once. If a
** header apears multiple times the last extension header has significance.
** In contrast to ipv6, netsend headers have no order.
*/


/* ns_nxt_digest is the digest header extension
** Currently netsend support following digest algorithms:
**  o NULL   (0)
**  o SHA    (20)
**  o SHA256 (32)
**  o SHA512 (64)
*/

struct ns_nxt_digest {
	uint16_t  nse_nxt_hdr; /* next header */
	uint16_t  nse_len; /* length in units of 4 octets (not including the first 4 octets) */
	uint8_t  nse_dgst_type;
	uint8_t  nse_dgst_len;
	/* followed by digest data */
} __attribute__((packed));


/* this is a dummy extension header. it indicates that this
** is the last extension header AND no more data is comming!
*/

struct ns_nxt_nonxt {
	uint16_t  nse_nxt_hdr; /* next header */
	uint16_t  nse_len; /* length in units of 4 octets (not including the first 4 octets) */
	uint32_t  unused;
} __attribute__((packed));


/* rtt packet */

enum ns_rtt_type { RTT_REQUEST_TYPE = 0, RTT_REPLY_TYPE };

struct ns_rtt_probe {
	uint16_t  nse_nxt_hdr; /* next header */
	uint16_t  nse_len; /* length in units of 4 octets (not including the first 4 octets) */
	uint16_t  type; /* on of ns_rtt_type */
	uint16_t  unused; /* checksum */
	uint16_t  ident; /* packetstream identifier */
	uint16_t  seq_no;
	uint32_t  sec;
	uint32_t  usec;
	/* variable data */
} __attribute__((packed));

struct ns_rtt_info {
	uint16_t  nse_nxt_hdr; /* next header */
	uint16_t  nse_len; /* ... you know */
	uint32_t  sec;
	uint32_t  usec;
} __attribute__((packed));


