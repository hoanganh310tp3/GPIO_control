
#include <linux/module.h>        /* For module_platform_driver */
#include <linux/platform_device.h> /* For platform driver support */
#include <linux/gpio/consumer.h> /* For GPIO descriptor interface */
#include <linux/kernel.h>       /* For kernel functions */
#include <linux/fs.h>           /* For file operations */
#include <linux/cdev.h>         /* For character device */
#include <linux/device.h>       /* For device creation */
#include <linux/uaccess.h>      /* For copy_to/from_user */
#include <linux/of.h>           /* For device tree support */

/* Device name and class definitions */
#define DEVICE_NAME "gpio_led"
#define DEVICE_CLASS "gpio_led_class"
#define NUM_DEVICES 3           /* Number of LED devices */

/* IOCTL command definitions */
#define GPIO_IOC_MAGIC 'k'      /* Magic number for IOCTL */
#define GPIO_IOC_LED_ON    _IO(GPIO_IOC_MAGIC, 1)    /* Turn LED on */
#define GPIO_IOC_LED_OFF   _IO(GPIO_IOC_MAGIC, 2)    /* Turn LED off */
#define GPIO_IOC_LED_TOGGLE _IO(GPIO_IOC_MAGIC, 3)   /* Toggle LED state */
#define GPIO_IOC_GET_STATUS _IOR(GPIO_IOC_MAGIC, 4, int) /* Get LED status */

/* GPIO and state tracking variables */
static struct gpio_desc *led_gpio[NUM_DEVICES];   /* GPIO descriptors for LEDs */
static bool led_state[NUM_DEVICES] = {false, false, false}; /* LED states */

/* Character device variables */
static dev_t dev_num;           /* Device number */
static struct class *dev_class; /* Device class */
static struct cdev led_cdev[NUM_DEVICES];    /* Character device structures */
static struct device *led_device[NUM_DEVICES]; /* Device structures */

/* LED device information structure */
struct my_led {
    const char *name;   /* LED name (green/white/yellow) */
    int index;         /* LED index (0-2) */
};

/* LED device configurations */
static struct my_led leds[NUM_DEVICES] = {
    { .name = "green_led" , .index = 0},   /* Green LED */
    { .name = "white_led" , .index = 1},   /* White LED */
    { .name = "yellow_led" , .index = 2},  /* Yellow LED */
};

/* Function prototypes for file operations */
static int led_open(struct inode *, struct file *);
static int led_release(struct inode *, struct file *);
static ssize_t led_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t led_write(struct file *, const char __user *, size_t, loff_t *);
static long led_ioctl(struct file *, unsigned int, unsigned long);

/* File operations structure */
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .release = led_release,
    .read = led_read,
    .write = led_write,
    .unlocked_ioctl = led_ioctl,
};

/*
 * Export GPIO access function for button driver
 * @index: LED index (0-2)
 * Returns: GPIO descriptor pointer or NULL if invalid index
 */
struct gpio_desc *led_get_gpio(int index) {
    if(index >= 0 && index < NUM_DEVICES){
        return led_gpio[index];
    }
    return NULL;
}
EXPORT_SYMBOL(led_get_gpio);

/*
 * Open file operation
 * Validates minor number and stores LED info in private_data
 */
static int led_open(struct inode *inode, struct file *file){
    int minor = iminor(inode);

    if (minor >= NUM_DEVICES) {
        pr_err("Invalid minor number: %d\n", minor);
        return -ENODEV;
    }

    pr_info("Opening led %s (minor %d)\n", leds[minor].name, minor);
    file->private_data = &leds[minor];
    return 0;
}

/*
 * Release file operation
 * Logs release of LED device
 */
static int led_release(struct inode *inode, struct file *file){
    int minor = iminor(inode);
    pr_info("Releasing led %s (minor %d)\n", leds[minor].name, minor);
    return 0;
}

/*
 * Write file operation
 * Accepts commands:
 * '1' - Turn LED on
 * '0' - Turn LED off
 * 't' - Toggle LED state
 */
static ssize_t led_write(struct file *file, const char __user *buffer, size_t len, loff_t *off)
{
    char cmd;
    struct my_led *dev = file->private_data;
    int led_index = dev->index;

    if (len < 1 || copy_from_user(&cmd, buffer, 1))
        return -EFAULT;

    switch (cmd) {
        case '1':
            led_state[led_index] = true;
            gpiod_set_value(led_gpio[led_index], 1);
            pr_info("Led %s is ON\n", dev->name);
            break;
        case '0':
            led_state[led_index] = false;
            gpiod_set_value(led_gpio[led_index], 0);
            pr_info("Led %s is OFF\n", dev->name);
            break;
        case 't':
            led_state[led_index] = !led_state[led_index];
            gpiod_set_value(led_gpio[led_index], led_state[led_index]);
            pr_info("Led %s is %s\n", dev->name, led_state[led_index] ? "ON" : "OFF");
            break;
        default:
            pr_err("Invalid command: %c\n", cmd);
            return -EINVAL;
    }
    return len;
}

/*
 * Read file operation
 * Returns current LED state as string
 */
static ssize_t led_read(struct file *file, char __user *buffer, size_t len, loff_t *offset)
{
    char status_msg[100];
    int msg_len;
    struct my_led *dev = file->private_data;
    int led_index = dev->index;

    if(*offset != 0)
        return 0;

    msg_len = snprintf(status_msg, sizeof(status_msg), "%s is %s\n", dev->name, led_state[led_index] ? "ON" : "OFF");

    if(len < msg_len)
        return -EINVAL;

    if(copy_to_user(buffer, status_msg, msg_len))
        return -EFAULT;

    *offset += msg_len;
    return msg_len;
}

