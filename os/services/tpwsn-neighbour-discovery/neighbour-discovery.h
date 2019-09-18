/*
 * Copyright (c) 2018, David Richardson
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \addtogroup tpwsn
 * @{
 */

/**
 * \file
 *      A process that periodically check the 1-hop neighbourhood
 *      of its node, maintaining a view of its currently
 *      connected neighbourhood.
 * \author David Richardson <d.j.richardson@warwick.ac.uk>
 */

#ifndef TPWSN_NEIGHBOUR_DISCOVERY_H_
#define TPWSN_NEIGHBOUR_DISCOVERY_H_

#include "net/ipv6/uip.h"

#include <stdbool.h>
#include <net/ipv6/uip-ds6.h>

/** \brief The neighbour discovery ping period */
#ifdef TPWSN_ND_CONF_PERIOD
#define TPWSN_ND_PERIOD (CLOCK_SECOND * TPWSN_ND_CONF_PERIOD)
#else /* TPWSN_ND_CONF_PERIOD */
#define TPWSN_ND_PERIOD (CLOCK_SECOND * 3)
#endif /*TPWSN_ND_CONF_PERIOD */

/** \brief The port used for neighbour discovery pings */
#ifdef TPWSN_ND_CONF_PORT
#define TPWSN_ND_PORT TPWSN_ND_CONF_PORT
#else /* TPWSN_ND_CONF_PORT */
#define TPWSN_ND_PORT 30002
#endif /* TPWSN_ND_CONF_PORT */

/** \brief A function for getting the maximum of two numbers */
#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
_a > _b ? _a : _b; })

/** \brief The neighbour discovery ping packet structure */
typedef struct nd_pkt_s {
    uip_ipaddr_t ipaddr;  /* The global ip addr of the sending node */
    int sequence;  /* The sequence number for the ping */
    bool is_response;       /* Flag to indicate ping or response */
} nd_pkt_t;

typedef struct nd_resp_queue_s {
    struct nd_resp_queue_s *next;   /* The next item in the cache */
    uip_ipaddr_t ipaddr;            /* The IP address of the node */
} nd_resp_queue_t;

/** \brief An item in the neighbour cache for this node */
typedef struct nbr_buf_item_s {
    struct nbr_buf_item_s *next;/* The next item in the cache */
    unsigned long last_seen;    /* The clock time a node was seen last */
    int sequence_no;   /* The last-seen sequence number */
    uip_ipaddr_t ipaddr;       /* The IP address of the node */
} nbr_buf_item_t;

/**
 * Initialise the neighbour discovery module for the applications
 * in transiently powered wireless sensor networks.
 */
void tpwsn_neighbour_discovery_init(void);

void tx_neighbourhood_ping(void);
void tx_neighbourhood_ping_response(const uip_ipaddr_t*);
void print_nbr_buf(void);

/**
 * Access the ND buffer
 */
const list_t* nd_neighbour_list(void);

#endif

/** @} */
