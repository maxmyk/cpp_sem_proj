#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#define DRIVER_NAME "sh1106_fb"
#define DRIVER_VERSION "1.0"
#define I2C_BUS_AVAILABLE 1
#define SLAVE_DEVICE_NAME "SH1106_OLED"
#define OLED_I2C_ADDRESS 0x3C

#define SH1106_XRES 128
#define SH1106_YRES 64
#define SH1106_BPP 1
#define SH1106_LINE_BYTES (SH1106_XRES / 8)
#define SH1106_FB_REAL_SIZE (SH1106_LINE_BYTES * SH1106_YRES)
#define SH1106_FB_MAP_SIZE PAGE_ALIGN(SH1106_FB_REAL_SIZE)

static struct delayed_work sh1106_refresh_work;
static u8 *sh1106_shadow;
static unsigned int sh1106_refresh_ms =
    50; // 20 Hz, should be adjustable, TODO.

static struct i2c_adapter *sh1106_i2c_adapter = NULL;
static struct i2c_client *sh1106_i2c_client_oled = NULL;

static struct fb_info *sh1106_fb_info;
static u8 sh1106_pagebuf[SH1106_FB_REAL_SIZE]; // 1024 bytes

static DEFINE_MUTEX(sh1106_lock);

static u8 *txbuf;

static int sh1106_write_command(u8 cmd) {
  int ret;
  u8 buffer[2] = {0x00, cmd}; // Control byte 0x00 for command
  struct i2c_msg msg = {
      .addr = OLED_I2C_ADDRESS,
      .flags = 0,
      .len = sizeof(buffer),
      .buf = buffer,
  };

  if (!sh1106_i2c_client_oled || !sh1106_i2c_client_oled->adapter) {
    return -ENODEV;
  }

  ret = i2c_transfer(sh1106_i2c_client_oled->adapter, &msg, 1);
  if (ret != 1) {
    printk(KERN_ERR DRIVER_NAME ": Failed to write command 0x%02x (ret=%d)\n", cmd,
           ret);
  }

  return (ret == 1) ? 0 : -EIO;
}

static int sh1106_write_data(const u8 *data, size_t size) {
  int ret;
  //   u8 *buffer;

  if (!sh1106_i2c_client_oled || !sh1106_i2c_client_oled->adapter) {
    return -ENODEV;
  }

  //   buffer = kmalloc(size + 1, GFP_KERNEL);
  if (!txbuf) {
    return -ENOMEM;
  }

  txbuf[0] = 0x40; // Control byte 0x40 for data
  memcpy(&txbuf[1], data, size);

  {
    struct i2c_msg msg = {
        .addr = OLED_I2C_ADDRESS,
        .flags = 0,
        .len = (u16)(size + 1),
        .buf = txbuf,
    };
    ret = i2c_transfer(sh1106_i2c_client_oled->adapter, &msg, 1);
  }

  //   kfree(buffer);

  if (ret != 1) {
    printk(KERN_ERR DRIVER_NAME ": Failed to write data (ret=%d)\n", ret);
  }

  return (ret == 1) ? 0 : -EIO;
}

static int sh1106_init_display(void) {
  printk(KERN_INFO DRIVER_NAME ": Initializing display\n");
  /* I don't reemember where I got this from.
     Probably from an existing library.
     Needs to be double-checked. */
  sh1106_write_command(0xAE);
  sh1106_write_command(0xD5);
  sh1106_write_command(0x80);
  sh1106_write_command(0xA8);
  sh1106_write_command(0x3F);
  sh1106_write_command(0xD3);
  sh1106_write_command(0x00);
  sh1106_write_command(0x40);
  sh1106_write_command(0x8D);
  sh1106_write_command(0x14);
  sh1106_write_command(0x20);
  sh1106_write_command(0x00);
  sh1106_write_command(0xA1);
  sh1106_write_command(0xC8);
  sh1106_write_command(0xDA);
  sh1106_write_command(0x12);
  sh1106_write_command(0x81);
  sh1106_write_command(0x7F);
  sh1106_write_command(0xD9);
  sh1106_write_command(0xF1);
  sh1106_write_command(0xDB);
  sh1106_write_command(0x40);
  sh1106_write_command(0xA4);
  sh1106_write_command(0xA6); // Set Normal/Inverse Display, 0xA7 - inverted
  sh1106_write_command(0xAF);
  printk(KERN_INFO DRIVER_NAME ": Display initialized\n");
  return 0;
}

