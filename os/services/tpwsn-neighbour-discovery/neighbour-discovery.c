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
#include "lib/queue.h"
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

// The neighbourhood buffer
LIST(neighbour_buf);
QUEUE(nd_response_queue);

/*---------------------------------------------------------------------------*/
const list_t *
nd_neighbour_list(void) {
    return (const list_t *) &neighbour_buf;
}
/*---------------------------------------------------------------------------*/
void
print_nbr_buf(void) {
    nbr_buf_item_t *item = list_head(neighbour_buf);

    LOG_INFO("NBR cache: {");

    while (item != NULL) {
        LOG_INFO_("(");
        LOG_INFO_6ADDR(&item->ipaddr);
        LOG_INFO_(", %d), ", item->sequence_no);

        item = list_item_next(item);
    }

    LOG_INFO_("}\n");
}
/*---------------------------------------------------------------------------*/
static nbr_buf_item_t *
neighbour_cache_item(const uip_ipaddr_t *ipaddr) {
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

        // TODO: Figure out why there's an issue with memory corruption - could be to
        // do with an offset from the uip_appdata pointer?

        LOG_INFO("Recv'd ND packet from ");
        LOG_INFO_6ADDR(&remote_ip);
        LOG_INFO_(" at time %lu with sequence %d\n", (unsigned long) clock_time(), pkt->sequence);

        nbr_buf_item_t *sender = neighbour_cache_item(&remote_ip);

        // The sender is someone new
        if (sender == NULL) {
            LOG_INFO("ND packet is from new neighbour\n");

            sender = (nbr_buf_item_t *) malloc(sizeof(nbr_buf_item_t));

            if (sender == NULL) {
                LOG_ERR("Could not allocate space for new neighbour\n");

                return;
            }

            sender->sequence_no = pkt->sequence;
            sender->last_seen = clock_seconds();
            uip_ipaddr_copy(&sender->ipaddr, &remote_ip);
            list_add(neighbour_buf, sender);

            LOG_INFO("Added neighbour ");
            LOG_INFO_6ADDR(&sender->ipaddr);
            LOG_INFO_(" to NBC with sequence %d\n", sender->sequence_no);

            if (!pkt->is_response) {
                nd_resp_queue_t *queue_item = (nd_resp_queue_t *) malloc(sizeof(nd_resp_queue_t));

                if (queue_item == NULL) {
                    LOG_ERR("Could not allocate space for response queue item\n");

                    return;
                }

                LOG_INFO("Queuing response for ");
                LOG_INFO_6ADDR(&sender->ipaddr);
                LOG_INFO_("\n");

                uip_ipaddr_copy(&queue_item->ipaddr, &remote_ip);
                queue_enqueue(nd_response_queue, queue_item);
            }
        } else {
            // Update our sequence number to match theirs
            sender->sequence_no = max(sender->sequence_no, pkt->sequence);

            if (!pkt->is_response) {
                nd_resp_queue_t *queue_item = (nd_resp_queue_t *) malloc(sizeof(nd_resp_queue_t));

                if (queue_item == NULL) {
                    LOG_ERR("Could not allocate space for response queue item\n");

                    return;
                }

                LOG_INFO("Queuing response for ");
                LOG_INFO_6ADDR(&sender->ipaddr);
                LOG_INFO_("\n");

                uip_ipaddr_copy(&queue_item->ipaddr, &remote_ip);
                queue_enqueue(nd_response_queue, queue_item);
            }
        }

    }
}
/*---------------------------------------------------------------------------*/
void
tpwsn_init(void) {
    // TODO: Initialise lists/data structures
    uip_create_linklocal_allnodes_mcast(&nd_ll_ipaddr);
    nd_bcast_conn = udp_new(NULL, UIP_HTONS(TPWSN_ND_PORT), NULL);
    udp_bind(nd_bcast_conn, UIP_HTONS(TPWSN_ND_PORT));
    nd_bcast_conn->rport = UIP_HTONS(TPWSN_ND_PORT);
    nd_bcast_conn->lport = UIP_HTONS(TPWSN_ND_PORT);

    list_init(neighbour_buf);
    queue_init(nd_response_queue);
}
/*---------------------------------------------------------------------------*/
void
process_nd_response_queue(void) {
    while (!queue_is_empty(nd_response_queue)) {
        nd_resp_queue_t *item = (nd_resp_queue_t *) queue_dequeue(nd_response_queue);

        tx_neighbourhood_ping_response(&item->ipaddr);
    }
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

        // TODO: Add in method to process the responses for the ND ping(s)

        if (ev == tcpip_event) {
            LOG_INFO("TCPIP Event, invoking relevant ND handler\n");

            tpwsn_tcpip_handler();
        }

        if (etimer_expired(&nd_timer)) {
            LOG_INFO("Sending next ND ping\n");

            tx_neighbourhood_ping();
        }

        process_nd_response_queue();

        LOG_INFO("Scheduling next ND period\n");

        etimer_set(&nd_timer, TPWSN_ND_PERIOD + ((random_rand() % 10) * CLOCK_SECOND));

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
    nd_pkt_t *new_ping = (nd_pkt_t *) malloc(sizeof(nd_pkt_t));

    if (new_ping == NULL) {
        LOG_ERR("Failed to allocate LL-bcast ping packet\n");
        return;
    }

    new_ping->is_response = false;
    new_ping->sequence = -1;
    uip_ipaddr_copy(&new_ping->ipaddr, &uip_ds6_get_link_local(-1)->ipaddr);

    LOG_INFO("IPADDR: ");
    LOG_INFO_6ADDR(&new_ping->ipaddr);
    LOG_INFO_("\n");

    LOG_INFO("Sending ND ping to link-local neighbours at %lu with seq=%d\n",
            (unsigned long) clock_time(), new_ping->sequence);

    // TX the token to link-local nodes
    uip_ipaddr_copy(&nd_bcast_conn->ripaddr, &nd_ll_ipaddr);
    uip_udp_packet_send(nd_bcast_conn, &new_ping, sizeof(nd_pkt_t));

    // Return to accepting incoming packets from any IP
    uip_create_unspecified(&nd_bcast_conn->ripaddr);

    // Free the packet since it's been BCast now and doesn't need RTX
    free(new_ping);
}

/*---------------------------------------------------------------------------*/
void
tx_neighbourhood_ping_response(const uip_ipaddr_t *sender) {
    LOG_INFO("Sending ping response to ");
    LOG_INFO_6ADDR(sender);
    LOG_INFO_(" at %lu\n", (unsigned long) clock_time());

    nbr_buf_item_t *item = neighbour_cache_item(sender);

    if (item == NULL) {
        LOG_ERR("Failed to find neighbour cache item for ");
        LOG_ERR_6ADDR(sender);
        LOG_ERR_(" when TXing ND response\n");

        return;
    }

    // Increment the sequence number by 2 because we've recv'd a msg already and are about to send a new one
    item->sequence_no = max(2, item->sequence_no + 2);

    LOG_INFO("Response sequence number = %d for ip ", item->sequence_no);
    LOG_INFO_6ADDR(sender);
    LOG_INFO_("\n");

    nd_pkt_t new_ping = {
            .is_response = true,
            .sequence = item->sequence_no
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
