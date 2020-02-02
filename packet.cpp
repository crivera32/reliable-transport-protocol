//
// packet.cpp
//

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "packet.h"

using namespace std;

//------------------------------------------------------------------------------
// Constructor 1
Packet::Packet(unsigned long _src_addr, unsigned short _src_port,
		unsigned long _dst_addr, unsigned short _dst_port,
		bool _syn, bool _ack,
		unsigned int _seqno, unsigned int _ackno,
		char *buf, unsigned int _len) {
	timeout = false;

	memset(buffer, '\0', MSS);
	memset(payload, '\0', MSS - HEADER_SIZE);

	src_addr = _src_addr;
	src_port = _src_port;

	dst_addr = _dst_addr;
	dst_port = _dst_port;
	
	syn = _syn;
	ack = _ack;
	fin = false;

	seqno = _seqno;
	ackno = _ackno;

	memset(&ts, 0, sizeof(ts));

	length = _len;
	recv_length = length;

	if (buf != NULL) {
		memcpy(payload, buf, MSS - HEADER_SIZE);
	}
	
	checksum = computeChecksum(buf, length);

	int i = loadHeader();
}

//------------------------------------------------------------------------------
// Constructor 2
Packet::Packet(int fd) {
	memset(buffer, '\0', MSS);
	memset(payload, '\0', MSS - HEADER_SIZE);

	src_addr = 0;
	src_port = 0;

	dst_addr = 0;
	dst_port = 0;
	
	syn = false;
	ack = false;
	fin = false;

	seqno = 0;
	ackno = 0;

	memset(&ts, 0, sizeof(ts));	

	length = 0;
	recv_length = 0;
	checksum = 0;
	
	timeout = false;
	int recv_count = recv(fd, buffer, MSS, 0);
	if (recv_count < 1)
		timeout = true;
	else
		unloadHeader();

	recv_length = recv_count;
}

//------------------------------------------------------------------------------
// Sends the packet to the specified address from the specified port
int Packet::sendPacket(int fd, struct sockaddr *addr, int len) {
	loadHeader();
	int send_len = HEADER_SIZE + length;
	if (send_len > MSS)
		send_len = MSS;
	int send_count = sendto(fd, buffer, send_len, 0,addr,len);
	return send_count;
}

//------------------------------------------------------------------------------
// Print packet data
void Packet::print() {
	fprintf(stderr, "========================================\n");
	fprintf(stderr, "PACKET INFO:\n");
	fprintf(stderr, "----------------------------------------\n");
	
	char ip[100];
	inet_ntop(AF_INET, &(src_addr), ip, 100);
	fprintf(stderr, "SRC ADDR/PORT:\t%s/%d\n", ip, ntohs(src_port));

	inet_ntop(AF_INET, &(dst_addr), ip, 100);
	fprintf(stderr, "DST ADDR/PORT:\t%s/%d\n", ip, ntohs(dst_port));

	fprintf(stderr, "----------------------------------------\n");
	fprintf(stderr, "SYN:\t\t");
	if (syn) fprintf(stderr, "1\n");
	else fprintf(stderr, "0\n");
	
	fprintf(stderr, "ACK:\t\t");
	if (ack) fprintf(stderr, "1\n");
	else fprintf(stderr, "0\n");

	fprintf(stderr, "FIN:\t\t");
	if (fin) fprintf(stderr, "1\n");
	else fprintf(stderr, "0\n");

	fprintf(stderr, "----------------------------------------\n");
	fprintf(stderr, "SEQNO:\t\t%d\n", seqno);
	fprintf(stderr, "ACKNO:\t\t%d\n", ackno);
	fprintf(stderr, "----------------------------------------\n");
	fprintf(stderr, "TIME:\t\t%f\n", (double)(ts.tv_sec * 1000000.0 + ts.tv_usec) / 1000000.0);
	fprintf(stderr, "SEC:\t\t%ld\n", ts.tv_sec);
	fprintf(stderr, "USEC:\t\t%ld\n", ts.tv_usec);
	fprintf(stderr, "----------------------------------------\n");
	fprintf(stderr, "LENGTH:\t\t%d\n", length);
	fprintf(stderr, "CHECKSUM:\t%d\n", checksum);
	fprintf(stderr, "----------------------------------------\n");	
	fprintf(stderr, "PAYLOAD:\n");
	fprintf(stderr, "----------------------------------------\n");
	fprintf(stderr, "%s\n", payload);
	fprintf(stderr, "========================================\n");
}

