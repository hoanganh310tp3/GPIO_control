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

#define DEVICE_NAME "gpio_ctl2" 
#define CLASS_NAME "gpio_class2"

// IOCTL commands
#define GPIO_IOC_MAGIC 'h'
#define GPIO_IOC_LED_ON    _IO(GPIO_IOC_MAGIC, 1)
#define GPIO_IOC_LED_OFF   _IO(GPIO_IOC_MAGIC, 2)
#define GPIO_IOC_LED_TOGGLE _IO(GPIO_IOC_MAGIC, 3)
#define GPIO_IOC_GET_STATUS _IOR(GPIO_IOC_MAGIC, 4, int)

// Device variables
static dev_t dev_num;
static struct cdev gpio_cdev;
static struct class *gpio_class;
static struct device *gpio_device;

// Platform device pointer
static struct platform_device *pdev_global;

// GPIO descriptors
static struct gpio_desc *led_gpio;
static struct gpio_desc *button_gpio;

// LED state tracking
static bool led_state = false;

// Button interrupt variables
static int button_irq;
static bool last_button_state = true; // Default HIGH (pull-up)

// Button interrupt handler - SIMPLIFIED VERSION
static irqreturn_t button_irq_handler(int irq, void *dev_id)
{
    static unsigned long last_interrupt_time = 0;
    unsigned long interrupt_time = jiffies;
    
    if (interrupt_time - last_interrupt_time < msecs_to_jiffies(50)) {
        return IRQ_HANDLED;
    }
    last_interrupt_time = interrupt_time;
    
    // Toggle LED ngay lập tức - không cần check state
    led_state = !led_state;
    gpiod_set_value(led_gpio, led_state);
    
    printk(KERN_INFO "GPIO_CTL2: Button pressed! LED %s\n", 
           led_state ? "ON" : "OFF");
    
    return IRQ_HANDLED;
}

// Character device file operations
static int gpio_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "GPIO_CTL2: Device opened\n");
    return 0;
}

static int gpio_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "GPIO_CTL2: Device closed\n");
    return 0;
}

static ssize_t gpio_read(struct file *file, char __user *buffer, size_t len, loff_t *offset)
{
    char status_msg[100];
    int msg_len;
    bool button_pressed;
    
    if (*offset > 0)
        return 0;
    
    // Read current GPIO states
    // Button pressed when GPIO16 = LOW (connected to GND)
    button_pressed = (gpiod_get_value(button_gpio) == 0);
    
    msg_len = snprintf(status_msg, sizeof(status_msg),
                      "LED: %s, Button: %s (GPIO16=%d)\n",
                      led_state ? "ON" : "OFF",
                      button_pressed ? "PRESSED" : "RELEASED",
                      gpiod_get_value(button_gpio));
    
    if (len < msg_len)
        return -EINVAL;
    
    if (copy_to_user(buffer, status_msg, msg_len))
        return -EFAULT;
    
    *offset = msg_len;
    return msg_len;
}

static ssize_t gpio_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset)
{
    char cmd;
    
    if (len == 0)
        return -EINVAL;
    
    if (copy_from_user(&cmd, buffer, 1))
        return -EFAULT;
    
    switch (cmd) {
        case '1':
            led_state = true;
            gpiod_set_value(led_gpio, 1);
            printk(KERN_INFO "GPIO_CTL2: LED turned ON (GPIO25=HIGH)\n");
            break;
        case '0':
            led_state = false;
            gpiod_set_value(led_gpio, 0);
            printk(KERN_INFO "GPIO_CTL2: LED turned OFF (GPIO25=LOW)\n");
            break;
        case 't':
        case 'T':
            led_state = !led_state;
            gpiod_set_value(led_gpio, led_state);
            printk(KERN_INFO "GPIO_CTL2: LED toggled %s (GPIO25=%s)\n", 
                   led_state ? "ON" : "OFF",
                   led_state ? "HIGH" : "LOW");
            break;
        default:
            printk(KERN_WARNING "GPIO_CTL2: Invalid command '%c'. Use '1', '0', or 't'\n", cmd);
            return -EINVAL;
    }
    
    return len;
}

static long gpio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int status;
    
    switch (cmd) {
        case GPIO_IOC_LED_ON:
            led_state = true;
            gpiod_set_value(led_gpio, 1);
            printk(KERN_INFO "GPIO_CTL2: LED turned ON (ioctl)\n");
            break;
            
        case GPIO_IOC_LED_OFF:
            led_state = false;
            gpiod_set_value(led_gpio, 0);
            printk(KERN_INFO "GPIO_CTL2: LED turned OFF (ioctl)\n");
            break;
            
        case GPIO_IOC_LED_TOGGLE:
            led_state = !led_state;
            gpiod_set_value(led_gpio, led_state);
            printk(KERN_INFO "GPIO_CTL2: LED toggled %s (ioctl)\n", led_state ? "ON" : "OFF");
            break;
            
        case GPIO_IOC_GET_STATUS:
            // Bit 0: LED state, Bit 1: Button pressed
            status = (led_state ? 1 : 0) | (gpiod_get_value(button_gpio) == 0 ? 2 : 0);
            if (copy_to_user((int __user *)arg, &status, sizeof(status)))
                return -EFAULT;
            break;
            
        default:
            return -ENOTTY;
    }
    
    return 0;
}

static struct file_operations gpio_fops = {
    .owner = THIS_MODULE,
    .open = gpio_open,
    .release = gpio_release,
    .read = gpio_read,
    .write = gpio_write,
    .unlocked_ioctl = gpio_ioctl,
};

