
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




#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

#define debug(...)  fprintf(stderr, __VA_ARGS__)

#define debug_send(...) debug(KGRN __VA_ARGS__)
#define debug_recv(...) debug(KBLU __VA_ARGS__)



/*
 #define debug(...)
 */

#define DATA_PACKET_SIZE 12
#define ACK_PACKET_SIZE 8

#define ACK_START 1
#define SEQ_START 1

struct unacked_packet_node {
    int time_since_last_send;
    packet_t * packet;
};
typedef struct unacked_packet_node unacked_t;

packet_t * null_packet;
unacked_t * null_unacked;

struct reliable_state {
    rel_t *next;			/* Linked list for traversing all connections */
    rel_t **prev;
    
    conn_t *c;			/* This is the connection object */
    
    /* Add your own data fields below this */
    
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
    
    bool read_eof;
    bool received_eof;
    
};
rel_t *rel_list;



rel_t * rel_create(conn_t *c, const struct sockaddr_storage *ss, const struct config_common *cc);
void destroy_packet(packet_t* p);
void destroy_unacked(unacked_t* u);
void rel_destroy (rel_t *r);
void print_window(packet_t * window, size_t window_size);
void rel_demux (const struct config_common *cc, const struct sockaddr_storage *ss, packet_t *pkt, size_t len);
void send_ack(rel_t *r);
void shift_receive_buffer (rel_t *r);
void shift_send_buffer (rel_t *r);
void rel_recvpkt (rel_t *r, packet_t *pkt, size_t n);
void rel_read (rel_t *r);
void rel_output (rel_t *r);
void rel_timer ();



/* Creates a new reliable protocol session, returns NULL on failure.
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
rel_t * rel_create (conn_t *c, const struct sockaddr_storage *ss,
                    const struct config_common *cc)
{
    rel_t *r;
    r = (rel_t*) xmalloc (sizeof (*r));
    memset (r, 0, sizeof (*r));
    
    if (!c) {
        c = conn_create (r, ss);
        if (!c) {
            free (r);
            return NULL;
        }
    }
    
    r->c = c;
    r->next = rel_list;
    r->prev = &rel_list;
    if (rel_list) {
        rel_list->prev = &r->next;
    }
    
    rel_list = r;
    
    /* Do any other initialization you need here */
    
    /* TODO */
    
    
    null_packet = (packet_t *) malloc(sizeof(packet_t));
    null_packet->seqno = 0;
    
    null_unacked = (unacked_t *) malloc(sizeof(unacked_t));
    null_unacked->time_since_last_send = -1;
    null_unacked->packet = null_packet;
    
    r->window_size = cc->window;
    
    packet_t * receive_buff = (packet_t*) xmalloc(sizeof(packet_t) * cc->window);
    r->receive_ordering_buffer = receive_buff;
    
    unacked_t * info = (unacked_t*) xmalloc(sizeof(unacked_t) * cc->window);
    r->unacked_infos = info;
    
    int i;
    for (i = 0; i < cc->window; i++) {
        memcpy(&(r->receive_ordering_buffer[i]), null_packet, sizeof(packet_t));
        memcpy(&(r->unacked_infos[i]), null_unacked, sizeof(unacked_t));
        r->unacked_infos[i].time_since_last_send = 0;
    }
    
    r->seqno = SEQ_START;
    r->ackno = ACK_START;
    r->last_ack_received = ACK_START;
    r->read_eof = false;
    r->received_eof = false;
    return r;
}

void destroy_packet(packet_t* p) {
    free(p);
}

void destroy_unacked(unacked_t* u) {
    free(u);
}

void rel_destroy (rel_t *r)
{
    debug("Rel Destroy\n");
    return;
    if (r->next)
        r->next->prev = r->prev;
    *r->prev = r->next;
    conn_destroy (r->c);
    
    /* Free any other allocated memory here */
    
    /* TODO */
    rel_t* current = r;
    while (current != NULL) {
        rel_t* next = r->next;
        destroy_packet(current->receive_ordering_buffer);
        /*        destroy_packet(current->send_ordering_buffer); */
        destroy_unacked(current->unacked_infos);
        free(current);
        current = next;
    }
}

void check_complete(rel_t * r) {
    debug("calling check_complete\n");
    debug("read_eof %d, received_eof %d \n", r->read_eof, r->received_eof);
    
    if (r->read_eof && r->received_eof) {
        /* all packets ACKed */
        if ((r->unacked_infos[0].packet)->seqno != null_packet->seqno) {
            debug("Waiting on unacked packets.\n");
            return;
        }
        /* all output written */
        if (r->receive_ordering_buffer[0].ackno !=  null_packet->ackno) {
            debug("Not all output outputted.\n");
            return;
        }
        
        rel_destroy(r);
        exit(0);
    }
}

