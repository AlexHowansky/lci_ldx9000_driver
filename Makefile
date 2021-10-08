obj-m += usblcpd.o

all: usblcpd.ko testlcpd

testlcpd: testlcpd.c
	gcc -o testlcpd testlcpd.c

usblcpd.ko: usblcpd.c
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M="$(shell pwd)"
	strip --strip-unneeded usblcpd.ko

install: usblcpd.ko
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M="$(shell pwd)" modules_install

clean:
	rm -f testlcpd
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M="$(shell pwd)" clean
