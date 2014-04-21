
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <stdbool.h>

#include "rlib.h"

#define RECEIVER 2
#define SENDER 1

#define DATA_PACKET_SIZE 12
#define ACK_PACKET_SIZE 8

#define ACK_START 1
#define SEQ_START 1

struct unacked_packet_node {
    int time_since_last_send;
    packet_t* packet;
};
typedef struct unacked_packet_node unacked_t;

packet_t * null_packet;
unacked_t * null_unacked;

struct send_window {
	int window_size;
	uint32_t *last_packet_sent;
	uint32_t *last_ack_received;
    // Array containing the packets the sender has sent but has not received an ack for
    // (along with the time since they were last sent)
    unacked_t* unacked_infos;
};
typedef struct send_window send_window_t;

struct receive_window {
	int window_size;
	uint32_t *last_packet_received;
	uint32_t *last_ack_sent;
};
typedef struct receive_window receive_window_t;

struct reliable_state {
    
    conn_t *c;			/* This is the connection object */
    
    /* Add your own data fields below this */

    struct receive_window;
    struct send_window;

    
    /*Array of size window that holds incomming packets so that they can be added to our linked list in order.
     */
    packet_t* receive_ordering_buffer;
    
    /* probably don't need these as the receiver terminates its end immediately
     bool read_eof;
     bool received_eof;
     */
    
    receive_window_t* receive_window;
    send_window_t* send_window;
};
rel_t *rel_list;


/**
 Send the desired packet and add it to the queue of unacked packets. I'm open to better names.
 Takes in a parameter of a packet in network byte order
 **/
void send_pkt_and_add_to_ack_queue(rel_t * r, packet_t* pkt, int packet_size){
    int order = ntohl(pkt->seqno) - (int) (r->send_window->last_ack_received);
    r->send_window->unacked_infos[order].packet = pkt;
    conn_sendpkt(r->c, pkt, packet_size);
}


/* Creates a new reliable protocol session, returns NULL on failure.
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
rel_t * rel_create (conn_t *c, const struct sockaddr_storage *ss,
	    const struct config_common *cc)
{
  rel_t *r;

  r = xmalloc (sizeof (*r));
  memset (r, 0, sizeof (*r));

  if (!c) {
    c = conn_create (r, ss);
    if (!c) {
      free (r);
      return NULL;
    }
  }

  r->c = c;
  rel_list = r;

  /* Do any other initialization you need here */
    
    null_packet = (packet_t *) malloc(sizeof(packet_t));
    null_packet->seqno = 0;
    
    null_unacked = (unacked_t *) malloc(sizeof(unacked_t));
    null_unacked->time_since_last_send = -1;
    null_unacked->packet = null_packet;
    
    if (c->sender_receiver == RECEIVER){
        //send EOF. Probably should add it to a list of unacked packets in case it is not received.
        //TODO: not sure about the size here. Do all 1000 values need to be read given we only care about the first char?
        //purposedly leaving this ugly with the likely wrong 1000 to catch someones eye.
        packet_t* eof_pkt = malloc(sizeof(packet_t));
        const int bytes_of_data = 1000;
        
        char data[bytes_of_data] = "\0";
        memcpy(eof_pkt->data, data, sizeof(char)* bytes_of_data);
        eof_pkt->len = DATA_PACKET_SIZE + sizeof(char) * bytes_of_data;
        eof_pkt->ackno = 0;
        eof_pkt->seqno = r->seqno;
        r->seqno++;
        eof_pkt->cksum = 0;
        eof_pkt->cksum = cksum((void *) eof_pkt, eof_pkt->len);
        send_pkt_and_add_to_ack_queue(r, eof_pkt, sizeof(char) * bytes_of_data);
    } else if (c->sender_receiver == SENDER){
        
    } else {
        //we're neither sender nor receiver so something went terribly wrong...
    }


  return r;
}

/**
  Removes the first packet from the send window. Shifts everything down by one in order to free up space at the end of the send window so that a new packet
  can be inserted and sent.
*/
void shift_send_buffer (rel_t *r) {
    /* debug("---Entering shift_send_buffer---\n");

    // debug("LAR: %d", r->last_ack_received);

*/
    int i = 0;
    while (
        i < r->window_size &&
        ntohl((r->unacked_infos[i].packet)->seqno) != null_packet->seqno &&
        ntohl((r->unacked_infos[i].packet)->seqno) < r->last_ack_received) {

        /* debug("Freeing Packet from Send: %d \n", ntohl(r->unacked_infos[i].packet.seqno));

        */
    	r->unacked_infos[i] = *null_unacked;
        i++;
    }
    int last_moved = i;

    for (i = 0; i < last_moved; i++) {
        r->unacked_infos[i] = r->unacked_infos[last_moved + i];
    }
    for (i = last_moved; i < r->window_size; i++) {
        r->unacked_infos[i] = *null_unacked;
    }

}


void
rel_destroy (rel_t *r)
{
  conn_destroy (r->c);

  /* Free any other allocated memory here */
}


void
rel_demux (const struct config_common *cc,
	   const struct sockaddr_storage *ss,
	   packet_t *pkt, size_t len)
{
  //leave it blank here!!!
}

void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
}


void
rel_read (rel_t *s)
{
  if(s->c->sender_receiver == RECEIVER)
  {
    //if already sent EOF to the sender
    //  return;
    //else
    //  send EOF to the sender
  }
  else //run in the sender mode
  {
    //same logic as lab 1
  }
}

void
rel_output (rel_t *r)
{
}

void
rel_timer ()
{
  /* Retransmit any packets that need to be retransmitted */
    rel_t* r = rel_list;
    
    while (r != NULL){
        
        /*Temporary constants TODO: replace them with runtime variables
         *
         */
        int sending_window_size = r->window_size;
        int resend_frequency = 5;
        int max_total_resend_time = resend_frequency * 10;
        int i;
        for (i = 0; i< sending_window_size;i++) {
            
            /*unacked nodes is a linked list containing metadata and previously sent packets that have not been successfully acked by the receiver.
             */
            unacked_t* u = &(r -> unacked_infos[i]);
            u->time_since_last_send++;
            
            /*if this is actually a node
             *
             */
            if (u->packet->seqno != null_unacked->packet->seqno){
                if ((u -> time_since_last_send % resend_frequency == 0) && u -> time_since_last_send < max_total_resend_time){
                    conn_sendpkt(r->c, u->packet, ntohs(u->packet->len));
                }
            }
        }
        r = r->next;
    }
}
