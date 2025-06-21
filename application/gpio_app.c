#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#define DEVICE_PATH "/dev/gpio_ctl"
#define BUFFER_SIZE 256

static int device_fd = -1;
static int running = 1;

void signal_handler(int sig) {
    running = 0;
    printf("\nShutting down...\n");
}

void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -i, --interactive  Interactive mode\n");
    printf("  -1             Turn LED ON\n");
    printf("  -0             Turn LED OFF\n");
    printf("  -s, --status   Read GPIO status\n");
    printf("  -m, --monitor  Monitor mode (continuous status)\n");
}

int open_device() {
    device_fd = open(DEVICE_PATH, O_RDWR);
    if (device_fd < 0) {
        perror("Failed to open device");
        return -1;
    }
    return 0;
}

void close_device() {
    if (device_fd >= 0) {
        close(device_fd);
        device_fd = -1;
    }
}

int read_status() {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    if (device_fd < 0) return -1;
    
    lseek(device_fd, 0, SEEK_SET);
    bytes_read = read(device_fd, buffer, sizeof(buffer) - 1);
    
    if (bytes_read < 0) {
        perror("Failed to read from device");
        return -1;
    }
    
    buffer[bytes_read] = '\0';
    printf("%s", buffer);
    return 0;
}

int send_command(const char *command) {
    ssize_t bytes_written;
    
    if (device_fd < 0) return -1;
    
    bytes_written = write(device_fd, command, strlen(command));
    if (bytes_written < 0) {
        perror("Failed to write to device");
        return -1;
    }
    
    printf("Command '%s' sent successfully\n", command);
    return 0;
}

void interactive_mode() {
    char input[10];
    
    printf("=== GPIO Control Interactive Mode ===\n");
    printf("Commands: 1 (LED ON), 0 (LED OFF), s (status), q (quit)\n");
    
    while (running) {
        printf("gpio> ");
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        
        input[strcspn(input, "\n")] = '\0';
        
        if (strcmp(input, "q") == 0 || strcmp(input, "quit") == 0) {
            break;
        } else if (strcmp(input, "1") == 0) {
            send_command("1");
        } else if (strcmp(input, "0") == 0) {
            send_command("0");
        } else if (strcmp(input, "s") == 0 || strcmp(input, "status") == 0) {
            read_status();
        } else if (strlen(input) > 0) {
            printf("Unknown command: %s\n", input);
        }
    }
}

void monitor_mode() {
    printf("=== GPIO Monitor Mode (Press Ctrl+C to exit) ===\n");
    
    while (running) {
        printf("\033[2J\033[H"); // Clear screen
        printf("GPIO Status:\n");
        printf("============\n");
        read_status();
        sleep(1);
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    if (open_device() < 0) {
        fprintf(stderr, "Error: Cannot open device %s\n", DEVICE_PATH);
        fprintf(stderr, "Make sure the gpio_driver module is loaded.\n");
        return 1;
    }
    
    if (argc == 1) {
        read_status();
    } else if (argc == 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
        } else if (strcmp(argv[1], "-i") == 0 || strcmp(argv[1], "--interactive") == 0) {
            interactive_mode();
        } else if (strcmp(argv[1], "-1") == 0) {
            send_command("1");
        } else if (strcmp(argv[1], "-0") == 0) {
            send_command("0");
        } else if (strcmp(argv[1], "-s") == 0 || strcmp(argv[1], "--status") == 0) {
            read_status();
        } else if (strcmp(argv[1], "-m") == 0 || strcmp(argv[1], "--monitor") == 0) {
            monitor_mode();
        } else {
            printf("Unknown option: %s\n", argv[1]);
            print_usage(argv[0]);
            close_device();
            return 1;
        }
    } else {
        print_usage(argv[0]);
        close_device();
        return 1;
    }
    
    close_device();
    return 0;
}
