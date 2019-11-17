CC	 = gcc
CFLAGS	 = -g
CPPFLAGS = -Wall -pedantic

ARFLAGS  = rvU

.PHONEY: all clean

all: osh

osh: osh.o

clean: 
	rm -f *.o *.a osh
