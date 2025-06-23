obj-m := led_driver.o button_driver.o

# Buildroot toolchain settings
BUILDROOT_DIR ?= /home/hoanganhpham/Downloads/buildroot
CROSS_COMPILE ?= arm-linux-gnueabihf-
ARCH ?= arm
KERNEL_DIR ?= $(shell find $(BUILDROOT_DIR)/output/build -name "linux-*" -type d | head -1)
M := $(shell pwd)

all:
	
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_DIR) M=$(M) modules

clean:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_DIR) M=$(M) clean


.PHONY: all clean 