//------------------------------------------------------------------------------
// Check the checksum
bool Packet::check() {
	return (computeChecksum(payload, length) == checksum);
}

//------------------------------------------------------------------------------
// Copy attribute data into the packet buffer
int Packet::loadHeader() {
	memset(buffer, 0, MSS);
	int i = 0;

	memcpy(&(buffer[i]), &src_addr, sizeof(src_addr));
	i += sizeof(src_addr);
	memcpy(&(buffer[i]), &src_port, sizeof(src_port));
	i += sizeof(src_port);

	memcpy(&(buffer[i]), &dst_addr, sizeof(dst_addr));
	i += sizeof(dst_addr);
	memcpy(&(buffer[i]), &dst_port, sizeof(dst_port));
	i += sizeof(dst_port);

	memcpy(&(buffer[i]), &syn, sizeof(syn));
	i += sizeof(syn);
	memcpy(&(buffer[i]), &ack, sizeof(ack));
	i += sizeof(ack);
	memcpy(&(buffer[i]), &fin, sizeof(fin));
	i += sizeof(fin);

	memcpy(&(buffer[i]), &seqno, sizeof(seqno));
	i += sizeof(seqno);
	memcpy(&(buffer[i]), &ackno, sizeof(ackno));
	i += sizeof(ackno);

	memcpy(&(buffer[i]), &ts, sizeof(ts));
	i += sizeof(ts);
	
	memcpy(&(buffer[i]), &length, sizeof(length));
	i += sizeof(length);
	memcpy(&(buffer[i]), &checksum, sizeof(checksum));
	i += sizeof(checksum);
	
	memcpy(&(buffer[i]), payload, MSS - HEADER_SIZE);
	
	
	return i;
}

//------------------------------------------------------------------------------
// Copy attribute data out of the packet buffer
int Packet::unloadHeader() {
	int i = 0;

	memcpy(&src_addr, &(buffer[i]), sizeof(src_addr));
	i += sizeof(src_addr);
	memcpy(&src_port, &(buffer[i]), sizeof(src_port));
	i += sizeof(src_port);

	memcpy(&dst_addr, &(buffer[i]), sizeof(dst_addr));
	i += sizeof(dst_addr);
	memcpy(&dst_port, &(buffer[i]), sizeof(dst_port));
	i += sizeof(dst_port);

	memcpy(&syn, &(buffer[i]), sizeof(syn));
	i += sizeof(syn);
	memcpy(&ack, &(buffer[i]), sizeof(ack));
	i += sizeof(ack);
	memcpy(&fin, &(buffer[i]), sizeof(fin));
	i += sizeof(fin);

	memcpy(&seqno, &(buffer[i]), sizeof(seqno));
	i += sizeof(seqno);
	memcpy(&ackno, &(buffer[i]), sizeof(ackno));
	i += sizeof(ackno);
	
	memcpy(&ts, &(buffer[i]), sizeof(ts));
	i += sizeof(ts);
	
	memcpy(&length, &(buffer[i]), sizeof(length));
	i += sizeof(length);
	memcpy(&checksum, &(buffer[i]), sizeof(checksum));
	i += sizeof(checksum);

	memcpy(payload, &(buffer[i]), MSS - HEADER_SIZE);	
	
	return i;
}

//------------------------------------------------------------------------------
// Get the checksum
unsigned int Packet::computeChecksum(char *buf, int len) {
	if (buf == NULL) return 0;
    
	unsigned int result = 7;
	int i;
	for (i = 0; i < len; i++) {
		result = result*31 + (unsigned int)(buf[i]);
	}
	return result;
}


