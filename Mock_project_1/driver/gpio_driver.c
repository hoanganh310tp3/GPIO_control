#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define DEVICE_NAME "gpio_ctl"
#define CLASS_NAME "gpio_class"

// IOCTL commands
#define GPIO_IOC_MAGIC 'g'
#define GPIO_IOC_LED_ON    _IO(GPIO_IOC_MAGIC, 1)
#define GPIO_IOC_LED_OFF   _IO(GPIO_IOC_MAGIC, 2)
#define GPIO_IOC_LED_TOGGLE _IO(GPIO_IOC_MAGIC, 3)
#define GPIO_IOC_GET_STATUS _IOR(GPIO_IOC_MAGIC, 4, int)

// Device variables
static dev_t dev_number;
static struct class* gpio_class = NULL;
static struct device* gpio_device = NULL;
static struct cdev gpio_cdev;

// GPIO variables
static struct gpio_desc *led_gpio = NULL;
static struct gpio_desc *button_gpio = NULL;
static bool led_status = false;

// Platform driver data
struct gpio_ctrl_data {
    struct gpio_desc *led_gpio;
    struct gpio_desc *button_gpio;
    struct device *dev;
};

static struct gpio_ctrl_data *gpio_data = NULL;

// Function prototypes
static int gpio_open(struct inode *inode, struct file *file);
static int gpio_release(struct inode *inode, struct file *file);
static ssize_t gpio_read(struct file *file, char __user *buffer, size_t len, loff_t *offset);
static ssize_t gpio_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset);
static long gpio_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

// File operations structure
static struct file_operations fops = {
    .open = gpio_open,
    .read = gpio_read,
    .write = gpio_write,
    .release = gpio_release,
    .unlocked_ioctl = gpio_ioctl,
    .owner = THIS_MODULE,
};

// File operations implementations
static int gpio_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "GPIO_CTL: Device opened\n");
    return 0;
}

static int gpio_release(struct inode *inode, struct file *file) {
    printk(KERN_INFO "GPIO_CTL: Device closed\n");
    return 0;
}

static ssize_t gpio_read(struct file *file, char __user *buffer, size_t len, loff_t *offset) {
    int button_state;
    char msg[64];
    int msg_len;
    
    if (*offset > 0) return 0; // EOF
    
    // Đọc trạng thái button (polling)
    button_state = gpiod_get_value(button_gpio);
    
    msg_len = snprintf(msg, sizeof(msg), "LED: %s, Button: %s\n",
                      led_status ? "ON" : "OFF",
                      button_state ? "PRESSED" : "RELEASED");
    
    if (len < msg_len) return -EINVAL;
    
    if (copy_to_user(buffer, msg, msg_len)) {
        return -EFAULT;
    }
    
    *offset = msg_len;
    return msg_len;
}

static ssize_t gpio_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset) {
    char command[16];
    
    if (len >= sizeof(command)) return -EINVAL;
    
    if (copy_from_user(command, buffer, len)) {
        return -EFAULT;
    }
    
    command[len] = '\0';
    
    // Remove newline if present
    if (len > 0 && command[len-1] == '\n') {
        command[len-1] = '\0';
    }
    
    // Process commands
    if (strcmp(command, "1") == 0 || strcmp(command, "on") == 0) {
        gpiod_set_value(led_gpio, 1);
        led_status = true;
        printk(KERN_INFO "GPIO_CTL: LED turned ON\n");
    } else if (strcmp(command, "0") == 0 || strcmp(command, "off") == 0) {
        gpiod_set_value(led_gpio, 0);
        led_status = false;
        printk(KERN_INFO "GPIO_CTL: LED turned OFF\n");
    } else if (strcmp(command, "toggle") == 0) {
        led_status = !led_status;
        gpiod_set_value(led_gpio, led_status ? 1 : 0);
        printk(KERN_INFO "GPIO_CTL: LED toggled to %s\n", led_status ? "ON" : "OFF");
    } else {
        printk(KERN_WARNING "GPIO_CTL: Invalid command. Use '1', '0', 'on', 'off', or 'toggle'\n");
        return -EINVAL;
    }
    
    return len;
}

static long gpio_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    int button_state;
    
    switch (cmd) {
        case GPIO_IOC_LED_ON:
            gpiod_set_value(led_gpio, 1);
            led_status = true;
            printk(KERN_INFO "GPIO_CTL: LED turned ON via IOCTL\n");
            break;
            
        case GPIO_IOC_LED_OFF:
            gpiod_set_value(led_gpio, 0);
            led_status = false;
            printk(KERN_INFO "GPIO_CTL: LED turned OFF via IOCTL\n");
            break;
            
        case GPIO_IOC_LED_TOGGLE:
            led_status = !led_status;
            gpiod_set_value(led_gpio, led_status ? 1 : 0);
            printk(KERN_INFO "GPIO_CTL: LED toggled via IOCTL\n");
            break;
            
        case GPIO_IOC_GET_STATUS:
            button_state = gpiod_get_value(button_gpio);
            if (copy_to_user((int*)arg, &button_state, sizeof(int))) {
                return -EFAULT;
            }
            break;
            
        default:
            return -EINVAL;
    }
    
    return 0;
}

