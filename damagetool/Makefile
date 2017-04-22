CFLAGS=-lcurses 
DEBUG=-g

all: dfstool

dfstool:	mydamage.c mylink.c myunlink.c 
	clang $(CFLAGS) $(DEBUG) mydamage.c mylink.c myunlink.c -o dfstool 
clean:
	rm -f dfstool

