import numpy as np
import time
from PIL import Image, ImageEnhance
from luma.core.interface.serial import i2c
from luma.oled.device import sh1106

def capture_framebuffer(width, height):
    buffer_size = width * height * 2  # 2 bytes per pixel
    with open("/dev/fb0", "rb") as f:
        fb_data = f.read(buffer_size)
    return fb_data

def display_framebuffer(device, framebuffer_data, width, height):
    # RGB565 to RGB888
    fb_data = np.frombuffer(framebuffer_data, dtype=np.uint16).byteswap()
    r = ((fb_data & 0xF800) >> 8).astype(np.uint8)
    g = ((fb_data & 0x07E0) >> 3).astype(np.uint8)
    b = ((fb_data & 0x001F) << 3).astype(np.uint8)
    rgb_data = np.stack([r, g, b], axis=-1).reshape(height, width, 3).astype(np.uint8)
    image = Image.fromarray(rgb_data)
    image = image.resize(device.size, Image.LANCZOS)
    sharpener = ImageEnhance.Sharpness(image)
    image = sharpener.enhance(2.0)
    image = image.convert("1", dither=Image.FLOYDSTEINBERG)
    device.display(image)

def main():
    serial = i2c(port=1, address=0x3C)
    device = sh1106(serial)
    framebuffer_width, framebuffer_height = 1280, 720

    try:
        while True:
            fb_data = capture_framebuffer(framebuffer_width, framebuffer_height)
            display_framebuffer(device, fb_data, framebuffer_width, framebuffer_height)
            time.sleep(0.05)
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()
