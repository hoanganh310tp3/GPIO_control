
#include <linux/module.h>        /* For module_platform_driver */
#include <linux/platform_device.h> /* For platform driver support */
#include <linux/gpio/consumer.h> /* For GPIO descriptor interface */
#include <linux/interrupt.h>     /* For interrupt handling */
#include <linux/jiffies.h>      /* For jiffies counter */
#include <linux/kernel.h>       /* For kernel functions */
#include <linux/fs.h>           /* For file operations */
#include <linux/cdev.h>         /* For character device */
#include <linux/device.h>       /* For device creation */
#include <linux/uaccess.h>      /* For copy_to/from_user */
#include <linux/timer.h>        /* For timer functionality */
#include <linux/workqueue.h>    /* For workqueue */
#include <linux/of.h>           /* For device tree support */

/* Device and timing constants */
#define DEVICE_NAME "gpio_button"
#define DEVICE_CLASS "gpio_button_class"
#define DEBOUNCE_TIME_MS 50        /* Debounce time in milliseconds */
#define MULTI_PRESS_TIMEOUT_MS 1000 /* Timeout for multi-press detection */

/* External function declaration from LED driver */
extern struct gpio_desc *led_get_gpio(int index);

/* GPIO and device related variables */
static struct gpio_desc *button_gpio;     /* GPIO descriptor for button */
static int button_irq;                    /* IRQ number for button */
static dev_t dev_number;                  /* Device number */
static struct class *dev_class;           /* Device class */
static struct cdev button_cdev;           /* Character device structure */
static struct device *button_device;      /* Device structure */

/* Button press handling variables */
static int press_count = 0;               /* Count of button presses */
static struct timer_list press_timer;     /* Timer for multi-press detection */
static struct work_struct button_work;    /* Work structure for button processing */
static bool button_pressed = false;       /* Button press state */

/* LED control variables */
static struct gpio_desc *led_gpios[3];    /* Array of LED GPIO descriptors */
static int current_led_state = 0;         /* Current LED state:
                                            0 = all off
                                            1-3 = individual LEDs
                                            4 = all on */

/* Function prototypes for file operations */
static int button_open(struct inode *, struct file *);
static int button_release(struct inode *, struct file *);
static ssize_t button_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t button_write(struct file *, const char __user *, size_t, loff_t *);

/* File operations structure */
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = button_open,
    .release = button_release,
    .read = button_read,
    .write = button_write
};

/* 
 * Turn off all connected LEDs
 * Called during initialization and state changes
 */
static void turn_off_all_leds(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (led_gpios[i]) {
            gpiod_set_value(led_gpios[i], 0);
        }
    }
    pr_info("All LEDs turned OFF\n");
}

/*
 * Turn on all connected LEDs
 * Called when button is pressed 4 times
 */
static void turn_on_all_leds(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (led_gpios[i]) {
            gpiod_set_value(led_gpios[i], 1);
        }
    }
    pr_info("All LEDs turned ON\n");
}

/*
 * Control specific LED
 * @led_index: Index of LED to control (0-2)
 * Turns off all LEDs then turns on specified LED
 */
static void control_led(int led_index)
{
    if (led_index >= 0 && led_index < 3 && led_gpios[led_index]) {
        /* Turn off all LEDs first */
        turn_off_all_leds();
        /* Turn on specific LED */
        gpiod_set_value(led_gpios[led_index], 1);
        pr_info("LED %d turned ON, others OFF\n", led_index);
    }
}

/*
 * Work queue handler for processing button presses
 * Called after button press timeout or 5 presses
 * Controls LEDs based on number of presses:
 * 1 press = Green LED
 * 2 presses = White LED  
 * 3 presses = Yellow LED
 * 4 presses = All LEDs on
 * 5+ presses = All LEDs off
 */
