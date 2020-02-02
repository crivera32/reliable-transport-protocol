//
// Sender
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <iostream>
#include <map>
#include <unordered_map>
#include <iterator>

#include "packet.h"

#define ALPHA		0.125
#define BETA		0.25

#define TIMEOUT_LIMIT	SEC * 10

#define LOSS_COUNTDOWN 1
#define LOSS_THRESHOLD 0.02
#define SAMPLE_COUNT 1

using namespace std;

//------------------------------------------------------------------------------
// Calculate new RTT
int update_rtt(int estimate, int sample) {
	return (1-ALPHA)*estimate + ALPHA*sample;
}

//------------------------------------------------------------------------------
// Calculate new Deviation
int update_dev(int estimate, int sample, int dev) {
	int dif = sample - estimate;
	if (dif < 0) dif = dif * -1;
	return (1-BETA)*dev + BETA*dif;
}

//------------------------------------------------------------------------------
// Calculate new send rate
double update_rate(double estimate, double sample) {
	return (1-ALPHA)*estimate + ALPHA*sample;
}

//------------------------------------------------------------------------------
// Get current timestamp
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
// Get the difference between 2 timestamps (in)
double timedif_sec(struct timeval *start, struct timeval *end) {
	return (double)( end->tv_sec - start->tv_sec ) + (double)( end->tv_usec - start->tv_usec ) / (double)SEC;
}

//------------------------------------------------------------------------------
// Print the address from sockaddr
void print_addr(struct sockaddr_in *a) {
	char ip[100];
	inet_ntop(AF_INET, &(a->sin_addr.s_addr), ip, 100);			
	printf("ADDR: %s\nPORT: %d\n", ip, ntohs(a->sin_port));
}

// Set up the sockets, addresses, ports
int s_fd;
struct sockaddr_in remote_addr;
struct sockaddr_in addr;
unsigned long myaddr;
unsigned short myport;

// Initialize state variables
int rtt = SEC / 10;
int dev = 0;
double loss_rate = 0;
int threshold = 100;
int start_seq = 0;

int window = 10;
int total_packets_sent = 0;
struct timeval init_time;
struct timeval last_send;
double send_rate = 1;

// Use a Map to store packets to be sent
map<int, Packet> m;
map<int, Packet>::iterator current_packet;
map<int, Packet>::iterator end_of_window;
unordered_map<int, int> duplicates; // Track number of duplicates for each packet
unordered_map<int, struct timeval> send_times; // Track send timestamps for each packet
unordered_map<int, bool> losses; // Track number of packet losses for each packet

//------------------------------------------------------------------------------
// Send a syn packet
int send_syn() {
	Packet syn_pkt(myaddr,myport,
		remote_addr.sin_addr.s_addr,
		remote_addr.sin_port,
		true, false, 0, 0, NULL, 0);

	struct timeval total_time_waiting = timestamp();
	bool done = false;
	while (!done) {
		// Send the packet, store the timestamp when the packet was sent
		struct timeval send_time = timestamp();
		syn_pkt.setTimestamp(send_time);
		syn_pkt.sendPacket(s_fd, (struct sockaddr*)&remote_addr, sizeof(addr));

		// Wait for an ack
		while (true) {
			Packet ack(s_fd);
			struct timeval current_t = timestamp();

			// Make sure it's the correct response
			if (!ack.getTimeout() && ack.isAck() && ack.isSyn()) {
				start_seq = ack.getSeqno();
				done = true;
				send_time = ack.getTimestamp();
				int sample_rtt = timedif(&send_time, &current_t);
				rtt = update_rtt(rtt, sample_rtt);
				dev = update_dev(rtt, sample_rtt, dev);
				break;
			}

			// Make sure we don't wait too long
			if (TIMEOUT_LIMIT < timedif(&total_time_waiting, &current_t)) return -1;
			if (rtt+4*dev < timedif(&send_time, &current_t)) break;
		}
	}
	return 0;
}

//------------------------------------------------------------------------------
// Count the number of packet losses in the packet window
int count_losses(int start, int latest, int w) {
	int count = 0;
	for (int i = start; i < start + w; i++) {
		if (i < latest || losses.count(i) < 1)
			continue;
		if (losses[i] == true)
			count++;
	}
	return count;
}

//------------------------------------------------------------------------------
// Send packets currently in the packet window
void sendPackets(int w) {
	// Start with the current packet
	auto iter = current_packet;

	while (0 < w && iter != m.end()) {
		auto current_t = timestamp();
		Packet pkt = iter->second;
		++iter;

		// If we already sent this one, then skip it
		if (duplicates.count(pkt.getSeqno()) > 0) {
			continue;
		}

		// Get the timestamp of this packet
		struct timeval send_t;
		memset(&send_t, 0, sizeof(send_t));
		if (send_times.count(pkt.getSeqno()) > 0) {
			send_t = send_times[pkt.getSeqno()];
		}
		
		// Send this packet if we haven't sent it in a while
		if (send_times.count(pkt.getSeqno()) < 1 || timedif(&send_t, &current_t) > rtt+4*dev) {
			pkt.setTimestamp(current_t);
			pkt.sendPacket(s_fd, (struct sockaddr*)&remote_addr, sizeof(addr));
			total_packets_sent++;
			send_times[pkt.getSeqno()] = current_t;
			losses[pkt.getSeqno()] = true;
		}

		// Decrease window counter
		--w;
	}
}

