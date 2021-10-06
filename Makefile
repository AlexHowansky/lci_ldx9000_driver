obj-m += usblcpd.o

all: usblcpd.ko testlcpd

testlcpd: testlcpd.c
	gcc -o testlcpd testlcpd.c

usblcpd.ko: usblcpd.c
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M="$(PWD)"
	strip --strip-unneeded usblcpd.ko

clean:
	rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order testlcpd

install: all
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M="$(PWD)" modules_install
