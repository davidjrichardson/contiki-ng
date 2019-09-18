/*
 * Copyright (c) 2019, David Richardson - <david@tankski.co.uk>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

/* Re-implementation of the RMH-B protocol using the uip network stack */
#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"

#include "dev/serial-line.h"
#include "dev/leds.h"

#include "lib/random.h"
#include "lib/list.h"
#include "lib/memb.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define DEBUG DEBUG_PRINT

#include "sys/log.h"

#define LOG_MODULE "TPWSN-RMHB"
#define LOG_LEVEL LOG_LEVEL_INFO

#include "net/ipv6/uip-debug.h"

/* Networking */
#define RMHB_PROTO_PORT 30001
static struct uip_udp_conn *rmhb_conn;
static uip_ipaddr_t ipaddr;     /* destination: link-local all-nodes multicast */
static bool is_sink = false;
static bool reset_scheduled = false;

/* RMH-B Params */
#define MSG_TYPE_CTRL 1
#define MSG_TYPE_RECOVER 2
#define MSG_TYPE_BEACON 3
#define MSG_TYPE_DATA 4
#define MSG_TYPE_ANNOUNCE 5

#define UDP_HDR ((struct uip_udpip_hdr *)&uip_buf[UIP_LLH_LEN])

static uint8_t beacon_period = 10; /* multiplied by CLOCK_SECOND later */
static uint8_t announce_period = 15; /* multiplied by CLOCK_SECOND later */

struct __attribute__ ((__packed__)) tpwsn_msg_s {
  short msg_type;
};
typedef struct tpwsn_msg_s tpwsn_msg_t;

struct __attribute__ ((__packed__)) tpwsn_data_s {
    short msg_type;
    short version;
    int token;
    int hops;
};
typedef struct tpwsn_data_s tpwsn_data_t;

struct __attribute__ ((__packed__)) tpwsn_beacon_s {
    short msg_type;
    short version;
};
typedef struct tpwsn_beacon_s tpwsn_beacon_t;

struct __attribute__ ((__packed__)) tpwsn_ctrl_s {
  short msg_type;
  short version;
};
typedef struct tpwsn_ctrl_s tpwsn_ctrl_t;

struct tpwsn_nbr_table_s {
  struct tpwsn_nbr_table_s *next;
  uip_ipaddr_t addr;
  struct ctimer ctimer;
};
typedef struct tpwsn_nbr_table_s tpwsn_nbr_table_t;

// Function defs
static void recv_announcement(const uip_ipaddr_t *);
static void recv_data_msg(const uip_ipaddr_t *);
static void recv_beacon(const uip_ipaddr_t *);
static void recv_ctrl_msg(const uip_ipaddr_t *);
static void recv_recov_msg(const uip_ipaddr_t *);

static void remove_neighbor(void *);
static tpwsn_nbr_table_t* pick_neighbour();
static void send_unicast(const uip_ipaddr_t *, const void *, size_t);
static void send_multicast(const void *, size_t);

/* Neighbour table params */
#define NEIGHBOR_TIMEOUT 30 * CLOCK_SECOND
#define MAX_NEIGHBORS 16
LIST(neighbor_table);
MEMB(neighbor_mem, struct tpwsn_nbr_table_s, MAX_NEIGHBORS);

static int token;
static short token_version;
static struct etimer beacon_timer; /* Used for the version beaconing */
static struct etimer announce_timer; /* Used for the version beaconing */
static struct etimer rt; /* Used to 'restart' the node  */
/*---------------------------------------------------------------------------*/
PROCESS(rmhb_protocol_process, "RMH-B Protocol process");
AUTOSTART_PROCESSES(&rmhb_protocol_process);
/*---------------------------------------------------------------------------*/
/*
 * This function is called by the ctimer present in each neighbor
 * table entry. The function removes the neighbor from the table
 * because it has become too old.
 */