/*
 * IOCTL file operation
 * Supports:
 * - GPIO_IOC_LED_ON: Turn LED on
 * - GPIO_IOC_LED_OFF: Turn LED off
 * - GPIO_IOC_LED_TOGGLE: Toggle LED state
 * - GPIO_IOC_GET_STATUS: Get current LED state
 */
static long led_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct my_led *dev = file->private_data;
    int led_index = dev->index;
    int status;

    switch(cmd){
        case GPIO_IOC_LED_ON:
            led_state[led_index] = true;
            gpiod_set_value(led_gpio[led_index], 1);
            pr_info("Led %s is ON by ioctl\n", dev->name);
            break;

        case GPIO_IOC_LED_OFF:  
            led_state[led_index] = false;
            gpiod_set_value(led_gpio[led_index], 0);
            pr_info("Led %s is OFF by ioctl\n", dev->name);
            break;

        case GPIO_IOC_LED_TOGGLE:
            led_state[led_index] = !led_state[led_index];
            gpiod_set_value(led_gpio[led_index], led_state[led_index]);
            pr_info("Led %s is %s by ioctl\n", dev->name, led_state[led_index] ? "ON" : "OFF");
            break;

        case GPIO_IOC_GET_STATUS:
            status = led_state[led_index] ? 1 : 0;
            if (copy_to_user((void __user *)arg, &status, sizeof(status)))
                return -EFAULT;
            break;

        default:
            return -ENOTTY;
    }   
    return 0;
}

/*
 * Platform driver probe function
 * Initializes:
 * - GPIO pins for LEDs
 * - Character devices
 * - Device class and nodes
 */
static int led_probe(struct platform_device *pdev)
{
    int ret, i;
    struct device *dev = &pdev->dev;

    pr_info("Probe led driver\n");

    /* Initialize GPIO pins */
    for(i = 0; i < NUM_DEVICES; i++){
        led_gpio[i] = devm_gpiod_get_index(dev, "led", i, GPIOD_OUT_LOW);
        if(IS_ERR(led_gpio[i])) {
            dev_err(dev, "Failed to get led %d\n", i);
            return PTR_ERR(led_gpio[i]);
        }

        led_state[i] = false;
        gpiod_set_value(led_gpio[i], 0);
    }

    /* Allocate character device region */
    ret = alloc_chrdev_region(&dev_num, 0, NUM_DEVICES, DEVICE_NAME);
    if( ret < 0 ) {
        dev_err(dev, "Failed to allocate char device region\n");
        return ret;
    }

    /* Create device class */
    dev_class = class_create(DEVICE_CLASS);
    if(IS_ERR(dev_class)) {
        dev_err(dev, "Failed to create device class\n");
        ret = PTR_ERR(dev_class);
        goto cleanup_chrdev;
    }

    /* Create character devices and nodes */
    for(i =0; i < NUM_DEVICES; i++){
        cdev_init(&led_cdev[i], &fops);
        led_cdev[i].owner = THIS_MODULE;

        ret = cdev_add(&led_cdev[i], MKDEV(MAJOR(dev_num), i), 1);
        if(ret < 0){
            dev_err(dev, "Failed to add cdev for led %d\n", i);
            goto cleanup_cdevs;
        }

        led_device[i] = device_create(dev_class, NULL, MKDEV(MAJOR(dev_num), i), NULL, "%s%d", DEVICE_NAME, i);
        if(IS_ERR(led_device[i])) {
            dev_err(dev, "Failed to create device for led %d\n", i);
            ret = PTR_ERR(led_device[i]);
            cdev_del(&led_cdev[i]);
            goto cleanup_cdevs;
        }

        pr_info("Created device /dev/%s%d for %s\n", DEVICE_NAME, i, leds[i].name);
    }

    pr_info("Led driver probe completed successfully\n");
    return 0;

cleanup_cdevs:
    for(i = i - 1; i >= 0; i--){
        device_destroy(dev_class, MKDEV(MAJOR(dev_num), i));
        cdev_del(&led_cdev[i]);
    }
    class_destroy(dev_class);

cleanup_chrdev:
    unregister_chrdev_region(dev_num, NUM_DEVICES);
    return ret;
}

/*
 * Platform driver remove function
 * Cleans up:
 * - LED states
 * - Character devices
 * - Device class and nodes
 */
static void led_remove(struct platform_device *pdev)
{
    int i;
    pr_info("Led driver remove\n");

    /* Turn off LEDs and clean up devices */
    for(i = 0; i < NUM_DEVICES; i++){
        led_state[i] = false;
        gpiod_set_value(led_gpio[i], 0);
        device_destroy(dev_class, MKDEV(MAJOR(dev_num), i));
        cdev_del(&led_cdev[i]);
        pr_info("Removed device /dev/%s%d for %s\n", DEVICE_NAME, i, leds[i].name);
    }

    /* Clean up class and character device region */
    class_destroy(dev_class);
    unregister_chrdev_region(dev_num, NUM_DEVICES);
    pr_info("Led driver removed successfully\n");
}

/* Device tree matching table */
static const struct of_device_id led_of_match[] = {
    { .compatible = "custom,gpio-led" },
    { }    
};

MODULE_DEVICE_TABLE(of, led_of_match);

/* Platform driver structure */
static struct platform_driver led_driver = {
    .probe = led_probe,
    .remove_new = led_remove,
    .driver = {
        .name = "led_driver",
        .of_match_table = led_of_match,
    },
};

/* Register platform driver */
module_platform_driver(led_driver);

/* Module information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("AnhPH58");
MODULE_DESCRIPTION("GPIO Led Driver");
