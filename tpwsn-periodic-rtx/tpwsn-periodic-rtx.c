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

#define LOG_MODULE "TPWSN-PRTX"
#define LOG_LEVEL LOG_LEVEL_INFO

// The broadcast period timer
static struct etimer prtx_timer;

// The udp link-local connection for pings
static struct uip_udp_conn *prtx_bcast_conn;

// The link-local IP address for ND pings
static uip_ipaddr_t prtx_ll_ipaddr;

static const list_t *prtx_neighbours;

LIST(neighbour_msg_map);

QUEUE(msg_buffer);

/*---------------------------------------------------------------------------*/
static tpwsn_map_t *
get_item_for_addr(const uip_ipaddr_t *ipaddr) {
    tpwsn_map_t *item = list_head(neighbour_msg_map);

    while (item != NULL) {
        if (uip_ip6addr_cmp((void *) &(item->ipaddr), (void *) ipaddr)) {
            return item;
        }

        item = (tpwsn_map_t *) list_item_next(item);
    }

    return NULL;
}

/*---------------------------------------------------------------------------*/
static bool
neighbour_in_msg_map(const uip_ipaddr_t *neighbour) {
    tpwsn_map_t *item = list_head(neighbour_msg_map);

    while (item != NULL) {
        if (uip_ip6addr_cmp((void *) &(item->ipaddr), (void *) neighbour)) {
            return true;
        }

        item = (tpwsn_map_t *) list_item_next(item);
    }

    return false;
}


/*---------------------------------------------------------------------------*/
static bool
neighbour_has_msg(const tpwsn_pkt_t *msg, const uip_ipaddr_t *sender) {
//    tpwsn_map_t *map_item = (tpwsn_map_t *) list_head(neighbour_msg_map);
//
//    while (map_item != NULL) {
//        LOG_INFO("Map item for ");
//        LOG_INFO_6ADDR(&map_item->ipaddr);
//        LOG_INFO_("\n");
//
//        if (uip_ip6addr_cmp((void *) &(map_item->ipaddr), (void *) sender) && map_item->msg_ids_list != NULL) {
//            LOG_INFO("Iterating over messages f or ");
//            LOG_INFO_6ADDR(&map_item->ipaddr);
//            LOG_INFO_("\n");
//
//            tpwsn_map_msg_t *msg_item = (tpwsn_map_msg_t *) list_head(map_item->msg_ids_list);
//
//            while (msg_item != NULL) {
//                if (msg_item->msg_uid == msg->msg_uid) {
//                    return true;
//                }
//
//                msg_item = (tpwsn_map_msg_t *) list_item_next(msg_item);
//            }
//        }
//
//        map_item = (tpwsn_map_t *) list_item_next(map_item);
//    }

    return false;
}

/*---------------------------------------------------------------------------*/
static bool
should_bcast_message(const tpwsn_pkt_t *msg) {
    if (msg == NULL) {
        return false;
    }

    nbr_buf_item_t *neighbour = (nbr_buf_item_t *) list_head(*prtx_neighbours);
    bool bcast = false;

    while (neighbour != NULL) {
        if (!neighbour_has_msg(msg, &neighbour->ipaddr)) {
            bcast = true;
        }

        neighbour = list_item_next(neighbour);
    }

    return bcast;
}

/*---------------------------------------------------------------------------*/
static bool
is_msg_in_buf(const tpwsn_pkt_t *msg) {
    tpwsn_pkt_t *item = list_head(msg_buffer);

    while (item != NULL) {
        if (item->msg_uid == msg->msg_uid) {
            return true;
        }

        item = (tpwsn_pkt_t *) list_item_next(item);
    }

    return false;
}

/*---------------------------------------------------------------------------*/
static bool
prtx_neighbour_refresh() {
    LOG_INFO("Starting PRTX Refresh\n");

    nbr_buf_item_t *item = (nbr_buf_item_t *) list_head(*prtx_neighbours);
    bool success = false;

    while (item != NULL) {
        if (!neighbour_in_msg_map(&item->ipaddr)) {
            LOG_INFO("Neighbour ");
            LOG_INFO_6ADDR(&item->ipaddr);
            LOG_INFO_(" not in msg_map, adding them\n");

            tpwsn_map_t *map_item = (tpwsn_map_t *) malloc(sizeof(tpwsn_map_t));

            if (map_item == NULL) {
                LOG_ERR("Failed to allocate memory for the neighbour message map\n");

                return false;
            }

            list_init(*(map_item->msg_ids));
            uip_ip6addr_copy(&(map_item->ipaddr), &(item->ipaddr));
            list_add(neighbour_msg_map, map_item);
        }

        item = (nbr_buf_item_t *) list_item_next(item);
    }

    return success;
}

/*---------------------------------------------------------------------------*/
static void
broadcast_msg(tpwsn_pkt_t *msg) {
    // Update the sender of the packet so we can keep track of the neighbours
    // who have seen it
    uip_ipaddr_copy(&msg->sender, &uip_ds6_get_link_local(-1)->ipaddr);

    LOG_INFO("Sending message %u to link-local nodes\n", msg->msg_uid);

    // TX the token to link-local nodes
    uip_ipaddr_copy(&prtx_bcast_conn->ripaddr, &prtx_ll_ipaddr);
    uip_udp_packet_send(prtx_bcast_conn, msg, sizeof(tpwsn_pkt_t));

    // Return to accepting incoming packets from any IP
    uip_create_unspecified(&prtx_bcast_conn->ripaddr);
}

