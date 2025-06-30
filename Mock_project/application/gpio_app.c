
#include <stdio.h>      /* For standard I/O operations */
#include <stdlib.h>     /* For general utilities like atoi() */
#include <string.h>     /* For string manipulation functions */
#include <unistd.h>     
#include <fcntl.h>      /* For file control options */
#include <sys/ioctl.h>  /* For device control operations */
#include <signal.h>     /* For signal handling */
#include <errno.h>      /* For error number definitions */

/* Device paths for accessing LED and button devices */
#define LED_DEVICE_BASE     "/dev/gpio_led"    /* Base path for LED devices */
#define BUTTON_DEVICE       "/dev/gpio_button"  /* Path for button device */
#define NUM_LEDS           3                    /* Total number of LEDs */

/* IOCTL command definitions for LED control */
#define GPIO_IOC_MAGIC      'k'                /* Magic number for LED IOCTL */
#define GPIO_IOC_LED_ON     _IO(GPIO_IOC_MAGIC, 1)    /* Turn LED on */
#define GPIO_IOC_LED_OFF    _IO(GPIO_IOC_MAGIC, 2)    /* Turn LED off */
#define GPIO_IOC_LED_TOGGLE _IO(GPIO_IOC_MAGIC, 3)    /* Toggle LED state */
#define GPIO_IOC_GET_STATUS _IOR(GPIO_IOC_MAGIC, 4, int) /* Get LED status */

/* IOCTL command definitions for Button control */
#define BUTTON_IOC_MAGIC     'b'               /* Magic number for Button IOCTL */
#define BUTTON_IOC_GET_STATUS _IOR(BUTTON_IOC_MAGIC, 1, int) /* Get button status */

/* Array of LED names for display purposes */
static const char* led_names[] = {
    "green_led",    /* Index 0: Green LED */
    "white_led",    /* Index 1: White LED */
    "yellow_led"    /* Index 2: Yellow LED */
};

/* Global variables for device file descriptors and program state */
static int led_fds[NUM_LEDS] = {-1, -1, -1};  /* File descriptors for LED devices */
static int button_fd = -1;                     /* File descriptor for button device */
static int running = 1;                        /* Program running flag */

/*
 * Signal handler for graceful program termination
 * @sig: Signal number received
 */
void signal_handler(int sig) {
    (void)sig;  /* Suppress unused parameter warning */
    running = 0;
    printf("\nExiting...\n");
}

/*
 * Opens all LED devices and button device
 * Returns: 0 on success, -1 on failure
 */
int open_devices(void) {
    char device_path[64];
    int i;
    
    /* Open each LED device */
    for (i = 0; i < NUM_LEDS; i++) {
        snprintf(device_path, sizeof(device_path), "%s%d", LED_DEVICE_BASE, i);
        led_fds[i] = open(device_path, O_RDWR);
        if (led_fds[i] < 0) {
            fprintf(stderr, "Failed to open %s: %s\n", device_path, strerror(errno));
            goto cleanup;
        }
    }
    
    /* Open button device */
    button_fd = open(BUTTON_DEVICE, O_RDWR);
    if (button_fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", BUTTON_DEVICE, strerror(errno));
        goto cleanup;
    }
    
    return 0;

cleanup:
    /* Close any successfully opened devices on error */
    for (i = 0; i < NUM_LEDS; i++) {
        if (led_fds[i] >= 0) {
            close(led_fds[i]);
            led_fds[i] = -1;
        }
    }
    return -1;
}

/*
 * Closes all opened devices
 */
void close_devices(void) {
    int i;
    
    /* Close LED devices */
    for (i = 0; i < NUM_LEDS; i++) {
        if (led_fds[i] >= 0) {
            close(led_fds[i]);
            led_fds[i] = -1;
        }
    }
    
    /* Close button device */
    if (button_fd >= 0) {
        close(button_fd);
        button_fd = -1;
    }
}

/*
 * Controls individual LED state
 * @led_index: Index of LED to control (0-2)
 * @command: Command string ("on", "off", or "toggle")
 * Returns: 0 on success, -1 on failure
 */
int led_control(int led_index, const char* command) {
    if (led_index < 0 || led_index >= NUM_LEDS || led_fds[led_index] < 0) {
        fprintf(stderr, "Invalid LED index %d\n", led_index);
        return -1;
    }
    
    int result = 0;
    
    /* Execute requested command */
    if (strcmp(command, "on") == 0) {
        result = ioctl(led_fds[led_index], GPIO_IOC_LED_ON);
    } else if (strcmp(command, "off") == 0) {
        result = ioctl(led_fds[led_index], GPIO_IOC_LED_OFF);
    } else if (strcmp(command, "toggle") == 0) {
        result = ioctl(led_fds[led_index], GPIO_IOC_LED_TOGGLE);
    } else {
        fprintf(stderr, "Invalid command: %s\n", command);
        return -1;
    }
    
    if (result < 0) {
        perror("LED control failed");
        return -1;
    }
    
    return 0;
}

