#include <sys/time.h>

#define MSS 1500
#define HEADER_SIZE 100

#define SEC 		1000000

/*
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

int timedif(struct timeval *start, struct timeval *end) {
	return SEC * ( end->tv_sec - start->tv_sec ) + ( end->tv_usec - start->tv_usec );
}

double timedif_sec(struct timeval *start, struct timeval *end) {
	return (double)( end->tv_sec - start->tv_sec ) + (double)( end->tv_usec - start->tv_usec ) / (double)SEC;
}
*/
class Packet {
private:
	unsigned long src_addr;
	unsigned short src_port;
	unsigned long dst_addr;
	unsigned short dst_port;

	bool syn;
	bool ack;
	bool fin;

	unsigned int seqno;
	unsigned int ackno;

	struct timeval ts;

	unsigned int length;
	unsigned int checksum;
	unsigned int recv_length;

	char payload[MSS - HEADER_SIZE];
	char buffer[MSS];

	bool timeout;

	int loadHeader();
	int unloadHeader();

	unsigned int computeChecksum(char *buf, int len);
public:
	Packet(unsigned long _src_addr, unsigned short _src_port,
		unsigned long _dst_addr, unsigned short _dst_port,
		bool _syn, bool _ack,
		unsigned int _seqno, unsigned int _ackno,
		char *buf, unsigned int _len);

	Packet(int fd);
	int sendPacket(int fd, struct sockaddr *addr, int len);
	void print();
	bool check();

	void setFin(bool f) { fin = f; }
	void setSeqno(unsigned int s) { seqno = s; }
	void setAckno(unsigned int a) { ackno = a; }
	void setTimestamp(struct timeval t) { ts = t; }

	unsigned long getSrcAddr() { return src_addr; }
	unsigned short getSrcPort() { return src_port; }
	unsigned long getDstAddr() { return dst_addr; }
	unsigned short getDstPort() { return dst_port; }
	bool isSyn() { return syn; }
	bool isAck() { return ack; }
	bool isFin() { return fin; }
	unsigned int getSeqno() { return seqno; }
	unsigned int getAckno() { return ackno; }
	struct timeval getTimestamp() { return ts; }
	unsigned int getLength() { return length; }
	char* getPayload() { return payload; }
	bool getTimeout() { return timeout; }

};
