#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>

#define DEVICE_NAME "gpio_ctl"
#define CLASS_NAME "gpio_class"

// GPIO pin definitions (fallback if no device tree)
#define LED_GPIO_PIN 21
#define BUTTON_GPIO_PIN 20

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
static int button_irq;
static bool led_status = false;
static bool use_device_tree = false;

// Legacy GPIO numbers (fallback)
static int led_gpio_num = LED_GPIO_PIN;
static int button_gpio_num = BUTTON_GPIO_PIN;

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

// IRQ handler for button
static irqreturn_t button_irq_handler(int irq, void *dev_id) {
    static unsigned long last_interrupt = 0;
    unsigned long interrupt_time = jiffies;
    
    // Debounce: ignore interrupts within 200ms
    if (interrupt_time - last_interrupt < msecs_to_jiffies(200)) {
        return IRQ_HANDLED;
    }
    last_interrupt = interrupt_time;
    
    printk(KERN_INFO "GPIO_CTL: Button pressed!\n");
    return IRQ_HANDLED;
}

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
    
    if (use_device_tree && button_gpio) {
        button_state = gpiod_get_value(button_gpio);
    } else {
        button_state = gpio_get_value(button_gpio_num);
    }
    
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
        if (use_device_tree && led_gpio) {
            gpiod_set_value(led_gpio, 1);
        } else {
            gpio_set_value(led_gpio_num, 1);
        }
        led_status = true;
        printk(KERN_INFO "GPIO_CTL: LED turned ON\n");
    } else if (strcmp(command, "0") == 0 || strcmp(command, "off") == 0) {
        if (use_device_tree && led_gpio) {
            gpiod_set_value(led_gpio, 0);
        } else {
            gpio_set_value(led_gpio_num, 0);
        }
        led_status = false;
        printk(KERN_INFO "GPIO_CTL: LED turned OFF\n");
    } else if (strcmp(command, "toggle") == 0) {
        led_status = !led_status;
        if (use_device_tree && led_gpio) {
            gpiod_set_value(led_gpio, led_status ? 1 : 0);
        } else {
            gpio_set_value(led_gpio_num, led_status ? 1 : 0);
        }
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
            if (use_device_tree && led_gpio) {
                gpiod_set_value(led_gpio, 1);
            } else {
                gpio_set_value(led_gpio_num, 1);
            }
            led_status = true;
            printk(KERN_INFO "GPIO_CTL: LED turned ON via IOCTL\n");
            break;
            
        case GPIO_IOC_LED_OFF:
            if (use_device_tree && led_gpio) {
                gpiod_set_value(led_gpio, 0);
            } else {
                gpio_set_value(led_gpio_num, 0);
            }
            led_status = false;
            printk(KERN_INFO "GPIO_CTL: LED turned OFF via IOCTL\n");
            break;
            
        case GPIO_IOC_LED_TOGGLE:
            led_status = !led_status;
            if (use_device_tree && led_gpio) {
                gpiod_set_value(led_gpio, led_status ? 1 : 0);
            } else {
                gpio_set_value(led_gpio_num, led_status ? 1 : 0);
            }
            printk(KERN_INFO "GPIO_CTL: LED toggled via IOCTL\n");
            break;
            
        case GPIO_IOC_GET_STATUS:
            if (use_device_tree && button_gpio) {
                button_state = gpiod_get_value(button_gpio);
            } else {
                button_state = gpio_get_value(button_gpio_num);
            }
            if (copy_to_user((int*)arg, &button_state, sizeof(int))) {
                return -EFAULT;
            }
            break;
            
        default:
            return -EINVAL;
    }
    
    return 0;
}

