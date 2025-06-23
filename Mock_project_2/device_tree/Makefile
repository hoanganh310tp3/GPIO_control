# Device tree compiler
DTC := dtc

all: gpio-overlay.dtbo

gpio-overlay.dtbo: gpio-overlay.dts
	$(DTC) -@ -I dts -O dtb -o gpio-overlay.dtbo gpio-overlay.dts

clean:
	rm -f gpio-overlay.dtbo

.PHONY: all clean