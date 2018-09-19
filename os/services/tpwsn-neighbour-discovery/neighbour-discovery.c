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

#include "contiki.h"
#include "contiki-net.h"
#include "neighbour-discovery.h"

#include "net/ipv6/uip.h"

#include "lib/list.h"
#include "lib/heapmem.h"

#define DEBUG DEBUG_FULL

/* Logging configuration */
#include "sys/log.h"

#define LOG_MODULE "TPWSN-ND"
#define LOG_LEVEL LOG_LEVEL_INFO

// The neighbour discovery timer
static struct etimer nd_timer;

// The udp link-local connection for pings
static struct uip_udp_conn *nd_bcast_conn;

// The link-local IP address for ND pings
static uip_ipaddr_t nd_ll_ipaddr;

// The node-local global sequence number for ND
static unsigned int global_sequence;

// The neighbourhood buffer
LIST(neighbour_buf);

/*---------------------------------------------------------------------------*/
static nbr_buf_item_t *
neighbour_cache_item(uip_ipaddr_t *ipaddr) {
    nbr_buf_item_t *item = list_head(neighbour_buf);

    while (item != NULL) {
        if (uip_ip6addr_cmp((void *) &(item->ipaddr), (void *) ipaddr)) {
            return item;
        }

        item = (nbr_buf_item_t *) list_item_next(item);
    }

    return NULL;
}

/*---------------------------------------------------------------------------*/
static void
tpwsn_tcpip_handler(void) {
    LOG_INFO("Invoked TCPIP handler\n");

    if (uip_newdata()) {
        nd_pkt_t *pkt = ((nd_pkt_t *) uip_appdata);
        uip_ipaddr_t remote_ip = pkt->ipaddr;

        LOG_INFO("Recv'd ND packet from ");
        LOG_INFO_6ADDR(&remote_ip);
        LOG_INFO_("at time %lu\n", (unsigned long) clock_time());

        nbr_buf_item_t *sender = neighbour_cache_item(&remote_ip);

        // The sender is someone new
        if (sender == NULL) {
            LOG_INFO("ND packet is from new neighbour\n");

            sender = (nbr_buf_item_t *) heapmem_alloc(sizeof(nbr_buf_item_t));
            sender->sequence_no = pkt->sequence;
            sender->last_seen = clock_seconds();
            uip_ipaddr_copy(&sender->ipaddr, &remote_ip);
            list_add(neighbour_buf, sender);

            LOG_INFO("pkt->is_response: %d\n", pkt->is_response);

            if (!pkt->is_response) {
                tx_neighbourhood_ping_response(pkt->sequence, &remote_ip);
            }
        } else {
            if (pkt->is_response) {
                global_sequence = max(global_sequence, pkt->sequence);
            } else {
                bool lost_sync = (sender->sequence_no != pkt->sequence);
                sender->sequence_no = max(sender->sequence_no, pkt->sequence);

                if (lost_sync) {
                    LOG_INFO("Neighbour ");
                    LOG_INFO_6ADDR(&remote_ip);
                    LOG_INFO_(" is out of sync, sending response with seq=%u\n", pkt->sequence);
                    tx_neighbourhood_ping_response(pkt->sequence, &remote_ip);
                }
            }
        }

    }
}
/*---------------------------------------------------------------------------*/
void
tpwsn_init(void) {
    // TODO: Initialise lists/data structures
    global_sequence = 0;
    uip_create_linklocal_allnodes_mcast(&nd_ll_ipaddr);
    nd_bcast_conn = udp_new(NULL, UIP_HTONS(TPWSN_ND_PORT), NULL);
    udp_bind(nd_bcast_conn, UIP_HTONS(TPWSN_ND_PORT));
    nd_bcast_conn->rport = UIP_HTONS(TPWSN_ND_PORT);
    nd_bcast_conn->lport = UIP_HTONS(TPWSN_ND_PORT);
}
/*---------------------------------------------------------------------------*/
PROCESS(tpwsn_neighbour_discovery_process, "TPWSN Neighbour Discovery");
PROCESS_THREAD(tpwsn_neighbour_discovery_process, ev, data) {
    PROCESS_BEGIN();

    tpwsn_init();

    etimer_set(&nd_timer, TPWSN_ND_PERIOD);

    LOG_INFO("Starting ND process\n");

    while (1) {
        PROCESS_YIELD();

        LOG_INFO("Passed YIELD, ev=%u\n", ev);

        if (ev == tcpip_event) {
            LOG_INFO("TCPIP Event, invoking relevant ND handler\n");

            tpwsn_tcpip_handler();
        }

        if (etimer_expired(&nd_timer)) {
            tx_neighbourhood_ping();

            etimer_set(&nd_timer, TPWSN_ND_PERIOD);
        }

        // TODO: Add a break-out condition
    }

    // TODO: Clean up memory that is allocated

    nbr_buf_item_t *item = (nbr_buf_item_t *) list_head(neighbour_buf);

    while (item != NULL) {
        void *tmp = (void *) item;
        item = (nbr_buf_item_t *) list_item_next(item);
        heapmem_free(tmp);
    }

    PROCESS_END();
}

/*---------------------------------------------------------------------------*/
void
tx_neighbourhood_ping(void) {
    // Increment the global sequence number to ensure monotonicity
    global_sequence = global_sequence + 1;

    nd_pkt_t new_ping = {
            .is_response = false,
            .sequence = global_sequence
    };
    uip_ipaddr_copy(&new_ping.ipaddr, &uip_ds6_get_link_local(-1)->ipaddr);

    LOG_INFO("Sending ND ping to link-local neighbours at %lu\n",
            (unsigned long) clock_time());

    // TX the token to link-local nodes
    uip_ipaddr_copy(&nd_bcast_conn->ripaddr, &nd_ll_ipaddr);
    uip_udp_packet_send(nd_bcast_conn, &new_ping, sizeof(nd_pkt_t));

    // Return to accepting incoming packets from any IP
    uip_create_unspecified(&nd_bcast_conn->ripaddr);
}

/*---------------------------------------------------------------------------*/
void
tx_neighbourhood_ping_response(unsigned int new_sequence, const uip_ipaddr_t *sender) {
    LOG_INFO("Sending ping response to ");
    LOG_INFO_6ADDR(sender);
    LOG_INFO_("\n");

    nd_pkt_t new_ping = {
            .is_response = false,
            .sequence = global_sequence
    };
    uip_ipaddr_copy(&new_ping.ipaddr, &uip_ds6_get_link_local(-1)->ipaddr);

    uip_ipaddr_copy(&nd_bcast_conn->ripaddr, sender);
    uip_udp_packet_send(nd_bcast_conn, &new_ping, sizeof(nd_pkt_t));

    // Return to accepting incoming packets from any IP
    uip_create_unspecified(&nd_bcast_conn->ripaddr);
}
/*---------------------------------------------------------------------------*/
void
tpwsn_neighbour_discovery_init(void) {
    // Start the ND process
    process_start(&tpwsn_neighbour_discovery_process, NULL);
}