// Character device setup
static int setup_char_device(struct device *parent_dev) {
    int result;
    
    // Allocate device number
    result = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (result < 0) {
        printk(KERN_ERR "GPIO_CTL: Failed to allocate device number\n");
        return result;
    }
    
    // Create device class
    gpio_class = class_create(CLASS_NAME);
    if (IS_ERR(gpio_class)) {
        unregister_chrdev_region(dev_number, 1);
        return PTR_ERR(gpio_class);
    }
    
    // Create device
    gpio_device = device_create(gpio_class, parent_dev, dev_number, NULL, DEVICE_NAME);
    if (IS_ERR(gpio_device)) {
        class_destroy(gpio_class);
        unregister_chrdev_region(dev_number, 1);
        return PTR_ERR(gpio_device);
    }
    
    // Initialize and add character device
    cdev_init(&gpio_cdev, &fops);
    gpio_cdev.owner = THIS_MODULE;
    result = cdev_add(&gpio_cdev, dev_number, 1);
    if (result < 0) {
        device_destroy(gpio_class, dev_number);
        class_destroy(gpio_class);
        unregister_chrdev_region(dev_number, 1);
        return result;
    }
    
    printk(KERN_INFO "GPIO_CTL: Character device created: /dev/%s (major: %d)\n", 
           DEVICE_NAME, MAJOR(dev_number));
    
    return 0;
}

static void cleanup_char_device(void) {
    cdev_del(&gpio_cdev);
    device_destroy(gpio_class, dev_number);
    class_destroy(gpio_class);
    unregister_chrdev_region(dev_number, 1);
    printk(KERN_INFO "GPIO_CTL: Character device cleanup complete\n");
}

// Platform driver probe function
static int gpio_ctrl_probe(struct platform_device *pdev) {
    int result;
    struct device *dev = &pdev->dev;
    
    printk(KERN_INFO "GPIO_CTL: Platform device probed\n");
    
    // Allocate driver data
    gpio_data = devm_kzalloc(dev, sizeof(*gpio_data), GFP_KERNEL);
    if (!gpio_data) {
        return -ENOMEM;
    }
    
    gpio_data->dev = dev;
    platform_set_drvdata(pdev, gpio_data);
    
    // Get GPIO descriptors from device tree
    led_gpio = devm_gpiod_get(dev, "led", GPIOD_OUT_LOW);
    if (IS_ERR(led_gpio)) {
        dev_err(dev, "Failed to get LED GPIO from device tree\n");
        return PTR_ERR(led_gpio);
    }
    
    button_gpio = devm_gpiod_get(dev, "button", GPIOD_IN);
    if (IS_ERR(button_gpio)) {
        dev_err(dev, "Failed to get Button GPIO from device tree\n");
        return PTR_ERR(button_gpio);
    }
    
    gpio_data->led_gpio = led_gpio;
    gpio_data->button_gpio = button_gpio;
    
    // Setup character device
    result = setup_char_device(dev);
    if (result) {
        return result;
    }
    
    dev_info(dev, "GPIO Control driver initialized successfully (GPIO only, no interrupts)\n");
    return 0;
}

// Platform driver remove function
static void gpio_ctrl_remove(struct platform_device *pdev) {
    printk(KERN_INFO "GPIO_CTL: Platform device removed\n");
    
    // Turn off LED before removing
    if (led_gpio) {
        gpiod_set_value(led_gpio, 0);
    }
    
    // Cleanup character device
    cleanup_char_device();
    
    printk(KERN_INFO "GPIO_CTL: Platform device removal complete\n");
}

// Device tree matching
static const struct of_device_id gpio_ctrl_of_match[] = {
    { .compatible = "custom,gpio-control", },
    { }
};
MODULE_DEVICE_TABLE(of, gpio_ctrl_of_match);

// Platform driver structure
static struct platform_driver gpio_ctrl_driver = {
    .probe = gpio_ctrl_probe,
    .remove = gpio_ctrl_remove,
    .driver = {
        .name = "gpio-control",
        .of_match_table = gpio_ctrl_of_match,
    },
};

// Module init function
static int __init gpio_ctrl_init(void) {
    printk(KERN_INFO "GPIO_CTL: Initializing GPIO Control driver (GPIO only)\n");
    return platform_driver_register(&gpio_ctrl_driver);
}

// Module exit function
static void __exit gpio_ctrl_exit(void) {
    printk(KERN_INFO "GPIO_CTL: Exiting GPIO Control driver\n");
    platform_driver_unregister(&gpio_ctrl_driver);
    printk(KERN_INFO "GPIO_CTL: Driver exit complete\n");
}

module_init(gpio_ctrl_init);
module_exit(gpio_ctrl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AnhPh58");
MODULE_DESCRIPTION("GPIO Control Driver for Raspberry Pi - GPIO only (no interrupts)");
MODULE_VERSION("3.1");
MODULE_ALIAS("platform:gpio-control");
