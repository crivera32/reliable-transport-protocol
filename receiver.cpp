//
// Receiver
//

#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include <iostream>
#include <map>
#include <vector>
#include <iterator>

#include "packet.h"

#define WAIT 10

using namespace std;

//------------------------------------------------------------------------------
// Get the current timestamp
struct timeval timestamp() {
	struct timeval tv;
	struct timezone tz;
	memset(&tv, 0, sizeof(tv));
	memset(&tz, 0, sizeof(tz));
	if (gettimeofday(&tv, &tz) < 0) {
		perror("time:");
	}
	return tv;
}

//------------------------------------------------------------------------------
// Get the difference between 2 timestamps
int timedif(struct timeval *start, struct timeval *end) {
	return SEC * ( end->tv_sec - start->tv_sec ) + ( end->tv_usec - start->tv_usec );
}

//------------------------------------------------------------------------------
// Get the difference between 2 timestamps (in seconds)
double timedif_sec(struct timeval *start, struct timeval *end) {
	return (double)( end->tv_sec - start->tv_sec ) + (double)( end->tv_usec - start->tv_usec ) / (double)SEC;
}

// A map to store the packets
map<int, Packet> m;
map<int, Packet>::iterator current_packet;

vector<Packet> v;

int ttl_time = 0;

//------------------------------------------------------------------------------
// Figure out the current packet in the sequence
int get_current() {

	while(1) {
		auto next = current_packet;
		++next;
		if (next == m.end()) {
			return current_packet->second.getSeqno();
		}
		if (current_packet->second.getSeqno() + 1 < next->second.getSeqno()) {
			return current_packet->second.getSeqno();
		}

		++current_packet;
	}
}

//------------------------------------------------------------------------------
// Set the address/port in sockaddr
void set_addr(struct sockaddr_in *a, unsigned long addr, unsigned short port) {
	a->sin_addr.s_addr = addr;
	a->sin_port = port;
}

//------------------------------------------------------------------------------
int main(int argc, char** argv) {	
	if(argc<2) {
		fprintf(stderr, "Usage: ./receiver <port>\n");
		exit(1);
	}

	// Setup the socket
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(sock < 0) {
		perror("Creating socket failed: ");
		exit(1);
	}
	int yes=1;
 	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		perror("setsockopt");
		exit(1);
	}

	// Set timeout value
	struct timeval t;
	t.tv_sec = WAIT;
	t.tv_usec = 0;
	if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t)) < 0) {
		perror("setsockopt");
		exit(1);
	}

	// local address
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(argv[1]));
	addr.sin_addr.s_addr = INADDR_ANY;

	// sender's address
	struct sockaddr_in remote_addr;
	remote_addr.sin_family = AF_INET;
	remote_addr.sin_port = 0;
	remote_addr.sin_addr.s_addr = 0;
	
	// Bind the socket to a port
	int res = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
	if(res < 0) {
		perror("Error binding: ");
		exit(1);
	}
	
	// Initialize ports and addresses
	unsigned long myaddr = 0;
	unsigned short myport = htons(atoi(argv[1]));
	unsigned long dst_addr = 0;
	unsigned short dst_port = 0;

	// Buffer to store the message segment
	char buf[MSS];
	memset(buf,'\0',MSS);
	
	// Initialize some state variables
	srand(time(NULL));
	int current = rand() % 10000; // Current seqno
	int init_seqno = current; // Starting seqno
	bool loop = true; // Loop condition
	bool handshaking = true; // Handshake state
	bool complete = false; // Transfer completion status
	bool pointer_init = false; // This is needed to track the Map iterator state
	int duplicates = 0; // Count how many duplicates were sent.  (for unreliable connections)

	fprintf(stderr, "Initial seqno is %d.\n", init_seqno);
    
	int x = 0;
	int fin_seqno = -99;
	// Receive data
	while(1) {
		Packet pkt(sock);
		
		if (pkt.getTimeout()) {
			//fprintf(stderr, "timeout %d\n", x);
			//x++;
			if (loop) continue;

			shutdown(sock,SHUT_RDWR);
			close(sock);
			break;
		}
		else {
			loop = false;
		}

		if (pkt.isSyn()) {
			if (dst_port != pkt.getSrcPort() || dst_addr != pkt.getSrcAddr()) {
				if (handshaking) {
					dst_port = pkt.getSrcPort();
					dst_addr = pkt.getSrcAddr();
					myaddr = pkt.getDstAddr();
					set_addr(&remote_addr, pkt.getSrcAddr(), pkt.getSrcPort());
				}
				else {
					fprintf(stderr, "Error: received SYN from a different sender.  Exiting...\n");
					shutdown(sock,SHUT_RDWR);
					close(sock);
					exit(1);
				}
			}
			Packet ack(myaddr,myport,dst_port,dst_addr, true, true, current, pkt.getSeqno(), NULL, 0);
			ack.setTimestamp(pkt.getTimestamp());
			ack.sendPacket(sock, (struct sockaddr*)&remote_addr, sizeof(remote_addr));
		}
		// Verify checksum is correct
		else if (pkt.check()) {
			if (handshaking && pkt.isAck() == false) {
				//fprintf(stderr, "Packet ignored during handshake...\n");
				//pkt.print();
				continue;
			}
			
			if (handshaking && pkt.isAck()) {
				//cout << "got ack:" << pkt.getSeqno() << "/" << current << endl;
				if (pkt.getAckno() != current) {
					//fprintf(stderr, "Wrong ackno during handshake... %d/%d\n",
					//	pkt.getAckno(), current);
					//pkt.print();
					continue;
				}

				handshaking = false;
			}

			// If this packet is not a duplicate, store it
			if (m.count(pkt.getSeqno()) < 1) {
				m.insert( pair<int, Packet>(pkt.getSeqno(), pkt) );
				fprintf(stderr, "got pkt %d", pkt.getSeqno());
				if (pointer_init == false) {
					pointer_init = true;
					current_packet = m.begin();
				}
				current = get_current();
				fprintf(stderr, ", now waiting for %d\n", current + 1);
			}
			else {
				// Keep track of duplicate packets
				fprintf(stderr, "***Duplicate %d (waiting for %d)\n", pkt.getSeqno(), current + 1);
				duplicates++;
			}

			// If this is the final packet, exit
			if (pkt.isFin())
				fin_seqno = pkt.getSeqno();
			if (current_packet->second.getSeqno() == fin_seqno)
				complete = true;
	
			// Send an ack
			int ack_seq = current_packet->second.getSeqno();
			Packet ack(myaddr,myport,dst_port,dst_addr, false, true, pkt.getSeqno(), ack_seq, NULL, 0);

			// Acknowledge transfer completion
			if (complete) {
				ack.setFin(true);
			}
			else {
				ack.setFin(false);
			}

			ack.setTimestamp(pkt.getTimestamp());
			ack.sendPacket(sock, (struct sockaddr*)&remote_addr, sizeof(remote_addr));
			
			if (complete)
				break;
		}
		else {
			//cout << "Checksum failed" << endl;
		}
	}
	
	// Clean up
	shutdown(sock,SHUT_RDWR);
	close(sock);
    
    // Write file contents to standard output
	for (map<int, Packet>::iterator iter = m.begin(); iter != m.end(); ++iter) {
		char *p = iter->second.getPayload();
		write(1, p, iter->second.getLength());
	}
	
	if (complete)
		fprintf(stderr, "File transfer complete.  Exiting...\n");
	else
		fprintf(stderr, "Connection timed out.  Exiting...\n");
	fprintf(stderr, "DUPLICATES: %d\n", duplicates);

	return 0;
}
