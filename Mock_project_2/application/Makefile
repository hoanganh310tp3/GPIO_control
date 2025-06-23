# Cross compiler
CC := /home/hoanganhpham/Mock_project/buildroot/output/host/bin/arm-buildroot-linux-gnueabihf-gcc

all: gpio_app

gpio_app: gpio_app.c
	$(CC) -o gpio_app gpio_app.c

clean:
	rm -f gpio_app

.PHONY: all clean