obj-m += gpio_driver.o

# Buildroot toolchain settings
BUILDROOT_DIR ?= /home/hoanganhpham/Downloads/buildroot
CROSS_COMPILE ?= arm-linux-gnueabihf-
ARCH ?= arm
KERNEL_DIR ?= $(shell find $(BUILDROOT_DIR)/output/build -name "linux-*" -type d | head -1)
M := $(shell pwd)

all:
	@echo "Building GPIO driver with Buildroot toolchain..."
	@echo "  BUILDROOT_DIR: $(BUILDROOT_DIR)"
	@echo "  KERNEL_DIR:    $(KERNEL_DIR)"
	@echo "  CROSS_COMPILE: $(CROSS_COMPILE)"
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_DIR) M=$(M) modules

clean:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_DIR) M=$(M) clean

# Copy compiled module to Pi
deploy: gpio_driver.ko
	@echo "Deploying to Raspberry Pi..."
	@read -p "Enter Pi IP address: " PI_IP; \
	scp gpio_driver.ko root@$$PI_IP:~/; \
	echo "Module copied to Pi at ~/gpio_driver.ko"

# Install on Pi via SSH
install-remote: deploy
	@read -p "Enter Pi IP address: " PI_IP; \
	ssh root@$$PI_IP "insmod ~/gpio_driver.ko && echo 'Driver loaded successfully'"

.PHONY: all clean deploy install-remote
