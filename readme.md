# C++ semester project

A simple sh1106 framebuffer driver for RPi.

## Main steps

### compiling and installing the driver

```
chmod +x install.sh
install.sh
```
or
```
sudo rm /lib/modules/$(uname -r)/sh1106_fbd.ko
make clean
make
sudo cp sh1106_fbd.ko /lib/modules/$(uname -r)/
sudo depmod
sudo modprobe sh1106_fbd
```

add to ```/etc/rc.local```

```
/sbin/modprobe sh1106_fbd
```

Test it

```
fbtest
/dev/fb0: res 128x64, virtual 128x64, line_len 16
```

```
echo "Hello, World!" > /dev/fb1 # will turn on some pixels
dd if=/dev/zero of=/dev/fb1 bs=128 count=64 # will make all pixels black
```

### How to use:

1. Install the driver
2. Write something to a framebuffer (located at /dev/fb*) and see it on the screen.

### Dependencies

1. build-essential
2. raspberrypi-kernel-headers

## Important!
This version uses 1bpp. It will be converted to 32bpp for compitability.