// Platform driver probe function - FIXED VERSION
static int gpio_probe(struct platform_device *pdev)
{
    int ret;
    
    printk(KERN_INFO "GPIO_CTL2: Platform device probed\n");
    
    pdev_global = pdev;
    
    // Get LED GPIO (GPIO25) - Output, initially LOW
    // No need to set flag 
    led_gpio = devm_gpiod_get(&pdev->dev, "led", 0);
    if (IS_ERR(led_gpio)) {
        printk(KERN_ERR "GPIO_CTL2: Failed to get LED GPIO (GPIO25)\n");
        return PTR_ERR(led_gpio);
    }
    
    // Get Button GPIO (GPIO16) - Input 
    button_gpio = devm_gpiod_get(&pdev->dev, "button", 0;
    if (IS_ERR(button_gpio)) {
        printk(KERN_ERR "GPIO_CTL2: Failed to get Button GPIO (GPIO16)\n");
        return PTR_ERR(button_gpio);
    }
    
    // Setup button interrupt
    button_irq = gpiod_to_irq(button_gpio);
    if (button_irq < 0) {
        printk(KERN_ERR "GPIO_CTL2: Failed to get IRQ for button GPIO\n");
        return button_irq;
    }
    
    // Request interrupt for FALLING edge only (button press)
    ret = devm_request_irq(&pdev->dev, button_irq, button_irq_handler,
                          IRQF_TRIGGER_FALLING,
                          "gpio_button2", &pdev->dev);
    if (ret) {
        printk(KERN_ERR "GPIO_CTL2: Failed to request IRQ\n");
        return ret;
    }
    
    // Initialize LED state
    led_state = false;
    gpiod_set_value(led_gpio, 0);
    
    // Initialize button state (should be HIGH due to pull-up from DT)
    last_button_state = gpiod_get_value(button_gpio);
    
    printk(KERN_INFO "GPIO_CTL2: Initial states - LED: %s, Button: %s (GPIO16=%d)\n",
           led_state ? "ON" : "OFF",
           last_button_state ? "RELEASED" : "PRESSED",
           last_button_state);
    
    // Allocate device number
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "GPIO_CTL2: Failed to allocate device number\n");
        return ret;
    }
    
    // Initialize and add character device
    cdev_init(&gpio_cdev, &gpio_fops);
    gpio_cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&gpio_cdev, dev_num, 1);
    if (ret < 0) {
        printk(KERN_ERR "GPIO_CTL2: Failed to add character device\n");
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }
    
    // Create device class
    gpio_class = class_create(CLASS_NAME);
    if (IS_ERR(gpio_class)) {
        printk(KERN_ERR "GPIO_CTL2: Failed to create device class\n");
        cdev_del(&gpio_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(gpio_class);
    }
    
    // Create device file
    gpio_device = device_create(gpio_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(gpio_device)) {
        printk(KERN_ERR "GPIO_CTL2: Failed to create device\n");
        class_destroy(gpio_class);
        cdev_del(&gpio_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(gpio_device);
    }
    
    printk(KERN_INFO "GPIO_CTL2: Character device created: /dev/%s (major: %d)\n", 
           DEVICE_NAME, MAJOR(dev_num));
    
    dev_info(&pdev->dev, "GPIO Control driver 2 initialized (GPIO25=LED, GPIO16=Button, pull-up from DT)\n");
    
    return 0;
}

// Platform driver remove function
static void gpio_remove(struct platform_device *pdev)
{
    printk(KERN_INFO "GPIO_CTL2: Platform device removed\n");
    
    // Cleanup device
    device_destroy(gpio_class, dev_num);
    class_destroy(gpio_class);
    cdev_del(&gpio_cdev);
    unregister_chrdev_region(dev_num, 1);
    
    // Turn off LED
    if (led_gpio)
        gpiod_set_value(led_gpio, 0);
    
    printk(KERN_INFO "GPIO_CTL2: GPIO Control driver 2 removed\n");
}

// Device tree matching table
static const struct of_device_id gpio_of_match[] = {
    { .compatible = "custom,gpio-control2" },
    { }
};
MODULE_DEVICE_TABLE(of, gpio_of_match);

// Platform driver structure
static struct platform_driver gpio_platform_driver = {
    .probe = gpio_probe,
    .remove = gpio_remove,
    .driver = {
        .name = "gpio-control2",
        .of_match_table = gpio_of_match,
    },
};

// Module initialization
static int __init gpio_driver_init(void)
{
    int ret;
    
    printk(KERN_INFO "GPIO_CTL2: Initializing GPIO Control driver 2 (GPIO25=LED, GPIO16=Button 2-pin)\n");
    
    ret = platform_driver_register(&gpio_platform_driver);
    if (ret) {
        printk(KERN_ERR "GPIO_CTL2: Failed to register platform driver\n");
        return ret;
    }
    
    return 0;
}

// Module cleanup
static void __exit gpio_driver_exit(void)
{
    printk(KERN_INFO "GPIO_CTL2: Exiting GPIO Control driver 2\n");
    platform_driver_unregister(&gpio_platform_driver);
}

module_init(gpio_driver_init);
module_exit(gpio_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("GPIO Control Driver 2");
MODULE_DESCRIPTION("GPIO Control Driver 2 for LED (GPIO25) and 2-pin Button (GPIO16→GND)");
MODULE_VERSION("3.0"); 