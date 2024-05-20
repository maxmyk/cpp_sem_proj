#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define DRIVER_NAME "sh1106_fb"
#define I2C_BUS_AVAILABLE   1
#define SLAVE_DEVICE_NAME   "SH1106_OLED"
#define OLED_I2C_ADDRESS    0x3C       // Address of a slave device

static struct i2c_adapter *sh1106_i2c_adapter = NULL;     // I2C Adapter Structure
static struct i2c_client  *sh1106_i2c_client_oled = NULL; // I2C Client Structure (In our case it is OLED)

static struct fb_info *sh1106_fb_info;

static int sh1106_write_command(uint8_t cmd) {
    int ret;
    uint8_t buffer[2] = {0x00, cmd};  // Control byte 0x00 for command
    struct i2c_msg msg = {
        .addr = OLED_I2C_ADDRESS,
        .flags = 0,
        .len = sizeof(buffer),
        .buf = buffer,
    };
    ret = i2c_transfer(sh1106_i2c_client_oled->adapter, &msg, 1);
    if (ret != 1)
        printk(KERN_ERR "SH1106: Failed to write command 0x%02x\n", cmd);
    return (ret == 1) ? 0 : -EIO;
}

static int sh1106_write_data(uint8_t *data, size_t size) {
    int ret;
    uint8_t *buffer = kmalloc(size + 1, GFP_KERNEL);
    if (!buffer)
        return -ENOMEM;

    buffer[0] = 0x40;  // Control byte 0x40 for data
    memcpy(&buffer[1], data, size);
    struct i2c_msg msg = {
        .addr = OLED_I2C_ADDRESS,
        .flags = 0,
        .len = size + 1,
        .buf = buffer,
    };
    ret = i2c_transfer(sh1106_i2c_client_oled->adapter, &msg, 1);
    kfree(buffer);
    if (ret != 1)
        printk(KERN_ERR "SH1106: Failed to write data\n");
    return (ret == 1) ? 0 : -EIO;
}

static int sh1106_init(void) {
    printk(KERN_INFO "SH1106: Initializing display\n");
    // Initialization sequence for SH1106
    sh1106_write_command(0xAE); // Display OFF
    sh1106_write_command(0xD5); // Set Display Clock Divide Ratio
    sh1106_write_command(0x80); // Suggested ratio
    sh1106_write_command(0xA8); // Set Multiplex Ratio
    sh1106_write_command(0x3F);
    sh1106_write_command(0xD3); // Set Display Offset
    sh1106_write_command(0x00); // No offset
    sh1106_write_command(0x40); // Set Display Start Line
    sh1106_write_command(0x8D); // Charge Pump Setting
    sh1106_write_command(0x14); // Enable charge pump
    sh1106_write_command(0x20); // Set Memory Addressing Mode
    sh1106_write_command(0x00); // Horizontal addressing mode
    sh1106_write_command(0xA1); // Set Segment Re-map
    sh1106_write_command(0xC8); // Set COM Output Scan Direction
    sh1106_write_command(0xDA); // Set COM Pins Hardware Configuration
    sh1106_write_command(0x12);
    sh1106_write_command(0x81); // Set Contrast Control
    sh1106_write_command(0x7F);
    sh1106_write_command(0xD9); // Set Pre-charge Period
    sh1106_write_command(0xF1);
    sh1106_write_command(0xDB); // Set VCOMH Deselect Level
    sh1106_write_command(0x40);
    sh1106_write_command(0xA4); // Entire Display ON
    sh1106_write_command(0xA6); // Set Normal/Inverse Display
    sh1106_write_command(0xAF); // Display ON
    printk(KERN_INFO "SH1106: Display initialized\n");
    return 0;
}

static void update_display(void) {
    // Update display with framebuffer contents
    uint8_t *buffer = (uint8_t *)sh1106_fb_info->screen_base;
    for (int page = 0; page < 8; page++) {
        sh1106_write_command(0xB0 + page); // Set page address
        sh1106_write_command(0x00);        // Set lower column address
        sh1106_write_command(0x10);        // Set higher column address
        sh1106_write_data(buffer + page * 128, 128);
    }
    printk(KERN_INFO "SH1106: Display updated\n");
}

static int sh1106_fb_open(struct fb_info *info, int user) {
    printk(KERN_INFO "SH1106: Framebuffer open\n");
    return 0;
}

static int sh1106_fb_release(struct fb_info *info, int user) {
    printk(KERN_INFO "SH1106: Framebuffer release\n");
    return 0;
}



