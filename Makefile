#
# Makefile for the linux MBS Monitor routines.
#

obj-m += mntr.o


all:
	make -C /lib/modules/$(shell uname -r)/build M=`pwd`

clean:
	make -C /lib/modules/$(shell uname -r)/build M=`pwd` clean
