#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>

// Device paths
#define LED_DEVICE "/dev/gpio_led"
#define BUTTON_DEVICE "/dev/gpio_button"

// IOCTL commands for LED
#define LED_IOC_MAGIC 'l'
#define LED_IOC_ON     _IO(LED_IOC_MAGIC, 1)
#define LED_IOC_OFF    _IO(LED_IOC_MAGIC, 2)
#define LED_IOC_TOGGLE _IO(LED_IOC_MAGIC, 3)
#define LED_IOC_STATUS _IOR(LED_IOC_MAGIC, 4, int)

// IOCTL commands for Button
#define BUTTON_IOC_MAGIC 'b'
#define BUTTON_IOC_GET_STATUS _IOR(BUTTON_IOC_MAGIC, 1, int)

static int led_fd = -1;
static int button_fd = -1;
static int running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
    printf("\nReceived signal, exiting...\n");
}

int open_devices(void) {
    led_fd = open(LED_DEVICE, O_RDWR);
    if (led_fd < 0) {
        fprintf(stderr, "Failed to open LED device %s: %s\n", LED_DEVICE, strerror(errno));
        return -1;
    }
    
    button_fd = open(BUTTON_DEVICE, O_RDWR);
    if (button_fd < 0) {
        fprintf(stderr, "Failed to open Button device %s: %s\n", BUTTON_DEVICE, strerror(errno));
        close(led_fd);
        led_fd = -1;
        return -1;
    }
    
    printf("Successfully opened devices:\n");
    printf("  LED device: %s (fd=%d)\n", LED_DEVICE, led_fd);
    printf("  Button device: %s (fd=%d)\n", BUTTON_DEVICE, button_fd);
    
    return 0;
}

void close_devices(void) {
    if (led_fd >= 0) {
        close(led_fd);
        led_fd = -1;
    }
    if (button_fd >= 0) {
        close(button_fd);
        button_fd = -1;
    }
    printf("Devices closed.\n");
}

int led_control_write(const char* command) {
    if (led_fd < 0) {
        fprintf(stderr, "LED device not open\n");
        return -1;
    }
    
    ssize_t bytes_written = write(led_fd, command, strlen(command));
    if (bytes_written < 0) {
        perror("LED write failed");
        return -1;
    }
    
    return 0;
}

int led_control_ioctl(const char* command) {
    if (led_fd < 0) {
        fprintf(stderr, "LED device not open\n");
        return -1;
    }
    
    int result = 0;
    
    if (strcmp(command, "on") == 0 || strcmp(command, "1") == 0) {
        result = ioctl(led_fd, LED_IOC_ON);
    } else if (strcmp(command, "off") == 0 || strcmp(command, "0") == 0) {
        result = ioctl(led_fd, LED_IOC_OFF);
    } else if (strcmp(command, "toggle") == 0) {
        result = ioctl(led_fd, LED_IOC_TOGGLE);
    } else {
        fprintf(stderr, "Invalid LED command: %s\n", command);
        return -1;
    }
    
    if (result < 0) {
        perror("LED ioctl failed");
        return -1;
    }
    
    return 0;
}

int get_led_status(void) {
    int status;
    if (led_fd < 0) {
        fprintf(stderr, "LED device not open\n");
        return -1;
    }
    
    if (ioctl(led_fd, LED_IOC_STATUS, &status) < 0) {
        perror("Failed to get LED status");
        return -1;
    }
    return status;
}

int get_button_status(void) {
    int status;
    if (button_fd < 0) {
        fprintf(stderr, "Button device not open\n");
        return -1;
    }
    
    if (ioctl(button_fd, BUTTON_IOC_GET_STATUS, &status) < 0) {
        perror("Failed to get button status");
        return -1;
    }
    return status;
}

int read_button_device(void) {
    char buffer[64];
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
    printf("Button device read: %s", buffer);
    return 0;
}

int read_led_device(void) {
    char buffer[64];
    if (led_fd < 0) {
        fprintf(stderr, "LED device not open\n");
        return -1;
    }
    
    ssize_t bytes_read = read(led_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        perror("Failed to read LED device");
        return -1;
    }
    
    buffer[bytes_read] = '\0';
    printf("LED device read: %s", buffer);
    return 0;
}

void print_status(void) {
    int led_status = get_led_status();
    int button_status = get_button_status();
    
    if (led_status >= 0 && button_status >= 0) {
        printf("Status: LED=%s, Button=%s\n",
               (led_status == 1) ? "ON" : "OFF",
               (button_status == 1) ? "PRESSED" : "RELEASED");
    } else {
        printf("Failed to read device status\n");
    }
}

