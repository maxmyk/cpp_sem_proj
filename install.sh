echo 0 | sudo tee /sys/class/vtconsole/vtcon1/bind
sudo rmmod sh1106_fbd

sudo rm /lib/modules/$(uname -r)/sh1106_fbd.ko
make clean

make
sudo cp sh1106_fbd.ko /lib/modules/$(uname -r)/
sudo depmod
sudo modprobe sh1106_fbd