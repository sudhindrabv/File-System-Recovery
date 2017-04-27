CFLAGS=-lcurses 
DEBUG=-g

all: damageFileSystemtool

damageFileSystemtool:	mydamage.c mylink.c myunlink.c 
	clang $(CFLAGS) $(DEBUG) mydamage.c mylink.c myunlink.c -o damageFileSystemtool
clean:
	rm -f damageFileSystemtool

