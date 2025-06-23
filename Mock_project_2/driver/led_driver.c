#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/version.h>

#define DEVICE_NAME "gpio_led"
#define CLASS_NAME "led_class"

#define LED_IOC_MAGIC 'l'
#define LED_IOC_ON     _IO(LED_IOC_MAGIC, 1)
#define LED_IOC_OFF    _IO(LED_IOC_MAGIC, 2)
#define LED_IOC_TOGGLE _IO(LED_IOC_MAGIC, 3)
#define LED_IOC_STATUS _IOR(LED_IOC_MAGIC, 4, int)

static dev_t dev_number;
static struct class* led_class = NULL;
static struct device* led_device = NULL;
static struct cdev led_cdev;
static struct gpio_desc *led_gpio = NULL;
static bool led_status = false;

struct led_data {
    struct gpio_desc *led_gpio;
    struct device *dev;
    bool status;
};

static struct led_data *led_driver_data = NULL;

static void led_set(bool on) {
    gpiod_set_value(led_gpio, on ? 1 : 0);
    led_status = on;
}

// --- File operations ---
static int led_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "LED_DRV: Device opened\n");
    return 0;
}

static int led_release(struct inode *inode, struct file *file) {
    printk(KERN_INFO "LED_DRV: Device closed\n");
    return 0;
}

static ssize_t led_read(struct file *file, char __user *buffer, size_t len, loff_t *offset) {
    char msg[32];
    int msg_len;

    if (*offset > 0) return 0;

    msg_len = snprintf(msg, sizeof(msg), "LED: %s\n", led_status ? "ON" : "OFF");
    if (len < msg_len) return -EINVAL;

    if (copy_to_user(buffer, msg, msg_len)) return -EFAULT;

    *offset = msg_len;
    return msg_len;
}

static ssize_t led_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset) {
    char command[16];

    if (len >= sizeof(command)) return -EINVAL;
    if (copy_from_user(command, buffer, len)) return -EFAULT;

    command[len] = '\0';
    if (len > 0 && command[len - 1] == '\n') command[len - 1] = '\0';

    if (strcmp(command, "1") == 0 || strcmp(command, "on") == 0) {
        led_set(true);
        printk(KERN_INFO "LED_DRV: LED turned ON\n");
    } else if (strcmp(command, "0") == 0 || strcmp(command, "off") == 0) {
        led_set(false);
        printk(KERN_INFO "LED_DRV: LED turned OFF\n");
    } else if (strcmp(command, "toggle") == 0) {
        led_set(!led_status);
        printk(KERN_INFO "LED_DRV: LED toggled to %s\n", led_status ? "ON" : "OFF");
    } else {
        printk(KERN_WARNING "LED_DRV: Invalid command. Use '1', '0', 'on', 'off', or 'toggle'\n");
        return -EINVAL;
    }

    return len;
}

static long led_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    int status;

    switch (cmd) {
        case LED_IOC_ON:
            led_set(true);
            printk(KERN_INFO "LED_DRV: LED turned ON via IOCTL\n");
            break;
        case LED_IOC_OFF:
            led_set(false);
            printk(KERN_INFO "LED_DRV: LED turned OFF via IOCTL\n");
            break;
        case LED_IOC_TOGGLE:
            led_set(!led_status);
            printk(KERN_INFO "LED_DRV: LED toggled via IOCTL\n");
            break;
        case LED_IOC_STATUS:
            status = led_status ? 1 : 0;
            if (copy_to_user((int*)arg, &status, sizeof(int))) return -EFAULT;
            break;
        default:
            return -EINVAL;
    }

    return 0;
}

static struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .write = led_write,
    .release = led_release,
    .unlocked_ioctl = led_ioctl,
};

// --- Character device setup ---
static int setup_led_char_device(struct device *parent_dev) {
    int result;

    result = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (result < 0) return result;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,4,0)
    led_class = class_create(CLASS_NAME);
#else
    led_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(led_class)) {
        unregister_chrdev_region(dev_number, 1);
        return PTR_ERR(led_class);
    }

    led_device = device_create(led_class, parent_dev, dev_number, NULL, DEVICE_NAME);
    if (IS_ERR(led_device)) {
        class_destroy(led_class);
        unregister_chrdev_region(dev_number, 1);
        return PTR_ERR(led_device);
    }

    cdev_init(&led_cdev, &led_fops);
    led_cdev.owner = THIS_MODULE;
    result = cdev_add(&led_cdev, dev_number, 1);
    if (result < 0) {
        device_destroy(led_class, dev_number);
        class_destroy(led_class);
        unregister_chrdev_region(dev_number, 1);
        return result;
    }

    printk(KERN_INFO "LED_DRV: /dev/%s created (major: %d)\n", DEVICE_NAME, MAJOR(dev_number));
    return 0;
}

static void cleanup_led_char_device(void) {
    cdev_del(&led_cdev);
    device_destroy(led_class, dev_number);
    class_destroy(led_class);
    unregister_chrdev_region(dev_number, 1);
    printk(KERN_INFO "LED_DRV: Character device cleaned up\n");
}

// --- Platform driver ---
static int led_probe(struct platform_device *pdev) {
    int result;
    struct device *dev = &pdev->dev;

    printk(KERN_INFO "LED_DRV: Platform device probed\n");

    led_driver_data = devm_kzalloc(dev, sizeof(*led_driver_data), GFP_KERNEL);
    if (!led_driver_data) return -ENOMEM;

    led_driver_data->dev = dev;
    led_driver_data->status = false;
    platform_set_drvdata(pdev, led_driver_data);

    led_gpio = devm_gpiod_get(dev, "led", GPIOD_OUT_LOW);
    if (IS_ERR(led_gpio)) {
        dev_err(dev, "Failed to get LED GPIO from device tree\n");
        return PTR_ERR(led_gpio);
    }

    led_driver_data->led_gpio = led_gpio;

    result = setup_led_char_device(dev);
    if (result) return result;

    dev_info(dev, "LED driver initialized (Device Tree mode)\n");
    return 0;
}

static void led_remove(struct platform_device *pdev) {
    if (led_gpio)
        gpiod_set_value(led_gpio, 0);

    cleanup_led_char_device();
}

// --- Device tree match ---
static const struct of_device_id led_of_match[] = {
    { .compatible = "custom,gpio-led", },
    { }
};
MODULE_DEVICE_TABLE(of, led_of_match);

static struct platform_driver led_driver = {
    .probe = led_probe,
    .remove = led_remove,
    .driver = {
        .name = "gpio-led",
        .of_match_table = led_of_match,
    },
};

// --- Module entry points ---
static int __init led_init(void) {
    printk(KERN_INFO "LED_DRV: Initializing driver\n");
    return platform_driver_register(&led_driver);
}

static void __exit led_exit(void) {
    printk(KERN_INFO "LED_DRV: Exiting driver\n");
    platform_driver_unregister(&led_driver);
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AnhPh58");
MODULE_DESCRIPTION("GPIO LED Control Driver (Device Tree only)");
MODULE_VERSION("1.0");
MODULE_ALIAS("platform:gpio-led");
