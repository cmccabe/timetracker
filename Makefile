CFLAGS=-Wall -O2
LDFLAGS=-lcurses

all:		timetracker

timetracker:	timetracker.o

clean:
	rm -rf timetracker *.o