// Legacy GPIO setup (fallback)
static int setup_legacy_gpio(void) {
    int result;
    
    printk(KERN_INFO "GPIO_CTL: Setting up legacy GPIO mode\n");
    
    // Request GPIO pins
    result = gpio_request(led_gpio_num, "LED_GPIO");
    if (result) {
        printk(KERN_ERR "GPIO_CTL: Failed to request LED GPIO %d\n", led_gpio_num);
        return result;
    }
    
    result = gpio_request(button_gpio_num, "BUTTON_GPIO");
    if (result) {
        printk(KERN_ERR "GPIO_CTL: Failed to request Button GPIO %d\n", button_gpio_num);
        gpio_free(led_gpio_num);
        return result;
    }
    
    // Set GPIO directions
    result = gpio_direction_output(led_gpio_num, 0);
    if (result) {
        printk(KERN_ERR "GPIO_CTL: Failed to set LED GPIO direction\n");
        goto cleanup_gpio;
    }
    
    result = gpio_direction_input(button_gpio_num);
    if (result) {
        printk(KERN_ERR "GPIO_CTL: Failed to set Button GPIO direction\n");
        goto cleanup_gpio;
    }
    
    // Setup button interrupt
    button_irq = gpio_to_irq(button_gpio_num);
    if (button_irq < 0) {
        printk(KERN_ERR "GPIO_CTL: Failed to get IRQ for button GPIO\n");
        result = button_irq;
        goto cleanup_gpio;
    }
    
    result = request_irq(button_irq, button_irq_handler,
                        IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                        "gpio_button", NULL);
    if (result) {
        printk(KERN_ERR "GPIO_CTL: Failed to request IRQ\n");
        goto cleanup_gpio;
    }
    
    printk(KERN_INFO "GPIO_CTL: Legacy GPIO setup complete - LED: %d, Button: %d\n", 
           led_gpio_num, button_gpio_num);
    return 0;
    
cleanup_gpio:
    gpio_free(button_gpio_num);
    gpio_free(led_gpio_num);
    return result;
}

static void cleanup_legacy_gpio(void) {
    if (button_irq >= 0) {
        free_irq(button_irq, NULL);
    }
    gpio_set_value(led_gpio_num, 0); // Turn off LED
    gpio_free(button_gpio_num);
    gpio_free(led_gpio_num);
    printk(KERN_INFO "GPIO_CTL: Legacy GPIO cleanup complete\n");
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,4,0)
    gpio_class = class_create(CLASS_NAME);
#else
    gpio_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
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
    use_device_tree = true;
    
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
    
    // Setup button interrupt
    button_irq = gpiod_to_irq(button_gpio);
    if (button_irq < 0) {
        dev_err(dev, "Failed to get IRQ for button GPIO\n");
        return button_irq;
    }
    
    result = devm_request_irq(dev, button_irq, button_irq_handler,
                             IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                             "gpio_button", gpio_data);
    if (result) {
        dev_err(dev, "Failed to request IRQ\n");
        return result;
    }
    
    // Setup character device
    result = setup_char_device(dev);
    if (result) {
        return result;
    }
    
    dev_info(dev, "GPIO Control driver probed successfully (Device Tree mode)\n");
    return 0;
}

// Platform driver remove function
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,18,0)
static void gpio_ctrl_remove(struct platform_device *pdev)
#else
static int gpio_ctrl_remove(struct platform_device *pdev)
#endif
{
    printk(KERN_INFO "GPIO_CTL: Platform device removed\n");
    
    // Turn off LED before removing
    if (led_gpio) {
        gpiod_set_value(led_gpio, 0);
    }
    
    // Cleanup character device
    cleanup_char_device();
    
    printk(KERN_INFO "GPIO_CTL: Platform device removal complete\n");
    
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,18,0)
    return 0;
#endif
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
    int result;
    
    printk(KERN_INFO "GPIO_CTL: Initializing GPIO Control driver\n");
    
    // Try to register platform driver first (device tree mode)
    result = platform_driver_register(&gpio_ctrl_driver);
    if (result) {
        printk(KERN_ERR "GPIO_CTL: Failed to register platform driver\n");
        return result;
    }
    
    // If no device tree match found after a short delay, use legacy mode
    msleep(100);
    
    if (!use_device_tree) {
        printk(KERN_INFO "GPIO_CTL: No device tree match, using legacy GPIO mode\n");
        
        result = setup_legacy_gpio();
        if (result) {
            platform_driver_unregister(&gpio_ctrl_driver);
            return result;
        }
        
        result = setup_char_device(NULL);
        if (result) {
            cleanup_legacy_gpio();
            platform_driver_unregister(&gpio_ctrl_driver);
            return result;
        }
        
        printk(KERN_INFO "GPIO_CTL: Driver initialized in legacy mode\n");
    }
    
    return 0;
}

// Module exit function
static void __exit gpio_ctrl_exit(void) {
    printk(KERN_INFO "GPIO_CTL: Exiting GPIO Control driver\n");
    
    if (!use_device_tree) {
        cleanup_char_device();
        cleanup_legacy_gpio();
    }
    
    platform_driver_unregister(&gpio_ctrl_driver);
    printk(KERN_INFO "GPIO_CTL: Driver exit complete\n");
}

module_init(gpio_ctrl_init);
module_exit(gpio_ctrl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AnhPh58");
MODULE_DESCRIPTION("GPIO Control Driver for Raspberry Pi - Device Tree + Legacy support");
MODULE_VERSION("2.0");
MODULE_ALIAS("platform:gpio-control");
