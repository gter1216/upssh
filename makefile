# type tab before command
# xuxiao, 2019.12.23

all : upssh upsshd

upssh : upssh.o  misc.o xmalloc.o fatal.o
	gcc -o upssh upssh.o misc.o fatal.o xmalloc.o 

upsshd : upsshd.o misc.o xmalloc.o fatal.o
	gcc -o upsshd upsshd.o misc.o fatal.o xmalloc.o  -lpam -lpam_misc

upsshd.o : upsshd.c
	gcc -c upsshd.c

upssh.o : upssh.c
	gcc -c upssh.c

misc.o : misc.c
	gcc -c misc.c

xmalloc.o : xmalloc.c
	gcc -c xmalloc.c

fatal.o :  fatal.c 
	gcc -c fatal.c

clean :
	rm upssh upsshd  upsshd.o upssh.o xmalloc.o misc.o fatal.o


