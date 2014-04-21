
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

#include "rlib.h"

struct unacked_packet_node {
    int time_since_last_send;
    packet_t* packet;
};
typedef struct unacked_packet_node unacked_t;

struct reliable_state {
    
    conn_t *c;			/* This is the connection object */
    
    /* Add your own data fields below this */
    
    
    //copied directly from old lab (with conn_t obviously not repeated)
    
    // will we have more than one connection?
    rel_t *next;			/* Linked list for traversing all connections */
    rel_t **prev;
    int window_size;
    /*
     // All packets with sequence number lower than ackno have been recieved by the SENDER
     // last frame received
     *
     */
    uint32_t ackno;
    
    /* the last ack received from the receiver */
    uint32_t last_ack_received;
    
    /* The next seqno the receiver is expecting. The lowest order number in the current window.
     */
    uint32_t seqno;
    
    /*Array of size window that holds incomming packets so that they can be added to our linked list in order.
     */
    packet_t* receive_ordering_buffer;
    
    /*
     //Array of size window that holds sent packets. Remove from the buffer when ACK comes back.
     //    packet_t* send_ordering_buffer;
     
     // Array containing the packets the sender has sent but has not received an ack for
     // (along with the time since they were last sent)
     */
    unacked_t* unacked_infos;
    
    /* probably don't need these as the receiver terminates its end immediately
     bool read_eof;
     bool received_eof;
     */
};

rel_t *rel_list;


/**
 Send the desired packet and add it to the queue of unacked packets. I'm open to better names.
 Takes in a parameter of a packet in network byte order
 **/
void send_pkt_and_add_to_ack_queue(rel_t * r, packet_t* pkt, int packet_size){
    int order = ntohl(pkt->seqno) - r->last_ack_received;
    r->unacked_infos[order].packet = pkt;
    conn_sendpkt(r->c, pkt, packet_size);
}


/* Creates a new reliable protocol session, returns NULL on failure.
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
rel_t *
rel_create (conn_t *c, const struct sockaddr_storage *ss,
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


  return r;
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

}