static ssize_t sh1106_fb_write(struct fb_info *info, const char __user *buf, size_t count, loff_t *ppos) {
    printk(KERN_INFO "SH1106: Writing to framebuffer\n");
    if (copy_from_user(info->screen_base + *ppos, buf, count)) {
        printk(KERN_ERR "SH1106: Failed to copy from user\n");
        return -EFAULT;
    }
    *ppos += count;
    update_display();
    return count;
}

static struct fb_ops sh1106_fb_ops = {
    .owner          = THIS_MODULE,
    .fb_open        = sh1106_fb_open,
    .fb_release     = sh1106_fb_release,
    .fb_write       = sh1106_fb_write,
};

static int __init sh1106_fb_init(void) {
    int ret;
    struct i2c_board_info board_info = {
        I2C_BOARD_INFO(SLAVE_DEVICE_NAME, OLED_I2C_ADDRESS)
    };

    sh1106_i2c_adapter = i2c_get_adapter(I2C_BUS_AVAILABLE);
    if (!sh1106_i2c_adapter) {
        printk(KERN_ERR "SH1106: Failed to get I2C adapter\n");
        return -ENODEV;
    }

    sh1106_i2c_client_oled = i2c_new_client_device(sh1106_i2c_adapter, &board_info);
    if (!sh1106_i2c_client_oled) {
        printk(KERN_ERR "SH1106: Failed to create I2C client\n");
        i2c_put_adapter(sh1106_i2c_adapter);
        return -ENODEV;
    }

    sh1106_fb_info = framebuffer_alloc(0, NULL);
    if (!sh1106_fb_info) {
        printk(KERN_ERR "SH1106: Failed to allocate framebuffer\n");
        i2c_unregister_device(sh1106_i2c_client_oled);
        i2c_put_adapter(sh1106_i2c_adapter);
        return -ENOMEM;
    }

    sh1106_fb_info->fbops = &sh1106_fb_ops;
    sh1106_fb_info->screen_base = kzalloc(1024, GFP_KERNEL);  // SH1106 has 1024 bytes (128x64 bits) framebuffer
    sh1106_fb_info->fix.smem_len = 1024;
    sh1106_fb_info->fix.line_length = 128;
    sh1106_fb_info->var.xres = 128;
    sh1106_fb_info->var.yres = 64;
    sh1106_fb_info->var.xres_virtual = 128;
    sh1106_fb_info->var.yres_virtual = 64;
    sh1106_fb_info->var.bits_per_pixel = 1;
    sh1106_fb_info->var.red.length = 1;
    sh1106_fb_info->var.red.offset = 0;
    sh1106_fb_info->var.green.length = 1;
    sh1106_fb_info->var.green.offset = 0;
    sh1106_fb_info->var.blue.length = 1;
    sh1106_fb_info->var.blue.offset = 0;
    sh1106_fb_info->var.transp.length = 0;
    sh1106_fb_info->var.transp.offset = 0;
    sh1106_fb_info->var.activate = FB_ACTIVATE_NOW;
    sh1106_fb_info->fix.type = FB_TYPE_PACKED_PIXELS;
    sh1106_fb_info->fix.visual = FB_VISUAL_MONO01;
    sh1106_fb_info->fix.line_length = 128 / 8; // 128 pixels / 8 bits per byte

    ret = register_framebuffer(sh1106_fb_info);
    if (ret < 0) {
        printk(KERN_ERR "SH1106: Failed to register framebuffer\n");
        kfree(sh1106_fb_info->screen_base);
        framebuffer_release(sh1106_fb_info);
        i2c_unregister_device(sh1106_i2c_client_oled);
        i2c_put_adapter(sh1106_i2c_adapter);
        return ret;
    }

    sh1106_init();
    printk(KERN_INFO "SH1106: Framebuffer driver initialized\n");
    return 0;
}

static void __exit sh1106_fb_exit(void) {
    unregister_framebuffer(sh1106_fb_info);
    kfree(sh1106_fb_info->screen_base);
    framebuffer_release(sh1106_fb_info);
    i2c_unregister_device(sh1106_i2c_client_oled);
    i2c_put_adapter(sh1106_i2c_adapter);
    printk(KERN_INFO "SH1106: Framebuffer driver exited\n");
}

module_init(sh1106_fb_init);
module_exit(sh1106_fb_exit);

MODULE_DESCRIPTION("SH1106 Framebuffer Driver");
MODULE_LICENSE("GPL");