void print_window(packet_t * window, size_t window_size) {
    return;
    debug("|");
    
    int i;
    for (i = 0; i < window_size; i++) {
        debug("------|");
    }
    debug("\n");
    for (i = 0; i < window_size; i++) {
        packet_t cur = window[i];
        debug("%7.5d", cur.seqno);
    }
    debug("\n");
    debug("|");
    for (i = 0; i < window_size; i++) {
        debug("------|");
    }
    debug("\n");
}



/* This function only gets called when the process is running as a
 * server and must handle connections from multiple clients.  You have
 * to look up the rel_t structure based on the address in the
 * sockaddr_storage passed in.  If this is a new connection (sequence
 * number 1), you will need to allocate a new conn_t using rel_create
 * ().  (Pass rel_create NULL for the conn_t, so it will know to
 * allocate a new connection.)
 */
void rel_demux (const struct config_common *cc,
                const struct sockaddr_storage *ss,
                packet_t *pkt, size_t len)
{
    /* LAB ASSIGNMENT SAYS NOT TO TOUCH rel_demux() */
}



void send_ack(rel_t *r) {
    // debug("---Entering send_ack---\n");
    debug("Sending ACK %d.\n", r->ackno);
    
    packet_t * pkt = (packet_t*) malloc(sizeof(packet_t));
    pkt->len = htons(ACK_PACKET_SIZE);
    pkt->ackno = htonl(r->ackno);
    pkt->seqno = 0;
    pkt->cksum = cksum((void*) pkt, ACK_PACKET_SIZE);
    
    conn_sendpkt(r->c, pkt, ACK_PACKET_SIZE);
    
    free(pkt);
}

/*
 Method to maintain the order of the receive_ordering_buffer.
 */
void shift_receive_buffer (rel_t *r) {
    print_window(r->receive_ordering_buffer, r->window_size);
    /* debug("---Entering shift_receive_buffer---\n"); */
    
    if (r->receive_ordering_buffer[0].seqno == null_packet->seqno){
        return;
    }
    
    /* debug("Freeing Packet from Receive: %d \n", r->receive_ordering_buffer[0].seqno);
     */
    int i;
    for (i = 0; i< r->window_size - 1; i++){
        r->receive_ordering_buffer[i] = r->receive_ordering_buffer [i+1];
    }
    r->receive_ordering_buffer[r->window_size - 1] = *null_packet;
    
    shift_receive_buffer(r);
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


/*
 Examine incomming packets. If the packet is an ACK then remove the corresponding packet from our send buffer.
 If it is data, see if we have already received that data (if it is a lower seq number than the lowest of our current frame, or if it is already in our recieved buffer).
 If it has already been recieved, do nothing with it but send the ACK. If it has not been recieved, put it in its ordered place in the buffer and send the ACK.
 
 n is the actual size where pkt -> len is what it should be.
 A discrepancy would indicate some bits were or the packet is not properly formed. Thus we will discard it.
 */
void rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
	pkt->len = ntohs(pkt->len);
	pkt->ackno = ntohl(pkt->ackno);
	pkt->seqno = ntohl(pkt->seqno);
    
    if (n != pkt->len) {
        /*we have not received the full packet or it's an error packet
         //ignore it and wait for the packet to come in its entirety
         */
        debug_recv("Bytes read: (%d) is different from pkt->len (%d); this is an error packet, return.\n", pkt->len, n);
        return;
    }
    
    /* debug("received packet of len: %d\n", pkt->len);
     *
     */
    
    if (pkt->len < DATA_PACKET_SIZE) {
        
        debug_recv("Received an ACK of: %d\n", pkt->ackno);
        debug_recv("\tReceiver received packet with seqno: %d\n", pkt->ackno - 1);
        debug_recv("\tFirst packet in send window: %d\n", ntohl(r->unacked_infos[0].packet->seqno));
        
        
        /* the ackno that was sent to us should be one larger than the last ack received on the sender side
         */
        if (pkt->ackno <= r->last_ack_received) {
            debug_recv("Ack No %d is out of order (already received %d)\n", pkt->ackno, r->last_ack_received);
            return;
        }
        
        uint32_t ackno = pkt->ackno;
        
        /* if the ackno is for the first packet in the send window (which it should be)
         // the ack number is the number of the packet the receiver is waiting for.
         *
         */
        
        if (ackno < ntohl(r->unacked_infos[0].packet->seqno) + 1) {
            debug_recv("Ack No %d is smaller than the send window.\n", ackno);
            return;
        }
        /* if we get to this point, then all seems good and we wil remove the packet that was acked from the beginning of the array
         // increment last_ack_received and then shift the buffer
         
         // is it always the first one in the list?
         *
         */
        memcpy(&(r->unacked_infos[0]), null_unacked, sizeof(unacked_t));
        r->last_ack_received = ackno;
        shift_send_buffer(r);
        
        rel_read(r);
    }
    else {
        /*pkt is a data PACKET */
        /*
         debug("received Data packet\n");
         debug("Ackno: %d Seqno: %d\n", r->ackno, pkt ->seqno);
         */
        int offset = (pkt -> seqno) - (r -> ackno);
        
        if (offset >= 0 && offset < r->window_size) {
            /* offset tells where in the receive_ordering_buffer this packet falls
             */
            r -> receive_ordering_buffer[offset] = *pkt;
            rel_output(r);
        }
        else {
            /* already received this data
             *
             */
            if (offset < 0) {
                /* need to ack it anyway
                 *
                 */
                send_ack(r);
            }
            else {
                debug_recv("Error, data packet too larger for receive window.");
            }
        }
        
    }
    check_complete(r);
}