static void
remove_neighbor(void *n)
{
    struct tpwsn_nbr_table_s *e = n;

    LOG_INFO("Removing ");
    log_6addr(&e->addr);
    LOG_INFO_(" from the neighbour cache\n");

    list_remove(neighbor_table, e);
    memb_free(&neighbor_mem, e);
}
/*---------------------------------------------------------------------------*/
static tpwsn_nbr_table_t *
pick_neighbour() {
    int neighbour_count = list_length(neighbor_table);
    if (neighbour_count > 0) {
        int neighbour_index = random_rand() % neighbour_count;

        // Get the neighbour at the calculated index
        int i = 0;
        struct tpwsn_nbr_table_s *neighbour = list_head(neighbor_table);
        while (neighbour != NULL) {
            if (i == neighbour_index) {
                break;
            }

            i++;
            neighbour = (struct tpwsn_nbr_table_s *) list_item_next(neighbour);
        }

        return neighbour;
    }

    return NULL;
}
/*---------------------------------------------------------------------------*/
static void
recv_announcement(const uip_ipaddr_t *from) {
    LOG_INFO("Remote IP: ");
    log_6addr(from);
    LOG_INFO_("\n");

    struct tpwsn_nbr_table_s *e;

    for(e = list_head(neighbor_table); e != NULL; e = e->next) {
        if(uip_ip6addr_cmp(from, &e->addr)) {
            /* Our neighbor was found, so we update the timeout. */
            ctimer_set(&e->ctimer, NEIGHBOR_TIMEOUT, remove_neighbor, e);
            LOG_INFO("IP is in neighbour table, refreshing callback timer\n");
            return;
        }
    }

    e = memb_alloc(&neighbor_mem);
    if(e != NULL) {
        LOG_INFO("Adding to neighbour table\n");
        uip_ipaddr_copy(&e->addr, from);
        list_add(neighbor_table, e);
        ctimer_set(&e->ctimer, NEIGHBOR_TIMEOUT, remove_neighbor, e);
    }
}
/*---------------------------------------------------------------------------*/
static void
recv_ctrl_msg(const uip_ipaddr_t *from) {   
    LOG_INFO("Recv'd beacon ctrl from ");
    log_6addr(from);
    LOG_INFO_(" at time %lu\n", (unsigned long) clock_time());
    LOG_INFO("Sending recovery data back\n");

    // Send recovery data back to the sender
    tpwsn_data_t recov_msg = {
        .msg_type = MSG_TYPE_RECOVER,
        .version = token_version,
        .token = token,
        .hops = -1,
    };
    send_unicast(from, &recov_msg, sizeof(tpwsn_data_t));
}
/*---------------------------------------------------------------------------*/
static void
recv_recov_msg(const uip_ipaddr_t *from) {
    tpwsn_data_t *msg = (tpwsn_data_t *) uip_appdata;

    LOG_INFO("Recv'd data recovery (d: %d, v: %d) from ", msg->token, msg->version);
    log_6addr(from);
    LOG_INFO_(" at time %lu\n", (unsigned long) clock_time());

    token = msg->token;
    token_version = msg->version;
}
/*---------------------------------------------------------------------------*/
static void
recv_beacon(const uip_ipaddr_t *from) {
    tpwsn_beacon_t *msg = (tpwsn_beacon_t *) uip_appdata;

    LOG_INFO("Recv'd beacon (v: %d) from ", msg->version);
    log_6addr(from);
    LOG_INFO_(" at time %lu\n", (unsigned long) clock_time());

    if (token_version == msg->version) {
        LOG_INFO("Both tokens are identical\n");
    } else if (token_version > msg->version) {
        LOG_INFO("We are newer, updating ");
        log_6addr(from);
        LOG_INFO_("\n");

        tpwsn_data_t msg = {
            .msg_type = MSG_TYPE_RECOVER,
            .version = token_version,
            .token = token,
            .hops = -1,
        };
        send_unicast(from, &msg, sizeof(tpwsn_data_t));
    } else { /* If we are behind */
        LOG_INFO("They are newer, sending control sequence to ");
        log_6addr(from);
        LOG_INFO_("\n");

        tpwsn_ctrl_t msg = {
            .msg_type = MSG_TYPE_CTRL,
            .version = token_version,
        };
        send_unicast(from, &msg, sizeof(tpwsn_ctrl_t));
    }
}
/*---------------------------------------------------------------------------*/
static void 
recv_data_msg(const uip_ipaddr_t *from) {
    tpwsn_data_t *msg = (tpwsn_data_t *) uip_appdata;

    // Update this node
    token = msg->token;
    token_version = msg->version;

    // Forward the message to a random neighbour if we aren't the sink
    if (!is_sink) {
        LOG_INFO("Recv'd msg from ");
        log_6addr(from);
        LOG_INFO_(" at time %lu with hops %d\n", (unsigned long) clock_time(), msg->hops);
        
        tpwsn_nbr_table_t *neighbour = pick_neighbour();

        if (neighbour != NULL) {
            LOG_INFO("Forwarding packet (val: %d, hops: %d) to: ", token, (msg->hops + 1));
            log_6addr(&neighbour->addr);
            LOG_INFO_("\n at time %lu\n", (unsigned long) clock_time());

            msg->hops = msg->hops + 1;

            send_unicast(&neighbour->addr, msg, sizeof(tpwsn_data_t));
        }
    } else {
        LOG_INFO("Sink recv'd msg from ");
        log_6addr(from);
        LOG_INFO_(" at time %lu with hops %d\n", (unsigned long) clock_time(), msg->hops);
    }
}
/*---------------------------------------------------------------------------*/
static void
tcpip_handler(void) {
    if (uip_newdata()) {
        tpwsn_msg_t *msg = (tpwsn_msg_t *) uip_appdata;
        uip_ipaddr_t src;
        uip_ipaddr_copy(&src, &UDP_HDR->srcipaddr);

        switch (msg->msg_type) {
            case MSG_TYPE_ANNOUNCE:     recv_announcement(&src); break;
            case MSG_TYPE_RECOVER:      recv_recov_msg(&src); break;
            case MSG_TYPE_BEACON:       recv_beacon(&src); break;
            case MSG_TYPE_DATA:         recv_data_msg(&src); break;
            case MSG_TYPE_CTRL:         recv_ctrl_msg(&src); break;
        }
    }
    return;
}
/*---------------------------------------------------------------------------*/
static void
serial_handler(char *data) {
    char *ptr = strtok(data, " ");
    char *endptr;
    long delay = 0;
    bool seen_sleep = false;
    bool seen_set = false;
    bool seen_print = false;
    bool seen_start = false;

    // Iterate over the tokenised string
    while (ptr != NULL) {
        // Parse serial input to output the current token of the node
        if (strcmp(ptr, "print") == 0) {
            LOG_INFO("Seen print\n");
            seen_print = true;
        }
        if (seen_print) {
            LOG_INFO("Current token: %d\n", token);
            NETSTACK_RADIO.off();
        }

        // Parse serial input for restarting a node
        if (strcmp(ptr, "sleep") == 0) {
            seen_sleep = true;
        }
        if (seen_sleep) {
            delay = strtol(ptr, &endptr, 10);
        }

        // Parse serial input to set a node as a sink or source
        if (strcmp(ptr, "set") == 0) {
            seen_set = true;
        }
        if (seen_set) {
            if (strcmp(ptr, "sink") == 0) {
                LOG_INFO("Setting node status to SINK\n");
                is_sink = true;
            }
        }

        // Parse serial input to start the RMH sending process
        if (strcmp(ptr, "start") == 0) {
            LOG_INFO("Seen start\n");
            seen_start = true;
        }
        if (seen_start) {
            token++;
            token_version++;

            // Send the message to the neighbour if there is one
            tpwsn_nbr_table_t *neighbour = pick_neighbour();
            if (neighbour != NULL) {
                tpwsn_data_t msg = { 
                    .msg_type = MSG_TYPE_DATA, 
                    .token = token, 
                    .version = token_version,
                    .hops = 0,
                };

                LOG_INFO("Starting RMH at time %lu, sending token %d to: ", 
                        (unsigned long) clock_time(), token);
                log_6addr(&neighbour->addr);
                LOG_INFO_("\n");
                send_unicast(&neighbour->addr, &msg, sizeof(tpwsn_data_t));
            }
        }

        ptr = strtok(NULL, " ");
    }

    if (seen_sleep && delay > 0) {
        LOG_INFO("Restarting with delay of %ld seconds\n", delay);

        NETSTACK_RADIO.off();
        etimer_set(&rt, (delay * CLOCK_SECOND));
        reset_scheduled = true;
        leds_on(LEDS_ALL);
    }
}
/*---------------------------------------------------------------------------*/
static void 
send_unicast(const uip_ipaddr_t *to, const void *data, size_t size) {
    uip_udp_packet_sendto(rmhb_conn, data, size, to, UIP_HTONS(RMHB_PROTO_PORT));
}
/*---------------------------------------------------------------------------*/
static void
send_multicast(const void *data, size_t size) {
    uip_udp_packet_sendto(rmhb_conn, data, size, &ipaddr, UIP_HTONS(RMHB_PROTO_PORT));
}
/*---------------------------------------------------------------------------*/
static void
initialise(void) {
    token = 0;
    token_version = 0;
    etimer_set(&beacon_timer, (beacon_period * CLOCK_SECOND));
    // Set the announcement sequence going ASAP
    etimer_set(&announce_timer, (1 * CLOCK_SECOND));
}
/*---------------------------------------------------------------------------*/
static void
restart_node(void) {
    // Reset the internal state to emulate power loss
    // TODO: stop the beacon timer & reset the neighbour table
    etimer_stop(&rt);
    reset_scheduled = false;
    NETSTACK_RADIO.on();
    leds_off(LEDS_ALL);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rmhb_protocol_process, ev, data) {    
    PROCESS_BEGIN();

                LOG_INFO("RMH-B protocol started\n");

                initialise();

                uip_create_linklocal_allnodes_mcast(&ipaddr); /* Store for later */

                rmhb_conn = udp_new(NULL, UIP_HTONS(RMHB_PROTO_PORT), NULL);
                udp_bind(rmhb_conn, UIP_HTONS(RMHB_PROTO_PORT));

                LOG_INFO("Connection: local/remote port %u/%u\n",
                         UIP_HTONS(rmhb_conn->lport), UIP_HTONS(rmhb_conn->rport));

                while (1) {
                    PROCESS_YIELD();
                    if (ev == tcpip_event) {
                        tcpip_handler();
                    } else if (ev == serial_line_event_message && data != NULL) {
                        serial_handler(data);
                    } else if (etimer_expired(&rt) && reset_scheduled) {
                        LOG_INFO("Restarting node at time %lu\n", (unsigned long) clock_time());
                        restart_node();
                    } else if (etimer_expired(&announce_timer)) {
                        LOG_INFO("Sending neighbour announce at time %lu\n", (unsigned long) clock_time());

                        tpwsn_msg_t msg = { .msg_type = MSG_TYPE_ANNOUNCE };
                        send_multicast(&msg, sizeof(tpwsn_msg_t));

                        etimer_set(&announce_timer, (announce_period * CLOCK_SECOND));
                    } else if (etimer_expired(&beacon_timer)) {
                        LOG_INFO("Sending data beacon\n");

                        // tpwsn_beacon_t msg = {
                        //     .msg_type = MSG_TYPE_BEACON,
                        //     .version = token_version,
                        // };
                        // send_multicast(&msg, sizeof(tpwsn_beacon_t));

                        etimer_restart(&beacon_timer);
                    }
                }
    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