//------------------------------------------------------------------------------
// Find and return the current packet
int update_current(int latest_ack) {
	if (latest_ack < current_packet->second.getSeqno()) return 0;
	int i = 0;
	while (current_packet != m.end() && current_packet->second.getSeqno() < latest_ack + 1
		&& current_packet->second.isFin() == false) {
		++current_packet;
		++i;
	}
	return i;
}

//------------------------------------------------------------------------------
// Update the end-of-window marker
void update_end() {
	auto temp = current_packet;
	int i = window;
	while (temp != m.end() && i - 1 > 0) {
		++temp;
		--i;
		if (temp != m.end())
			end_of_window = temp;
	}
}

//------------------------------------------------------------------------------
// 
void sendNext(int w) {
	auto iter = end_of_window;

	while (0 < w && iter != m.end()) {
		auto current_t = timestamp();
		Packet pkt = iter->second;
		++iter;

		// Check for duplicates
		if (duplicates.count(pkt.getSeqno()) > 0) {
			continue;
		}

		// Store the send time for this packet
		struct timeval send_t;
		memset(&send_t, 0, sizeof(send_t));
		if (send_times.count(pkt.getSeqno()) > 0) {
			send_t = send_times[pkt.getSeqno()];
		}
		
		// Send the packet if it hasn't been sent in a while
		if (send_times.count(pkt.getSeqno()) < 1 || timedif(&send_t, &current_t) > rtt+4*dev) {
			pkt.setTimestamp(current_t);
			pkt.sendPacket(s_fd, (struct sockaddr*)&remote_addr, sizeof(addr));
			total_packets_sent++;
			send_times[pkt.getSeqno()] = current_t;
			losses[pkt.getSeqno()] = true;
		}

		// Decrease window size
		--w;
	}
}

