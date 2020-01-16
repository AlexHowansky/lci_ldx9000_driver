# Synopsis

This is a slightly modified version of the Linux driver for the LCI/Bematech LDX9000 series display pole. The drivers
offered on the [manufacturer's site](http://bematechus.com/support/product-drivers/) are quite old and do not compile
in modern Linux versions. This repository represents a minimal update to make the driver work with kernel version 4.

# Compilation

You will need a C compiler, `make`, and perhaps `flex` and `bison` installed. On Debian-based systems, this can typically be done with a command like:

```sh
apt-get install build-essential bison flex
```

To compile and install the driver:

```sh
make
sudo make install
```

This should load the driver and create a `/dev/lcpd` device file, which your code can interface with to manipulate the
display. The `testlcpd` program may be run to perform some simple tests.

# Disclaimer

I don't know a damn thing about writing kernel drivers. I merely made what appeared to be sensible changes based on
documentation found online regarding kernel updates from v2 to v4. They have worked ok for me, on various versions of
Linux from Ubuntu 14.04 to to Fedora 29. Your mileage may vary. These changes were offered to LCI/Bematech, but no
response was received. As a result, I have made this package available to the public here.
