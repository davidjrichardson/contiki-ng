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

#include "lib/list.h"
#include "lib/heapmem.h"

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
static nbr_buf_item_t*
neighbour_cache_item(uip_ipaddr6_t *ipaddr)
{
    nbr_buf_item_t *item = list_head(neighbour_buf);

    while ((item = (nbr_buf_item_t *) list_item_next(item)) != NULL) {
        if (uip_ip6addr_comp((void *) &(item->ipaddr), (void *) ipaddr)) {
            return item;
        }
    }

    return NULL;
}
/*---------------------------------------------------------------------------*/
static void
tpwsn_tcpip_handler(void)
{
    if(uip_newdata()) {
        // TODO: Logging
        
        nd_pkt_t *pkt = ((nd_pkt_t *) uip_appdata)[0];
        uip_ipaddr6_t remote_ip = uip_conn->ripaddr;
        
        nbr_buf_item_t *sender = neighbour_cache_item(&remote_ip);
        
        // The sender is someone new
        if (sender == NULL) {
            sender = (nbr_buf_item_t *) heapmem_alloc(sizeof(nbr_buf_item_t));
            sender->sequence_no = pkt->sequence;
            sender->last_seen = (unsigned long) clock_time();
            // TODO: Copy the IP address accross
        }

        // TODO: Sequence number response here
    }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(tpwsn_neighbour_discovery_process, ev, data)
{
    PROCESS_BEGIN();

    etimer_set(&nd_timer, TPWSN_ND_PERIOD);

    while (1) {
        PROCESS_YIELD();

        if (ev == tcpip_event) {
            tpwsn_tcpip_handler();
        }

        if (etimer_expired(&nd_timer)) {
            tx_neighbourhood_ping();
            
            etimer_set(&nd_timer, TPWSN_ND_PERIOD);
        }
    }

    // TODO: Clean up memory that is allocated

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
void 
tx_neighbourhood_ping(void)
{
    nd_pkt_t new_ping = {
        .is_response = false,
        .sequence = global_sequence  
    };

    // Increment the global sequence number to ensure monotonicity
    global_sequence = global_sequence + 1;

    // TODO: Logging

    // TX the token to link-local nodes
    uip_ipaddr_copy(&nd_bcast_conn->ripaddr, &nd_ll_ipaddr);
    uip_udp_packet_send(nd_bcast_conn, &new_ping, sizeof(nd_pkt_t));

    // Return to accepting incoming packets from any IP
    uip_create_unspecified(&nd_bcast_conn->ripaddr);
}
/*---------------------------------------------------------------------------*/
void
tx_neighbourhood_ping_response(unsigned int new_sequence, uip_ipaddr6_t *sender)
{
    nd_pkt_t new_ping = {
        .is_response = true,
        .sequence = new_sequence 
    };

    // TODO: Logging
    
    // TX the token to the original sender
    uip_ipaddr_copy(&nd_bcast_conn->ripaddr, sender);
    uip_udp_packet_send(nd_bcast_conn, &new_ping, sizeof(nd_pkt_t));

    // Return to accepting incoming packets from any IP
    uip_create_unspecified(&nd_bcast_conn->ripaddr);
}
/*---------------------------------------------------------------------------*/
void
tpwsn_neighbour_discovery_init(void)
{
    // TODO: Initialise lists/data structures
    uip_create_linklocal_allnodes_mcast(&nd_ll_ipaddr);
    nd_bcast_conn = udp_new(NULL, UIP_HTONS(TPWSN_ND_PORT), NULL);
    udp_bind(nd_bcast_conn, UIP_HTONS(TPWSN_ND_PORT));
    // Start the ND process
    process_start(&tpwsn_neighbour_discovery_process, NULL);
}