static void button_work_handler(struct work_struct *work)
{
    pr_info("Processing %d button presses\n", press_count);
    
    switch (press_count) {
        case 1:
            current_led_state = 1;
            control_led(0); /* LED 0 (green) */
            break;
        case 2:
            current_led_state = 2;
            control_led(1); /* LED 1 (white) */
            break;
        case 3:
            current_led_state = 3;
            control_led(2); /* LED 2 (yellow) */
            break;
        case 4:
            current_led_state = 4;
            turn_on_all_leds(); /* All LEDs on */
            break;
        case 5:
        default:
            current_led_state = 0;
            turn_off_all_leds(); /* All LEDs off */
            break;
    }
    
    /* Reset press count after processing */
    press_count = 0;
}

/*
 * Timer callback for multi-press timeout
 * Called after MULTI_PRESS_TIMEOUT_MS if no more presses
 */
static void press_timer_callback(struct timer_list *timer)
{
    if (press_count > 0) {
        /* Schedule work to process the button presses */
        schedule_work(&button_work);
    }
}

/*
 * IRQ handler for button press
 * Implements debouncing and tracks press count
 * Schedules work immediately on 5 presses
 */
static irqreturn_t button_irq_handler(int irq, void *dev_id)
{
    unsigned long current_time = jiffies;
    static unsigned long last_irq_time = 0;
    
    /* Simple debouncing */
    if (time_before(current_time, last_irq_time + msecs_to_jiffies(DEBOUNCE_TIME_MS))) {
        return IRQ_HANDLED;
    }
    last_irq_time = current_time;
    
    button_pressed = true;
    press_count++;
    
    pr_info("Button pressed! Count: %d\n", press_count);
    
    /* Reset or start the timer for multi-press detection */
    mod_timer(&press_timer, jiffies + msecs_to_jiffies(MULTI_PRESS_TIMEOUT_MS));
    
    /* If we reach 5 presses, process immediately */
    if (press_count >= 5) {
        del_timer(&press_timer);
        schedule_work(&button_work);
    }
    
    return IRQ_HANDLED;
}

/* File operation implementations */

/*
 * Called when device file is opened
 */
static int button_open(struct inode *inode, struct file *file)
{
    pr_info("Button device opened\n");
    return 0;
}

/*
 * Called when device file is closed
 */
static int button_release(struct inode *inode, struct file *file)
{
    pr_info("Button device closed\n");
    return 0;
}

/*
 * Read implementation - returns button and LED status
 * Returns:
 * - Button pressed/released state
 * - Press count
 * - Current LED state
 */
static ssize_t button_read(struct file *file, char __user *buffer, size_t len, loff_t *offset)
{
    char status_msg[200];
    int msg_len;
    const char *led_status;
    
    if (*offset != 0)
        return 0;
    
    switch (current_led_state) {
        case 0: led_status = "All LEDs OFF"; break;
        case 1: led_status = "LED 0 (Green) ON"; break;
        case 2: led_status = "LED 1 (White) ON"; break;
        case 3: led_status = "LED 2 (Yellow) ON"; break;
        case 4: led_status = "All LEDs ON"; break;
        default: led_status = "Unknown state"; break;
    }
    
    msg_len = snprintf(status_msg, sizeof(status_msg), "Button Status: %s\nPress Count: %d\nCurrent State: %s\n", button_pressed ? "Pressed" : "Released", press_count, led_status);
    
    if (len < msg_len)
        return -EINVAL;
    
    if (copy_to_user(buffer, status_msg, msg_len))
        return -EFAULT;
    
    *offset += msg_len;
    button_pressed = false; /* Reset after read */
    return msg_len;
}

/*
 * Write implementation - accepts commands:
 * 'r' - Reset all states
 * 's' - Print status to kernel log
 */
static ssize_t button_write(struct file *file, const char __user *buffer, size_t len, loff_t *off)
{
    char cmd;
    
    if (len < 1 || copy_from_user(&cmd, buffer, 1))
        return -EFAULT;
    
    switch (cmd) {
        case 'r': /* Reset */
            press_count = 0;
            current_led_state = 0;
            turn_off_all_leds();
            pr_info("Button driver reset\n");
            break;
        case 's': /* Status */
            pr_info("Current LED state: %d, Press count: %d\n", current_led_state, press_count);
            break;
        default:
            return -EINVAL;
    }
    
    return len;
}