/*
 * Controls all LEDs simultaneously
 * @command: Command to execute ("on", "off", or "toggle")
 * Returns: 0 if all LEDs controlled successfully, -1 if any failed
 */
int all_leds_control(const char* command) {
    int i;
    int success = 0;
    
    printf("Controlling all LEDs: %s\n", command);
    
    /* Apply command to each LED */
    for (i = 0; i < NUM_LEDS; i++) {
        if (led_control(i, command) == 0) {
            success++;
        }
    }
    
    if (success == NUM_LEDS) {
        printf("All LEDs %s successfully\n", command);
        return 0;
    } else {
        printf("Only %d/%d LEDs controlled successfully\n", success, NUM_LEDS);
        return -1;
    }
}

/*
 * Gets the current status of an LED
 * @led_index: Index of LED to check (0-2)
 * Returns: 1 if LED is on, 0 if off, -1 on error
 */
int get_led_status(int led_index) {
    int status;
    
    if (led_index < 0 || led_index >= NUM_LEDS || led_fds[led_index] < 0) {
        return -1;
    }
    
    if (ioctl(led_fds[led_index], GPIO_IOC_GET_STATUS, &status) < 0) {
        return -1;
    }
    
    return status;
}

/*
 * Gets the current button status
 * Returns: 1 if button is pressed, 0 if released, -1 on error
 */
int get_button_status(void) {
    int status;
    
    if (button_fd < 0) {
        return -1;
    }
    
    if (ioctl(button_fd, BUTTON_IOC_GET_STATUS, &status) < 0) {
        return -1;
    }
    
    return status;
}

/*
 * Reads and displays detailed button device information
 * Returns: 0 on success, -1 on failure
 */
int read_button_device(void) {
    char buffer[256];
    
    if (button_fd < 0) {
        fprintf(stderr, "Button device not open\n");
        return -1;
    }
    
    ssize_t bytes_read = read(button_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        perror("Failed to read button device");
        return -1;
    }
    
    buffer[bytes_read] = '\0';
    printf("Button Status:\n%s", buffer);
    
    return 0;
}

/*
 * Prints comprehensive status of all LEDs and button
 */
void print_status(void) {
    int i;
    
    /* Display LED Status */
    printf("=== LED Status ===\n");
    for (i = 0; i < NUM_LEDS; i++) {
        int status = get_led_status(i);
        printf("  LED%d (%s): %s\n", i, led_names[i], 
               (status == 1) ? "ON" : "OFF");
    }
    
    /* Display Button Status */
    printf("\n=== Button Status ===\n");
    int button_status = get_button_status();
    if (button_status >= 0) {
        printf("  Button: %s\n", (button_status == 1) ? "PRESSED" : "RELEASED");
    } else {
        printf("  Button: ERROR\n");
    }
    
    /* Display Detailed Button Information */
    printf("\n=== Detailed Button Info ===\n");
    read_button_device();
    printf("========================\n");
}

/*
 * Main program entry point
 * Supports commands:
 * - led <index> <command>: Control specific LED
 * - all <command>: Control all LEDs
 * - status: Show system status
 * - button: Show button status
 */
int main(int argc, char *argv[]) {
    /* Set up signal handlers for graceful termination */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Initialize devices */
    if (open_devices() < 0) {
        fprintf(stderr, "Failed to open devices. Make sure drivers are loaded.\n");
        return 1;
    }
    
    /* Parse and execute commands */
    if (argc == 4 && strcmp(argv[1], "led") == 0) {
        /* Control specific LED: ./gpio_app led 0 on */
        int led_index = atoi(argv[2]);
        if (led_control(led_index, argv[3]) == 0) {
            printf("LED%d (%s) %s\n", led_index, led_names[led_index], argv[3]);
            print_status();
        }
    } else if (argc == 3 && strcmp(argv[1], "all") == 0) {
        /* Control all LEDs: ./gpio_app all on */
        if (all_leds_control(argv[2]) == 0) {
            print_status();
        }
    } else if (argc == 2 && strcmp(argv[1], "status") == 0) {
        /* Show all status: ./gpio_app status */
        print_status();
    } else if (argc == 2 && strcmp(argv[1], "button") == 0) {
        /* Show button status: ./gpio_app button */
        printf("=== Button Status ===\n");
        read_button_device();
        printf("====================\n");
    } else {
        fprintf(stderr, "Invalid command. Check documentation for usage.\n");
        close_devices();
        return 1;
    }
    
    /* Cleanup and exit */
    close_devices();
    return 0;
}
