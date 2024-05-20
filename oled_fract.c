// Draws Mandelbrot fractal on sh1106 OLED display via I2C

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <math.h>

#define I2C_DEVICE "/dev/i2c-1"
#define OLED_ADDR 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

void oled_write_command(int i2c_fd, uint8_t cmd) {
    uint8_t buffer[2];
    buffer[0] = 0x00; // Co = 0, D/C# = 0
    buffer[1] = cmd;

    if (write(i2c_fd, buffer, 2) != 2) {
        perror("Failed to write command");
    }
}

void oled_write_data(int i2c_fd, uint8_t* data, size_t size) {
    uint8_t* buffer = malloc(size + 1);
    if (!buffer) {
        perror("Failed to allocate memory");
        return;
    }

    buffer[0] = 0x40; // Co = 0, D/C# = 1
    memcpy(buffer + 1, data, size);

    if (write(i2c_fd, buffer, size + 1) != size + 1) {
        perror("Failed to write data");
    }

    free(buffer);
}

void oled_init(int i2c_fd) {
    oled_write_command(i2c_fd, 0xAE); // Display OFF

    // Set display clock divide ratio/oscillator frequency
    oled_write_command(i2c_fd, 0xD5);
    oled_write_command(i2c_fd, 0x80);

    // Set multiplex ratio (1 to 64)
    oled_write_command(i2c_fd, 0xA8);
    oled_write_command(i2c_fd, OLED_HEIGHT - 1);

    // Set display offset
    oled_write_command(i2c_fd, 0xD3);
    oled_write_command(i2c_fd, 0x00);

    // Set start line address
    oled_write_command(i2c_fd, 0x40);

    // Set charge pump
    oled_write_command(i2c_fd, 0x8D);
    oled_write_command(i2c_fd, 0x14);

    // Set memory addressing mode
    oled_write_command(i2c_fd, 0x20);
    oled_write_command(i2c_fd, 0x00);

    // Set segment re-map
    oled_write_command(i2c_fd, 0xA1);

    // Set COM output scan direction
    oled_write_command(i2c_fd, 0xC8);

    // Set COM pins hardware configuration
    oled_write_command(i2c_fd, 0xDA);
    oled_write_command(i2c_fd, 0x12);

    // Set contrast control
    oled_write_command(i2c_fd, 0x81);
    oled_write_command(i2c_fd, 0xCF);

    // Set pre-charge period
    oled_write_command(i2c_fd, 0xD9);
    oled_write_command(i2c_fd, 0xF1);

    // Set VCOMH deselect level
    oled_write_command(i2c_fd, 0xDB);
    oled_write_command(i2c_fd, 0x40);

    // Enable entire display on
    oled_write_command(i2c_fd, 0xA4);

    // Set normal display
    oled_write_command(i2c_fd, 0xA6);

    // Turn display on
    oled_write_command(i2c_fd, 0xAF);
}

void oled_display_image(int i2c_fd, uint8_t* image) {
    for (uint8_t page = 0; page < 8; page++) {
        oled_write_command(i2c_fd, 0xB0 + page);
        oled_write_command(i2c_fd, 0x00);
        oled_write_command(i2c_fd, 0x10);
        oled_write_data(i2c_fd, image + (OLED_WIDTH * page), OLED_WIDTH);
    }
}

void generate_mandelbrot(uint8_t* image) {
    const int max_iter = 1000;
    for (int y = 0; y < OLED_HEIGHT; y++) {
        for (int x = 0; x < OLED_WIDTH; x++) {
            double cr = (x - OLED_WIDTH / 2.0) * 4.0 / OLED_WIDTH;
            double ci = (y - OLED_HEIGHT / 2.0) * 4.0 / OLED_WIDTH;
            double zr = 0.0, zi = 0.0;
            int iter = 0;
            while (zr * zr + zi * zi < 4.0 && iter < max_iter) {
                double tmp = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = tmp;
                iter++;
            }
            if (iter < max_iter) {
                image[y / 8 * OLED_WIDTH + x] |= (1 << (y % 8));
            }
        }
    }
}

int main() {
    int i2c_fd;
    uint8_t image[OLED_WIDTH * OLED_HEIGHT / 8] = {0};

    generate_mandelbrot(image);

    if ((i2c_fd = open(I2C_DEVICE, O_RDWR)) < 0) {
        perror("Failed to open the i2c bus");
        return 1;
    }

    if (ioctl(i2c_fd, I2C_SLAVE, OLED_ADDR) < 0) {
        perror("Failed to acquire bus access and/or talk to slave");
        close(i2c_fd);
        return 1;
    }

    oled_init(i2c_fd);
    oled_display_image(i2c_fd, image);

    close(i2c_fd);
    return 0;
}