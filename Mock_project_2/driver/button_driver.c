#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>

#define DEVICE_NAME "gpio_button"
#define CLASS_NAME "button_class"

#define BUTTON_IOC_MAGIC 'b'
#define BUTTON_IOC_GET_STATUS _IOR(BUTTON_IOC_MAGIC, 1, int)

static dev_t dev_number;
static struct class* button_class = NULL;
static struct device* button_device = NULL;
static struct cdev button_cdev;
static struct gpio_desc *button_gpio = NULL;
static int button_irq;

struct button_data {
    struct gpio_desc *button_gpio;
    struct device *dev;
    int irq;
};

static struct button_data *button_driver_data = NULL;

static int button_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "BUTTON_DRV: Device opened\n");
    return 0;
}

static int button_release(struct inode *inode, struct file *file) {
    printk(KERN_INFO "BUTTON_DRV: Device closed\n");
    return 0;
}

static ssize_t button_read(struct file *file, char __user *buffer, size_t len, loff_t *offset) {
    int button_state;
    char msg[32];
    int msg_len;

    if (*offset > 0) return 0;

    button_state = gpiod_get_value(button_gpio);
    msg_len = snprintf(msg, sizeof(msg), "Button: %s\n", button_state ? "PRESSED" : "RELEASED");

    if (len < msg_len) return -EINVAL;
    if (copy_to_user(buffer, msg, msg_len)) return -EFAULT;

    *offset = msg_len;
    return msg_len;
}

static long button_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    int button_state;

    if (cmd == BUTTON_IOC_GET_STATUS) {
        button_state = gpiod_get_value(button_gpio);
        if (copy_to_user((int*)arg, &button_state, sizeof(int))) return -EFAULT;
        return 0;
    }

    return -EINVAL;
}

static struct file_operations button_fops = {
    .owner = THIS_MODULE,
    .open = button_open,
    .read = button_read,
    .release = button_release,
    .unlocked_ioctl = button_ioctl,
};

static irqreturn_t button_irq_handler(int irq, void *dev_id) {
    static unsigned long last_interrupt = 0;
    unsigned long interrupt_time = jiffies;

    if (interrupt_time - last_interrupt < msecs_to_jiffies(200))
        return IRQ_HANDLED;
    last_interrupt = interrupt_time;

    printk(KERN_INFO "BUTTON_DRV: Button pressed!\n");
    return IRQ_HANDLED;
}

static int setup_button_char_device(struct device *parent_dev) {
    int result;

    result = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (result < 0) return result;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,4,0)
    button_class = class_create(CLASS_NAME);
#else
    button_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(button_class)) {
        unregister_chrdev_region(dev_number, 1);
        return PTR_ERR(button_class);
    }

    button_device = device_create(button_class, parent_dev, dev_number, NULL, DEVICE_NAME);
    if (IS_ERR(button_device)) {
        class_destroy(button_class);
        unregister_chrdev_region(dev_number, 1);
        return PTR_ERR(button_device);
    }

    cdev_init(&button_cdev, &button_fops);
    button_cdev.owner = THIS_MODULE;
    result = cdev_add(&button_cdev, dev_number, 1);
    if (result < 0) {
        device_destroy(button_class, dev_number);
        class_destroy(button_class);
        unregister_chrdev_region(dev_number, 1);
        return result;
    }

    printk(KERN_INFO "BUTTON_DRV: /dev/%s created\n", DEVICE_NAME);
    return 0;
}

static void cleanup_button_char_device(void) {
    cdev_del(&button_cdev);
    device_destroy(button_class, dev_number);
    class_destroy(button_class);
    unregister_chrdev_region(dev_number, 1);
    printk(KERN_INFO "BUTTON_DRV: Character device cleaned up\n");
}

static int button_probe(struct platform_device *pdev) {
    int result;
    struct device *dev = &pdev->dev;

    printk(KERN_INFO "BUTTON_DRV: Platform device probed\n");

    button_driver_data = devm_kzalloc(dev, sizeof(*button_driver_data), GFP_KERNEL);
    if (!button_driver_data) return -ENOMEM;

    button_driver_data->dev = dev;
    platform_set_drvdata(pdev, button_driver_data);

    button_gpio = devm_gpiod_get(dev, "button", GPIOD_IN);
    if (IS_ERR(button_gpio)) {
        dev_err(dev, "Failed to get button GPIO from device tree\n");
        return PTR_ERR(button_gpio);
    }

    button_driver_data->button_gpio = button_gpio;

    button_irq = gpiod_to_irq(button_gpio);
    if (button_irq < 0) {
        dev_err(dev, "Failed to get IRQ for button GPIO\n");
        return button_irq;
    }

    result = devm_request_irq(dev, button_irq, button_irq_handler,
                              IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                              "gpio_button", button_driver_data);
    if (result) {
        dev_err(dev, "Failed to request IRQ\n");
        return result;
    }

    button_driver_data->irq = button_irq;

    result = setup_button_char_device(dev);
    if (result) return result;

    dev_info(dev, "Button driver initialized (Device Tree mode)\n");
    return 0;
}

static void button_remove(struct platform_device *pdev) {
    cleanup_button_char_device();
    printk(KERN_INFO "BUTTON_DRV: Platform device removed\n");
}

static const struct of_device_id button_of_match[] = {
    { .compatible = "custom,gpio-button", },
    { }
};
MODULE_DEVICE_TABLE(of, button_of_match);

static struct platform_driver button_driver = {
    .probe = button_probe,
    .remove = button_remove,
    .driver = {
        .name = "gpio-button",
        .of_match_table = button_of_match,
    },
};

static int __init button_init(void) {
    printk(KERN_INFO "BUTTON_DRV: Initializing driver\n");
    return platform_driver_register(&button_driver);
}

static void __exit button_exit(void) {
    platform_driver_unregister(&button_driver);
    printk(KERN_INFO "BUTTON_DRV: Exiting driver\n");
}

module_init(button_init);
module_exit(button_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AnhPh58");
MODULE_DESCRIPTION("GPIO Button Driver (Device Tree only)");
MODULE_VERSION("1.0");
MODULE_ALIAS("platform:gpio-button");