/*---------------------------------------------------------------------------*/
static void
recv_pkt_handler() {
    if (uip_newdata()) {
        tpwsn_pkt_t *pkt = ((tpwsn_pkt_t *) uip_appdata);
        uip_ipaddr_t remote_ip = pkt->sender;

        LOG_INFO("Recv'd packet at time %lu from", (unsigned long) clock_time());
        LOG_INFO_6ADDR(&remote_ip);
        LOG_INFO_("\n");

//        print_nbr_buf();

        if (!is_msg_in_buf(pkt)) {
            // Copy the packet to our own memory and add it to the list
            tpwsn_pkt_t *new_pkt = (tpwsn_pkt_t *) malloc(sizeof(tpwsn_pkt_t));

            if (new_pkt == NULL) {
                LOG_ERR("Could not allocate memory to copy packet to app space\n");

                return;
            }

            memcpy(new_pkt, pkt, sizeof(tpwsn_pkt_t));
            queue_enqueue(msg_buffer, new_pkt);

            if (etimer_expired(&prtx_timer)) {
                // Periodically re-broadcast the message if there's a neighbour that doesn't have it yet
                etimer_set(&prtx_timer, TPWSN_PRTX_PERIOD);
            }
        }

        // If a neighbour has sent the message but isn't marked as receiving it
        if (!neighbour_has_msg(pkt, &remote_ip)) {
            tpwsn_map_msg_t *msg_item = (tpwsn_map_msg_t *) malloc(sizeof(tpwsn_map_msg_t));

            if (msg_item == NULL) {
                LOG_ERR("Failed to allocate space for a message map list item\n");

                return;
            }

            msg_item->last_sent = (unsigned long) clock_time();
            msg_item->msg_uid = pkt->msg_uid;

            LOG_INFO("Getting map entry for IP ");
            LOG_INFO_6ADDR(&remote_ip);
            LOG_INFO_("\n");

            tpwsn_map_t *map_entry = get_item_for_addr(&remote_ip);

            if (map_entry == NULL) {
                LOG_ERR("Failed to locate neighbour entry for IP ");
                LOG_ERR_6ADDR(&remote_ip);
                LOG_ERR_("\n");

                return;
            }

            LOG_INFO("Got map entry\n");

            list_add(map_entry->msg_ids_list, msg_item);
        }
    }
}

/*---------------------------------------------------------------------------*/
static void
serial_handler(const char *data) {
//    // TODO: Implement source/sink selection using serial
//    if (strcmp(data,"foo") == 0) {
//        LOG_INFO("Recv'd serial line: %s\n", data);
//    }
}

/*---------------------------------------------------------------------------*/
static void
prtx_init() {
    // Init the connection
    uip_create_linklocal_allnodes_mcast(&prtx_ll_ipaddr);
    prtx_bcast_conn = udp_new(NULL, UIP_HTONS(TPWSN_PRTX_PORT), NULL);
    udp_bind(prtx_bcast_conn, UIP_HTONS(TPWSN_PRTX_PORT));
    prtx_bcast_conn->rport = UIP_HTONS(TPWSN_PRTX_PORT);
    prtx_bcast_conn->lport = UIP_HTONS(TPWSN_PRTX_PORT);
    // Init the lists etc
    list_init(neighbour_msg_map);
    queue_init(msg_buffer);
    // Init the pointer to the neighbour buffer
    prtx_neighbours = nd_neighbour_list();
    serial_line_init();
}
/*---------------------------------------------------------------------------*/
PROCESS(periodic_rtx_process, "Periodic Retransmission Protocol Process");
AUTOSTART_PROCESSES(&periodic_rtx_process);
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(periodic_rtx_process, ev, data) {
    PROCESS_BEGIN();

                LOG_INFO("Starting Periodic RTX broadcast app\n");

                prtx_init();

                while (1) {
                    // TODO: Handle the case when the node is the destination

                    PROCESS_YIELD();

                    // Refresh the neighbour map each invocation
                    prtx_neighbour_refresh();

                    if (ev == tcpip_event) {
                        recv_pkt_handler();
                    }

                    if (ev == serial_line_event_message && data != NULL) {
                        serial_handler((const char *) data);
                    }

                    if (etimer_expired(&prtx_timer)) {
                        if (should_bcast_message(queue_peek(msg_buffer))) {
                            broadcast_msg(queue_peek(msg_buffer));

                            // Periodically re-broadcast the message if there's a neighbour that doesn't have it yet
                            etimer_set(&prtx_timer, TPWSN_PRTX_PERIOD + ((random_rand() % 5) * CLOCK_SECOND));
                        } else {
                            // Remove the packet from the queue and free the memory
                            tpwsn_pkt_t *old_pkt = (tpwsn_pkt_t *) queue_dequeue(msg_buffer);
                            free(old_pkt);
                        }
                    }
                }

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/