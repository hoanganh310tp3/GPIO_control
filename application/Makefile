CROSS_COMPILE ?= arm-linux-gnueabihf-
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -O2 -static

TARGET = gpio_app
SOURCE = gpio_app.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	@echo "Cross-compiling application..."
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

clean:
	rm -f $(TARGET)

.PHONY: all clean