//------------------------------------------------------------------------------
int main(int argc, char** argv) {
	if(argc<4) {
		printf("Usage: ./sender <ip> <port> <filename>\n");
		exit(1);
	}
	
	// Set up the socket
	s_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(s_fd < 0) {
		perror("Creating socket failed: ");
		exit(1);
	}
	struct timeval t;
	t.tv_sec = 0;
	t.tv_usec = 100;
	if (setsockopt(s_fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t)) < 0) {
		perror("setsockopt");
		exit(1);
	}
	
	// Configure the address
	addr.sin_family = AF_INET;
	addr.sin_port = 0;
	addr.sin_addr.s_addr = INADDR_ANY;
	
	struct sockaddr_in test_addr;
	test_addr.sin_family = AF_INET;
	test_addr.sin_port = 0;
	test_addr.sin_addr.s_addr = INADDR_ANY;

	remote_addr.sin_family = AF_INET;
	remote_addr.sin_port = htons(atoi(argv[2]));
	inet_pton(AF_INET,argv[1],&remote_addr.sin_addr.s_addr);

	// Get local address
	int test_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(test_sock < 0) {
		perror("Creating socket failed: ");
		exit(1);
	}
	connect(test_sock, (struct sockaddr*)&remote_addr, sizeof(remote_addr));

	socklen_t len = sizeof(test_addr);
	getsockname(test_sock, (struct sockaddr*)&test_addr, &len);
	myaddr = test_addr.sin_addr.s_addr;
	char myaddr_buf[100];
	inet_ntop(AF_INET, &(test_addr.sin_addr.s_addr), myaddr_buf, 100);

	// Close the test socket, since it is no longer needed.  (Just used to get the local address)
	shutdown(test_sock,SHUT_RDWR);
	close(test_sock);

	// Bind our main socket
	int res = bind(s_fd, (struct sockaddr*)&addr, sizeof(addr));
	if(res < 0) {
		perror("Error binding: ");
		exit(1);
	}

	len = sizeof(addr);
	getsockname(s_fd, (struct sockaddr*)&addr, &len);
	myport = addr.sin_port;

	// Open the file to be sent
	FILE *f = NULL;
	f = fopen(argv[3],"r");
	if(!f) {
		perror("problem opening file");
		exit(0);
	}
	
	// Read in all the data
	char buf[MSS];
	memset(buf, '\0', MSS);
	int i = 0;

	if (send_syn() < 0) {
		fprintf(stderr, "Handshake failed: connection timed out.\n");
		exit(1);
	}

	int bytes_read = fread(buf,sizeof(char), MSS-HEADER_SIZE-1, f);
	while(bytes_read > 0) {
		i++;
		int init_ackno = (i==1) ? start_seq : 0;

		Packet pkt(myaddr,myport,remote_addr.sin_addr.s_addr,remote_addr.sin_port,
			   false, (i == 1), i + start_seq, init_ackno, buf, bytes_read);

		m.insert( pair<int, Packet>(i, pkt) );
		memset(buf, '\0', MSS);
		//m.at(i).print();
		bytes_read = fread(buf,sizeof(char), MSS-HEADER_SIZE-1, f);
	}

	// Set completion flag on final packet
	{
		auto iter = m.end();
		--iter;
		iter->second.setFin(true);
	}

	// Initialize some state variables
	int total = i;
	int sent = 0;

	current_packet = m.begin();
	end_of_window = m.begin();
	bool complete = false;
	bool first_loss = false;
	int got_duplicates = 0;
	int loss_counter = LOSS_COUNTDOWN;

	bool threshold_init = true;
	int w_size_total = 0;
	int w_num_total = 0;	
	
	init_time = timestamp();
	last_send = init_time;
	struct timeval last_ack_time = init_time;


	while (!complete) {
		Packet pkt = current_packet->second;
		int latest_ack = -1;
		int waiting_for_ack = pkt.getSeqno() + window - 1;
		
		struct timeval start_t = timestamp();
		fprintf(stderr, "Sending %d pkts (from %d)", window, pkt.getSeqno());
		fprintf(stderr, "  [rtt: %dms]\n", rtt/1000);
		got_duplicates = 0;

		last_send = start_t;

		// Send packets in the current window
		sendPackets(window);
		// Increase window size
		w_size_total += window;
		w_num_total++;

		bool timeout = false;
		int sample_rtt = 0;
		int samples = SAMPLE_COUNT;
		auto start_packet = current_packet;

		int start_of_window = current_packet->second.getSeqno();

		// Wait for an ack until a timeout is reached
		while (!timeout) {
			Packet ack(s_fd);
			struct timeval current_t = timestamp();
			if (timedif(&last_ack_time, &current_t) > TIMEOUT_LIMIT) {
				cout << "\nConnection timed out.  Exiting...\n";
				shutdown(s_fd,SHUT_RDWR);
				close(s_fd);
				exit(1);
			}

			// Get the ack's timestamp for comparison
			struct timeval send_t = ack.getTimestamp();
			sample_rtt = timedif(&send_t, &current_t);
			
			if (ack.getTimeout() == false && ack.isAck() == true) {
				// We got an ack, so update the ackno and initialize the duplicate counter
				if (latest_ack < ack.getAckno())
					latest_ack = ack.getAckno();
				if (duplicates.count(ack.getSeqno()) < 1) {
					duplicates[ack.getSeqno()] = 0;
				}
				duplicates[ack.getSeqno()] += 1;
				if (duplicates[ack.getSeqno()] > 1) {
					got_duplicates++;
				}
				losses[ack.getSeqno()] = false;
				losses[ack.getAckno()] = false;
				
				// Update RTT sampling
				last_ack_time = timestamp();
				if (samples > 0) {
					rtt = update_rtt(rtt, sample_rtt);
					dev = update_dev(rtt, sample_rtt, dev);
					samples--;			
				}
				
				int temp = update_current(ack.getAckno());
				sendNext(temp);
				update_end();

				// Update the loss count, reduce window size if needed
				if (ack.getAckno() == waiting_for_ack) {
					int loss_count = count_losses(start_of_window, latest_ack, window);
					loss_counter = LOSS_COUNTDOWN;
					if (window > threshold / 2)
						window += 1;
					else {
						window = window * 2;
						if (window > threshold / 2)
							window = (threshold / 2) + 1;
					}
					break;
				}

				// Check whether the transmission ended
				if (ack.isFin()) {
					complete = true;
					break;
				}
			}

			// Check the loss tolerance threshold, update the window if needed
			if (rtt+4*dev < timedif(&start_t, &current_t) || SEC * 1.2 < timedif(&start_t, &current_t)) {
				int loss_count = count_losses(start_of_window, latest_ack, window);
				int loss_tolerance = 2;
				if (window < threshold)
					loss_tolerance += window * LOSS_THRESHOLD;

				if (loss_count > loss_tolerance) {
					loss_counter--;
					if (loss_counter < 1) {
						if (threshold_init) {
							threshold = window;
							threshold_init = false;
						}
						else {
							threshold = update_rtt(threshold, window);
							threshold = window;
						}
						window = window * 0.5;
						if (window < 10)
							window = 10;
						timeout = true;
						first_loss = true;
						loss_counter = LOSS_COUNTDOWN;
					}
				}
				break;
			}
		}
		if (timeout) {
			//cout << "Fewer acks than expected.  Retrying...\n";
		}
	}
	
	cout << "\nFile transfer complete.  Exiting...\n\n";
	
	auto finish_t = timestamp();
	double seconds = (double)timedif(&init_time, &finish_t) / (double)SEC;

	fprintf(stderr, "TOTAL TIME:\t\t%f sec\n", seconds);
	fprintf(stderr, "AVG SEND RATE:\t\t%f pkt/sec\n", (double)total_packets_sent / seconds);

	// Clean uo the socket
	shutdown(s_fd,SHUT_RDWR);
	close(s_fd);
	
	return 0;
}
