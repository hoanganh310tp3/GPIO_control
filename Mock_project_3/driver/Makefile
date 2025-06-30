# Kernel build directory - Buildroot kernel
KERNEL_DIR := /home/hoanganhpham/Downloads/buildroot/output/build/linux-custom

# Cross-compilation settings
ARCH := arm
CROSS_COMPILE := arm-linux-gnueabihf-

# Module name
obj-m := gpio_driver_2.o

# Build targets
all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
	rm -f *.ko *.o *.mod.c *.mod *.symvers *.order

.PHONY: all clean