static int button_probe(struct platform_device *pdev)
{
    int ret, i;
    struct device *dev = &pdev->dev;
    
    pr_info("Button driver probe started\n");
    
    /* Get button GPIO */
    button_gpio = devm_gpiod_get(dev, "button", GPIOD_IN);
    if (IS_ERR(button_gpio)) {
        dev_err(dev, "Failed to get button GPIO\n");
        return PTR_ERR(button_gpio);
    }
    
    /* Get LED GPIOs from led_driver */
    for (i = 0; i < 3; i++) {
        led_gpios[i] = led_get_gpio(i);
        if (!led_gpios[i]) {
            dev_err(dev, "Failed to get LED GPIO %d from led_driver\n", i);
            return -ENODEV;
        }
        pr_info("Got LED GPIO %d from led_driver\n", i);
    }
    
    /* Setup IRQ */
    button_irq = gpiod_to_irq(button_gpio);
    if (button_irq < 0) {
        dev_err(dev, "Failed to get IRQ for button GPIO\n");
        return button_irq;
    }
    
    ret = devm_request_irq(dev, button_irq, button_irq_handler,
                          IRQF_TRIGGER_FALLING,
                          "button_irq", NULL);
    if (ret) {
        dev_err(dev, "Failed to request IRQ\n");
        return ret;
    }
    
    /* Initialize timer and work queue */
    timer_setup(&press_timer, press_timer_callback, 0);
    INIT_WORK(&button_work, button_work_handler);
    
    /* Create character device */
    ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        dev_err(dev, "Failed to allocate char device region\n");
        return ret;
    }
    
    dev_class = class_create(DEVICE_CLASS);
    if (IS_ERR(dev_class)) {
        dev_err(dev, "Failed to create device class\n");
        ret = PTR_ERR(dev_class);
        goto cleanup_chrdev;
    }
    
    cdev_init(&button_cdev, &fops);
    button_cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&button_cdev, dev_number, 1);
    if (ret < 0) {
        dev_err(dev, "Failed to add cdev\n");
        goto cleanup_class;
    }
    
    button_device = device_create(dev_class, NULL, dev_number, NULL, DEVICE_NAME);
    if (IS_ERR(button_device)) {
        dev_err(dev, "Failed to create device\n");
        ret = PTR_ERR(button_device);
        goto cleanup_cdev;
    }
    
    /* Initialize LED state (all off) */
    turn_off_all_leds();
    
    pr_info("Button driver probe completed successfully\n");
    pr_info("Created device /dev/%s\n", DEVICE_NAME);
    
    return 0;
    
cleanup_cdev:
    cdev_del(&button_cdev);
cleanup_class:
    class_destroy(dev_class);
cleanup_chrdev:
    unregister_chrdev_region(dev_number, 1);
    return ret;
}

/*
 * Remove function - called when device is removed
 * Cleans up all resources
 */
static void button_remove(struct platform_device *pdev)
{
    pr_info("Button driver remove started\n");
    
    /* Clean up timer and work */
    del_timer_sync(&press_timer);
    cancel_work_sync(&button_work);
    
    /* Turn off all LEDs before removing */
    turn_off_all_leds();
    
    /* Clean up character device */
    device_destroy(dev_class, dev_number);
    cdev_del(&button_cdev);
    class_destroy(dev_class);
    unregister_chrdev_region(dev_number, 1);
    
    pr_info("Button driver removed successfully\n");
}


static const struct of_device_id button_of_match[] = {
    { .compatible = "custom,gpio-button" },
    { },    
};

MODULE_DEVICE_TABLE(of, button_of_match);


static struct platform_driver button_driver = {
    .probe = button_probe,
    .remove_new = button_remove,
    .driver = {
        .name = "button_driver",
        .of_match_table = button_of_match,
    },
};


module_platform_driver(button_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AnhPH58");
MODULE_DESCRIPTION("GPIO Button driver with LED control");
