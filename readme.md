# C++ semester project

Using C and Python (for now) to use OLED screen as a primary display. In the future it'll be a complete driver.

## Main steps

### compiling drm2fb
```
gcc -o drm2fb drm2fb.c -I/usr/include/drm -ldrm
```

### [VFB driver](https://elixir.bootlin.com/linux/v3.12.43/source/drivers/video/vfb.c) compilation

```
make
```

Installing the driver

```
sudo insmod vfb.ko
```

```
FB0: resolution 1280x720, 16 bpp
DRM FB: resolution 1280x720, pitch 5120, depth 24
```

FB copy test
```
cat /dev/fb0 > fbdump.data
ffmpeg -vcodec rawvideo -f rawvideo -pix_fmt rgb565 -s 1280x720 -i fbdump.data -f image2 -vcodec png screenshot.png
```

### How to use:

1. Run drm2fb
2. Run fb2disp.py

### Dependencies

1. luma python environment
2. libdrm-dev
3. build-essential