/*
 Take information from standard input and create packets.  Will call some means of sending to the appropriate receiver.
 */
void rel_read (rel_t *r)
{
    /* debug("---Entering rel_read---\n");
     *
     */
	char buffer[500];
	/* drain the console
	 *
	 */
	while (true) {
        if (r->seqno - r->last_ack_received > r->window_size) {
            debug_send("Sequence Number (%d) for new packet is too large for window. (%d -> %d)\n", r->seqno, r->last_ack_received, r->last_ack_received + r->window_size);
            /*
             cannot fit any new packets into the buffer
             don't read anything in
             */
            return;
        }
        
		int bytes_read = conn_input(r->c, buffer, 500);
        
		if (bytes_read == 0) {
			return;
		}
        
        /* Read no bytes and already read the end of the file */
        if (bytes_read == -1 && r->read_eof) {
            return;
        }
        
        debug_send("Read %d bytes\n", bytes_read);
        /* this may need to be r->seqno - 1
         // debug("Current SeqNo: %d \t Last ACK: %d \t Window Size: %d\n", r->seqno, r->last_ack_received, r->window_size);
         */
        
		packet_t * pkt = (packet_t *)malloc(sizeof(packet_t));
		int packet_size = DATA_PACKET_SIZE;
		if (bytes_read > 0) {
			memcpy(pkt->data, buffer, bytes_read);
			packet_size += bytes_read;
		}
        debug_send("Sending Packet #%d\n", r->seqno);
        
		pkt->seqno = htonl(r->seqno);
		pkt->len = htons(packet_size);
		pkt->ackno = htonl(r->ackno);
		pkt->cksum = cksum((void*)pkt, packet_size);
        
        /* this packet seqno into the sender buffer and keep here until we receive the ack back from receiver
         */
        int order = ntohl(pkt->seqno) - r->last_ack_received;
        r->unacked_infos[order].packet = pkt;
        
        conn_sendpkt(r->c, pkt, packet_size);
        
        /* increment the sequence number for next time
         *
         */
        r->seqno = r->seqno + 1;
        
		if (bytes_read == -1) {
            r->read_eof = true;
            debug_send("EOF Packet sent with size %d.\n", ntohs(pkt->len));
            check_complete(r);
			return;
		}
	}
}


void rel_output (rel_t *r) {
	/* debug("---Entering rel_output---\n");
	 *
	 */
	int i = 0;
	for (i = 0; i < r->window_size; i++) {
		packet_t f = r->receive_ordering_buffer[i];
		if (f.ackno == null_packet->ackno) {
			/* first out of order packet encountered
			 *
			 */
			break;
		}
		else {
			/* if we have enough space in the buffer
			 *
			 */
			if (conn_bufspace(r->c) > (f.len)) {
                
                r->ackno = r->ackno + 1;
                send_ack(r);
                
                if (f.len == DATA_PACKET_SIZE) {
                    debug("EOF Packet Received!\n");
                    r->received_eof = true;
                    /* this is an EOF packet */
                    conn_output(r->c, f.data, 0);
                }
                else {
                    conn_output(r->c, f.data, f.len - DATA_PACKET_SIZE);
                }
				
                
                /* send ack to free up the sender's send window
                 *
                 */
                
                
                
			}
		}
        debug("LOL\n");
	}
    
    /* shift our receive buffer for the packets that have now been outputted
     *
     */
    shift_receive_buffer(r);
}

/*
 Run through our state of packets that have been sent but have not yet been acked.  At a specified interval, resend them.
 There also exists a maximum time after which the packet will not continue to be resent.
 Timeout
 */
void rel_timer ()
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


