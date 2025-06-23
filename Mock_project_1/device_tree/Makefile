# Device Tree Compiler
DTC ?= dtc

# Device tree overlay files
DTBO_FILES = gpio-overlay.dtbo

all: $(DTBO_FILES)

%.dtbo: %.dts
	@echo "Compiling device tree overlay: $<"
	$(DTC) -@ -I dts -O dtb -o $@ $<

clean:
	rm -f *.dtbo

.PHONY: all clean