static void update_display(void) {
  const u8 *fb;
  int x, y, page;

  if (!sh1106_fb_info || !sh1106_fb_info->screen_base) {
    return;
  }

  fb = (const u8 *)sh1106_fb_info->screen_base;

  memset(sh1106_pagebuf, 0, sizeof(sh1106_pagebuf));

  for (y = 0; y < SH1106_YRES; y++) {
    for (x = 0; x < SH1106_XRES; x++) {
      int fb_byte_index = y * SH1106_LINE_BYTES + (x >> 3);
      int fb_bit = (x & 7);
      int pixel_on = (fb[fb_byte_index] >> fb_bit) & 1;

      if (pixel_on) {
        page = y >> 3;
        sh1106_pagebuf[page * SH1106_XRES + x] |= (1u << (y & 7));
      }
    }
  }

  for (page = 0; page < 8; page++) {
    sh1106_write_command(0xB0 + page);
    sh1106_write_command(0x02);
    sh1106_write_command(0x10);

    sh1106_write_data(&sh1106_pagebuf[page * SH1106_XRES], SH1106_XRES);
  }
}

static int sh1106_fb_open(struct fb_info *info, int user) {
  printk(KERN_INFO DRIVER_NAME ": Open\n");
  return 0;
}

static int sh1106_fb_release(struct fb_info *info, int user) {
  printk(KERN_INFO DRIVER_NAME ": Release\n");
  return 0;
}

static ssize_t sh1106_fb_write(struct fb_info *info, const char __user *buf,
                               size_t count, loff_t *ppos) {
  mutex_lock(&sh1106_lock);
  // printk(KERN_INFO DRIVER_NAME ": fb_write called (count=%zu, ppos=%lld)\n", count,
  // *ppos);
  ssize_t ret;
  size_t max;

  // printk(KERN_INFO DRIVER_NAME ": Writing to framebuffer\n");

  if (*ppos < 0) {
    ret = -EINVAL;
    goto out;
  }
  if (*ppos >= SH1106_FB_REAL_SIZE) {
    ret = -ENOSPC;
    goto out;
  }

  max = SH1106_FB_REAL_SIZE - (size_t)*ppos;
  if (count > max) {
    count = max;
  }

  if (copy_from_user((u8 *)info->screen_base + *ppos, buf, count)) {
    ret = -EFAULT;
    goto out;
  }

  *ppos += count;
  update_display();
  mutex_unlock(&sh1106_lock);
  ret = count;

out:
  mutex_unlock(&sh1106_lock);
  return ret;
}

static int sh1106_fb_mmap(struct fb_info *info, struct vm_area_struct *vma) {
  // TODO: add lock here to avoid surprises in the future.
  unsigned long size = vma->vm_end - vma->vm_start;
  unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
  unsigned long pos;
  void *base;

  printk(KERN_INFO DRIVER_NAME ": fb_mmap called (size=%lu, offset=%lu)\n", size,
         offset);

  if (!info || !info->screen_base) {
    return -ENODEV;
  }

  if (offset + size > info->fix.smem_len) {
    return -EINVAL;
  }

  base = (void *)((unsigned long)info->screen_base + offset);

  for (pos = 0; pos < size; pos += PAGE_SIZE) {
    unsigned long pfn = vmalloc_to_pfn(base + pos);
    int ret = remap_pfn_range(vma, vma->vm_start + pos, pfn, PAGE_SIZE,
                              vma->vm_page_prot);
    if (ret) {
      return ret;
    }
  }

  return 0;
}

