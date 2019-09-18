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

#include "tpwsn-periodic-rtx.h"

#include "neighbour-discovery.h"

#include "net/ipv6/uip.h"

#include "contiki.h"
#include "contiki-net.h"

#include "lib/list.h"
#include "lib/queue.h"
#include "lib/heapmem.h"

#include "dev/serial-line.h"
#include <stdio.h>
#include <string.h>

#define DEBUG DEBUG_FULL

#include "sys/log.h"

#define LOG_MODULE "TPWSN-PRTX-SINK"
#define LOG_LEVEL LOG_LEVEL_INFO

// The udp link-local connection for pings
static struct uip_udp_conn *prtx_bcast_conn;

// The link-local IP address for ND pings
static uip_ipaddr_t prtx_ll_ipaddr;

LIST(msg_buf);

/*---------------------------------------------------------------------------*/
static void
prtx_init() {
    // Init the connection
    uip_create_linklocal_allnodes_mcast(&prtx_ll_ipaddr);
    prtx_bcast_conn = udp_new(NULL, UIP_HTONS(TPWSN_PRTX_PORT), NULL);
    udp_bind(prtx_bcast_conn, UIP_HTONS(TPWSN_PRTX_PORT));
    prtx_bcast_conn->rport = UIP_HTONS(TPWSN_PRTX_PORT);
    prtx_bcast_conn->lport = UIP_HTONS(TPWSN_PRTX_PORT);
    list_init(msg_buf);
    serial_line_init();
}
/*---------------------------------------------------------------------------*/
static bool
neighbour_has_msg(const unsigned int msg_uid) {
    tpwsn_map_msg_t *buf_item = list_head(msg_buf);

    while(buf_item != NULL) {
        if (msg_uid == buf_item->msg_uid) {
            return true;
        }

        buf_item = (tpwsn_map_msg_t *) list_item_next(buf_item);
    }

    return false;
}
/*---------------------------------------------------------------------------*/
static void
tcpip_handler() {
    if (uip_newdata()) {
        tpwsn_pkt_t *pkt = ((tpwsn_pkt_t *) uip_appdata);

        if (!neighbour_has_msg(pkt->msg_uid)) {
            LOG_INFO("Sink recv'd message at %lu\n", (unsigned long) clock_time());

            tpwsn_map_msg_t *msg = (tpwsn_map_msg_t *) malloc(sizeof(tpwsn_map_msg_t));
            msg->msg_uid = pkt->msg_uid;
            msg->last_sent = clock_time();
            list_add(msg_buf, msg);
        }
    }
}
/*---------------------------------------------------------------------------*/
PROCESS(periodic_rtx_process, "Periodic Retransmission Protocol Process");
AUTOSTART_PROCESSES(&periodic_rtx_process);
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(periodic_rtx_process, ev, data)
{
    PROCESS_BEGIN();

    LOG_INFO("Starting Periodic RTX sink app\n");

    prtx_init();

    while (1) {
        PROCESS_YIELD();

        if (ev == tcpip_event) {
            tcpip_handler();
        }
    }

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/