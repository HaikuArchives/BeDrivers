CC = gcc
LD = gcc -nostdlib -Xlinker -x -Xlinker -s
OPT = -mcpu=k6 -O3 -fexpensive-optimizations
CFLAGS = -Wall -I. -no-fpic $(OPT)

HEADERS = audio.h audio_driver.h midi_driver.h sound.h adi.h
OBJS = driver.o pcm.o register.o mix.o mux.o

all: ad1816

ad1816: $(OBJS)
	$(LD) -o $@ $(OBJS) /boot/develop/lib/x86/_KERNEL_
	mimeset -f $@

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

driver.o: driver.c $(HEADERS) makefile
pcm.o: pcm.c $(HEADERS) makefile
register.o: register.c $(HEADERS) makefile
mix.o: mix.c $(HEADERS) makefile
mux.o: mux.c $(HEADERS) makefile
