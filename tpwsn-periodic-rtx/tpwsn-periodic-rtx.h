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

#ifndef CONTIKI_NG_TPWSN_PERIODIC_RTX_H
#define CONTIKI_NG_TPWSN_PERIODIC_RTX_H

#include <net/ipv6/uip.h>
#include <lib/list.h>

/** \brief The broadcast period */
#ifdef TPWSN_PRTX_CONF_PERIOD
#define TPWSN_PRTX_PERIOD (CLOCK_SECOND * TPWSN_PRTX_CONF_PERIOD)
#else /* TPWSN_PRTX_CONF_PERIOD */
#define TPWSN_PRTX_PERIOD (CLOCK_SECOND * 7)
#endif /*TPWSN_PRTX_CONF_PERIOD */

/** \brief The port used for PRTX broadcasts */
#ifdef TPWSN_PRTX_CONF_PORT
#define TPWSN_PRTX_PORT TPWSN_PRTX_CONF_PORT
#else /* TPWSN_PRTX_CONF_PORT */
#define TPWSN_PRTX_PORT 30003
#endif /* TPWSN_PRTX_CONF_PORT */

/** \brief A wrapping packet structure to handle TPWSN routing */
typedef struct tpwsn_pkt_s {
    struct tpwsn_pkt_s *next;
    uip_ipaddr_t dest;
    unsigned int msg_uid;
    unsigned char* data;
} tpwsn_pkt_t;

/** \brief A sucicnt representation of a message sent to a neighbour (its UID) */
typedef struct tpwsn_map_msg_s {
    struct tpwsn_map_msg_s *next;
    unsigned int msg_uid;
    unsigned long last_sent;
} tpwsn_map_msg_t;

/** \brief A map type to store msg->ip mapping */
typedef struct tpwsn_map_s {
    struct tpwsn_map_s *next;
    uip_ipaddr_t ipaddr;
    LIST_STRUCT(msg_ids);
} tpwsn_map_t;

/**
 * Initialise the periodic rtx broadcast application
 */
static void prtx_init(void);

void prtx_broadcast(const tpwsn_pkt_t *pkt);

#endif //CONTIKI_NG_TPWSN_PERIODIC_RTX_H
