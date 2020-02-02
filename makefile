BINS=sender receiver
all: $(BINS)

receiver: receiver.cpp packet.o
	g++ -o receiver receiver.cpp packet.o

sender: sender.cpp packet.o
	g++ -o sender sender.cpp packet.o

packet.o: packet.cpp packet.h
	g++ -c packet.cpp

clean:
	rm -f *.o sender receiver
