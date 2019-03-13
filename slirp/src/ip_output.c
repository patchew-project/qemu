/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
 * The Regents of the University of California.  All rights reserved.
 *
 * Changes and additions relating to SLiRP are
 * Copyright (c) 1995 Danny Gasparovski.
 */

#include "slirp.h"

/* Number of packets queued before we start sending
 * (to prevent allocing too many mbufs) */
#define IF_THRESH 10

/*
 * IP output.  The packet in mbuf chain m contains a skeletal IP
 * header (with len, off, ttl, proto, tos, src, dst).
 * The mbuf chain containing the packet will be freed.
 * The mbuf opt, if present, will not be freed.
 */
int
ip_output(struct socket *so, struct mbuf *m0)
{
	Slirp *slirp = m0->slirp;
	register struct ip *ip;
	register struct mbuf *m = m0;
	register int hlen = sizeof(struct ip );
	int len, off, error = 0;

	DEBUG_CALL("ip_output");
	DEBUG_ARG("so = %p", so);
	DEBUG_ARG("m0 = %p", m0);

	ip = mtod(m, struct ip *);
	/*
	 * Fill in IP header.
	 */
	ip->ip_v = IPVERSION;
	ip->ip_off &= IP_DF;
	ip->ip_id = htons(slirp->ip_id++);
	ip->ip_hl = hlen >> 2;

	/*
	 * If small enough for interface, can just send directly.
	 */
	if ((uint16_t)ip->ip_len <= IF_MTU) {
		ip->ip_len = htons((uint16_t)ip->ip_len);
		ip->ip_off = htons((uint16_t)ip->ip_off);
		ip->ip_sum = 0;
		ip->ip_sum = cksum(m, hlen);

		if_output(so, m);
		goto done;
	}

	/*
	 * Too large for interface; fragment if possible.
	 * Must be able to put at least 8 bytes per fragment.
	 */
	if (ip->ip_off & IP_DF) {
		error = -1;
		goto bad;
	}

	len = (IF_MTU - hlen) &~ 7;       /* ip databytes per packet */
	if (len < 8) {
		error = -1;
		goto bad;
	}

    {
	int mhlen, firstlen = len;
	struct mbuf **mnext = &m->m_nextpkt;

	/*
	 * Loop through length of segment after first fragment,
	 * make new header and copy data of each part and link onto chain.
	 */
	m0 = m;
	mhlen = sizeof (struct ip);
	for (off = hlen + len; off < (uint16_t)ip->ip_len; off += len) {
	  register struct ip *mhip;
	  m = m_get(slirp);
          if (m == NULL) {
	    error = -1;
	    goto sendorfree;
	  }
	  m->m_data += IF_MAXLINKHDR;
	  mhip = mtod(m, struct ip *);
	  *mhip = *ip;

	  m->m_len = mhlen;
	  mhip->ip_off = ((off - hlen) >> 3) + (ip->ip_off & ~IP_MF);
	  if (ip->ip_off & IP_MF)
	    mhip->ip_off |= IP_MF;
	  if (off + len >= (uint16_t)ip->ip_len)
	    len = (uint16_t)ip->ip_len - off;
	  else
	    mhip->ip_off |= IP_MF;
	  mhip->ip_len = htons((uint16_t)(len + mhlen));

	  if (m_copy(m, m0, off, len) < 0) {
	    error = -1;
	    goto sendorfree;
	  }

	  mhip->ip_off = htons((uint16_t)mhip->ip_off);
	  mhip->ip_sum = 0;
	  mhip->ip_sum = cksum(m, mhlen);
	  *mnext = m;
	  mnext = &m->m_nextpkt;
	}
	/*
	 * Update first fragment by trimming what's been copied out
	 * and updating header, then send each fragment (in order).
	 */
	m = m0;
	m_adj(m, hlen + firstlen - (uint16_t)ip->ip_len);
	ip->ip_len = htons((uint16_t)m->m_len);
	ip->ip_off = htons((uint16_t)(ip->ip_off | IP_MF));
	ip->ip_sum = 0;
	ip->ip_sum = cksum(m, hlen);
sendorfree:
	for (m = m0; m; m = m0) {
		m0 = m->m_nextpkt;
                m->m_nextpkt = NULL;
		if (error == 0)
			if_output(so, m);
		else
			m_free(m);
	}
    }

done:
	return (error);

bad:
	m_free(m0);
	goto done;
}