static void sh1106_refresh_worker(struct work_struct *work) {
  mutex_lock(&sh1106_lock);
  bool changed = false;

  if (sh1106_fb_info && sh1106_fb_info->screen_base && sh1106_shadow) {
    if (memcmp(sh1106_shadow, sh1106_fb_info->screen_base,
               SH1106_FB_REAL_SIZE) != 0) {
      memcpy(sh1106_shadow, sh1106_fb_info->screen_base, SH1106_FB_REAL_SIZE);
      changed = true;
    }
  }

  if (changed) {
    update_display();
  }
  mutex_unlock(&sh1106_lock);

  schedule_delayed_work(&sh1106_refresh_work,
                        msecs_to_jiffies(sh1106_refresh_ms));
}

static struct fb_ops sh1106_fb_ops = {
    .owner = THIS_MODULE,
    .fb_open = sh1106_fb_open,
    .fb_release = sh1106_fb_release,
    .fb_write = sh1106_fb_write,

    .fb_fillrect = cfb_fillrect,
    .fb_copyarea = cfb_copyarea,
    .fb_imageblit = cfb_imageblit,

    .fb_mmap = sh1106_fb_mmap,
};

static int __init sh1106_fb_init(void) {
  int ret;
  struct i2c_board_info board_info = {
      I2C_BOARD_INFO(SLAVE_DEVICE_NAME, OLED_I2C_ADDRESS)};

  sh1106_i2c_adapter = i2c_get_adapter(I2C_BUS_AVAILABLE);
  if (!sh1106_i2c_adapter) {
    printk(KERN_ERR DRIVER_NAME ": Failed to get I2C adapter %d\n",
           I2C_BUS_AVAILABLE);
    return -ENODEV;
  }

  sh1106_i2c_client_oled =
      i2c_new_client_device(sh1106_i2c_adapter, &board_info);
  if (IS_ERR(sh1106_i2c_client_oled)) {
    ret = PTR_ERR(sh1106_i2c_client_oled);
    sh1106_i2c_client_oled = NULL;
    printk(KERN_ERR DRIVER_NAME ": Failed to create I2C client (err=%d)\n", ret);
    i2c_put_adapter(sh1106_i2c_adapter);
    sh1106_i2c_adapter = NULL;
    return ret;
  }

  sh1106_fb_info = framebuffer_alloc(0, &sh1106_i2c_client_oled->dev);
  if (!sh1106_fb_info) {
    printk(KERN_ERR DRIVER_NAME ": Failed to allocate framebuffer\n");
    i2c_unregister_device(sh1106_i2c_client_oled);
    sh1106_i2c_client_oled = NULL;
    i2c_put_adapter(sh1106_i2c_adapter);
    sh1106_i2c_adapter = NULL;
    return -ENOMEM;
  }

  sh1106_fb_info->fbops = &sh1106_fb_ops;

  sh1106_fb_info->screen_base = vzalloc(SH1106_FB_MAP_SIZE);
  if (!sh1106_fb_info->screen_base) {
    printk(KERN_ERR DRIVER_NAME ": Failed to allocate screen_base\n");
    framebuffer_release(sh1106_fb_info);
    sh1106_fb_info = NULL;
    i2c_unregister_device(sh1106_i2c_client_oled);
    sh1106_i2c_client_oled = NULL;
    i2c_put_adapter(sh1106_i2c_adapter);
    sh1106_i2c_adapter = NULL;
    return -ENOMEM;
  }
  sh1106_fb_info->fix.smem_start = 0; // vmalloc

  txbuf = kmalloc(1 + SH1106_XRES, GFP_KERNEL);
  if (!txbuf) {
    printk(KERN_ERR DRIVER_NAME ": Failed to allocate txbuf\n");
    vfree(sh1106_fb_info->screen_base);
    sh1106_fb_info->screen_base = NULL;
    framebuffer_release(sh1106_fb_info);
    sh1106_fb_info = NULL;
    i2c_unregister_device(sh1106_i2c_client_oled);
    sh1106_i2c_client_oled = NULL;
    i2c_put_adapter(sh1106_i2c_adapter);
    sh1106_i2c_adapter = NULL;
    return -ENOMEM;
  }

  sh1106_shadow = kzalloc(SH1106_FB_REAL_SIZE, GFP_KERNEL);
  if (!sh1106_shadow) {
    printk(KERN_ERR DRIVER_NAME ": Failed to allocate shadow buffer\n");
    vfree(sh1106_fb_info->screen_base);
    framebuffer_release(sh1106_fb_info);
    i2c_unregister_device(sh1106_i2c_client_oled);
    i2c_put_adapter(sh1106_i2c_adapter);
    return -ENOMEM;
  }

  INIT_DELAYED_WORK(&sh1106_refresh_work, sh1106_refresh_worker);
  schedule_delayed_work(&sh1106_refresh_work,
                        msecs_to_jiffies(sh1106_refresh_ms));

  sh1106_fb_info->fix.smem_len = SH1106_FB_MAP_SIZE;
  sh1106_fb_info->screen_size = SH1106_FB_REAL_SIZE;

  sh1106_fb_info->fix.line_length = SH1106_LINE_BYTES;
  sh1106_fb_info->fix.type = FB_TYPE_PACKED_PIXELS;
  sh1106_fb_info->fix.visual = FB_VISUAL_MONO10; // or FB_VISUAL_MONO01

  sh1106_fb_info->var.xres = SH1106_XRES;
  sh1106_fb_info->var.yres = SH1106_YRES;
  sh1106_fb_info->var.xres_virtual = SH1106_XRES;
  sh1106_fb_info->var.yres_virtual = SH1106_YRES;
  sh1106_fb_info->var.bits_per_pixel = SH1106_BPP;

  sh1106_fb_info->var.red.length = 1;
  sh1106_fb_info->var.red.offset = 0;
  sh1106_fb_info->var.green.length = 1;
  sh1106_fb_info->var.green.offset = 0;
  sh1106_fb_info->var.blue.length = 1;
  sh1106_fb_info->var.blue.offset = 0;
  sh1106_fb_info->var.transp.length = 0;
  sh1106_fb_info->var.transp.offset = 0;
  sh1106_fb_info->var.activate = FB_ACTIVATE_NOW;

  ret = register_framebuffer(sh1106_fb_info);
  if (ret < 0) {
    printk(KERN_ERR DRIVER_NAME ": Failed to register framebuffer (err=%d)\n", ret);
    vfree(sh1106_fb_info->screen_base);
    sh1106_fb_info->screen_base = NULL;
    framebuffer_release(sh1106_fb_info);
    sh1106_fb_info = NULL;
    i2c_unregister_device(sh1106_i2c_client_oled);
    sh1106_i2c_client_oled = NULL;
    i2c_put_adapter(sh1106_i2c_adapter);
    sh1106_i2c_adapter = NULL;
    return ret;
  }

  sh1106_init_display();
  printk(KERN_INFO DRIVER_NAME ": Initialized\n");
  return 0;
}

static void __exit sh1106_fb_exit(void) {
  if (sh1106_fb_info) {
    unregister_framebuffer(sh1106_fb_info);

    cancel_delayed_work_sync(&sh1106_refresh_work);
    kfree(sh1106_shadow);
    sh1106_shadow = NULL;

    if (sh1106_fb_info->screen_base) {
      vfree(sh1106_fb_info->screen_base);
      sh1106_fb_info->screen_base = NULL;
    }

    kfree(txbuf);

    framebuffer_release(sh1106_fb_info);
    sh1106_fb_info = NULL;
  }

  if (sh1106_i2c_client_oled) {
    i2c_unregister_device(sh1106_i2c_client_oled);
    sh1106_i2c_client_oled = NULL;
  }

  if (sh1106_i2c_adapter) {
    i2c_put_adapter(sh1106_i2c_adapter);
    sh1106_i2c_adapter = NULL;
  }

  printk(KERN_INFO DRIVER_NAME ": Exited\n");
}

module_init(sh1106_fb_init);
module_exit(sh1106_fb_exit);
MODULE_DESCRIPTION(DRIVER_NAME " Framebuffer Driver v" DRIVER_VERSION);
MODULE_AUTHOR("maxmyk");
MODULE_LICENSE("GPL");
