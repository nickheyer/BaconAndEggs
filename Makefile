PROJECT      = bacon_and_eggs
BUILD_DIR    = build

PICO_SDK_PATH       ?= $(HOME)/.pico-sdk/sdk/2.2.0
PICO_TOOLCHAIN_PATH ?= $(HOME)/.pico-sdk/toolchain/14_2_Rel1
CMAKE               ?= $(HOME)/.pico-sdk/cmake/v3.31.5/bin/cmake
NINJA               ?= $(HOME)/.pico-sdk/ninja/v1.12.1/ninja
PICOTOOL            ?= $(HOME)/.pico-sdk/picotool/2.2.0-a4/picotool/picotool

SERIAL_PORT ?= /dev/tty.usbmodem141101
SERIAL_BAUD ?= 115200

DEFAULT_AUTOWAKE ?= 0

-include .env

export PICO_SDK_PATH
export PICO_TOOLCHAIN_PATH

.PHONY: all build flash serial clean

all: build

build:
	$(CMAKE) -B $(BUILD_DIR) -G Ninja \
		-DWIFI_SSID="$(WIFI_SSID)" \
		-DWIFI_PASSWORD="$(WIFI_PASSWORD)" \
		-DDEFAULT_SERVERS="$(DEFAULT_SERVERS)" \
		-DDEFAULT_AUTOWAKE=$(DEFAULT_AUTOWAKE)
	$(NINJA) -C $(BUILD_DIR)

flash: build
	$(PICOTOOL) load $(BUILD_DIR)/$(PROJECT).uf2 -fx

serial:
	screen $(SERIAL_PORT) $(SERIAL_BAUD)

clean:
	rm -rf $(BUILD_DIR)