void print_help(void) {
    printf("\n=== GPIO Control Application (Separate Devices) ===\n");
    printf("Available commands:\n");
    printf("  LED Control:\n");
    printf("    on/1      - Turn LED on (via IOCTL)\n");
    printf("    off/0     - Turn LED off (via IOCTL)\n");  
    printf("    toggle    - Toggle LED state (via IOCTL)\n");
    printf("    write_on  - Turn LED on (via write)\n");
    printf("    write_off - Turn LED off (via write)\n");
    printf("    write_toggle - Toggle LED (via write)\n");
    printf("  \n");
    printf("  Status & Info:\n");
    printf("    status    - Show current GPIO status\n");
    printf("    read_led  - Read from LED device\n");
    printf("    read_btn  - Read from button device\n");
    printf("  \n");
    printf("  General:\n");
    printf("    help      - Show this help\n");
    printf("    quit/exit - Exit application\n");
    printf("===============================================\n\n");
}

void test_all_functions(void) {
    printf("\n=== Testing All Functions ===\n");
    
    printf("1. Reading initial status:\n");
    print_status();
    
    printf("\n2. Testing LED control via IOCTL:\n");
    printf("   Turning LED ON...\n");
    led_control_ioctl("on");
    sleep(1);
    print_status();
    
    printf("   Turning LED OFF...\n");
    led_control_ioctl("off");
    sleep(1);
    print_status();
    
    printf("   Toggling LED...\n");
    led_control_ioctl("toggle");
    sleep(1);
    print_status();
    
    printf("\n3. Testing LED control via write:\n");
    printf("   Writing 'on' to LED device...\n");
    led_control_write("on");
    sleep(1);
    print_status();
    
    printf("   Writing 'off' to LED device...\n");
    led_control_write("off");
    sleep(1);
    print_status();
    
    printf("\n4. Reading from devices:\n");
    read_led_device();
    read_button_device();
    
    printf("\n=== Test Complete ===\n\n");
}

int main(int argc, char *argv[]) {
    char command[256];
    
    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("GPIO Control Application for Separate Devices\n");
    printf("==============================================\n");
    
    // Open devices
    if (open_devices() < 0) {
        fprintf(stderr, "Failed to open devices. Make sure drivers are loaded.\n");
        return 1;
    }
    
    // Command line mode
    if (argc > 1) {
        if (strcmp(argv[1], "test") == 0) {
            test_all_functions();
        } else if (strcmp(argv[1], "status") == 0) {
            print_status();
        } else if (strncmp(argv[1], "write_", 6) == 0) {
            // Handle write commands
            const char* write_cmd = argv[1] + 6;  // Skip "write_" prefix
            if (led_control_write(write_cmd) == 0) {
                printf("LED write command '%s' executed\n", write_cmd);
                print_status();
            }
        } else {
            // Handle IOCTL commands
            if (led_control_ioctl(argv[1]) == 0) {
                printf("LED IOCTL command '%s' executed\n", argv[1]);
                print_status();
            }
        }
        close_devices();
        return 0;
    }
    
    // Interactive mode
    printf("Entering interactive mode. Type 'help' for commands.\n");
    print_status();
    
    while (running) {
        printf("gpio> ");
        fflush(stdout);
        
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }
        
        // Remove newline
        command[strcspn(command, "\n")] = 0;
        
        if (strlen(command) == 0) {
            continue;
        }
        
        // Process commands
        if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
            break;
        } else if (strcmp(command, "help") == 0) {
            print_help();
        } else if (strcmp(command, "status") == 0) {
            print_status();
        } else if (strcmp(command, "read_led") == 0) {
            read_led_device();
        } else if (strcmp(command, "read_btn") == 0) {
            read_button_device();
        } else if (strcmp(command, "test") == 0) {
            test_all_functions();
        } else if (strncmp(command, "write_", 6) == 0) {
            // Handle write commands
            const char* write_cmd = command + 6;  // Skip "write_" prefix
            if (led_control_write(write_cmd) == 0) {
                printf("LED write command executed\n");
                print_status();
            }
        } else if (strcmp(command, "on") == 0 || strcmp(command, "1") == 0 ||
                   strcmp(command, "off") == 0 || strcmp(command, "0") == 0 ||
                   strcmp(command, "toggle") == 0) {
            // Handle IOCTL commands
            if (led_control_ioctl(command) == 0) {
                printf("LED IOCTL command executed\n");
                print_status();
            }
        } else {
            printf("Unknown command '%s'. Type 'help' for available commands.\n", command);
        }
    }
    
    printf("\nExiting application...\n");
    close_devices();
    return 0;
}
