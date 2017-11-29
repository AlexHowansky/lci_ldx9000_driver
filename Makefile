KERNELVERSION = `uname -r`
KERNELSRC := /lib/modules/$(KERNELVERSION)/build

obj-m += usblcpd.o

all: usblcpd.ko testlcpd

testlcpd: testlcpd.c
	gcc -o testlcpd testlcpd.c

usblcpd.ko: usblcpd.c
	$(MAKE) -I /usr/include -C $(KERNELSRC) SUBDIRS="$(PWD)" modules
	strip --strip-unneeded usblcpd.ko

clean:
	rm -f *.o *.ko *.mod.c .*.cmd Module.symvers modules.order testlcpd
	rm -rf .tmp_versions

install:
	mkdir -p /lib/modules/$(KERNELVERSION)/extra/drivers/usb/misc
	cp -v usblcpd.ko /lib/modules/$(KERNELVERSION)/extra/drivers/usb/misc
	depmod -a
	modprobe usblcpd
