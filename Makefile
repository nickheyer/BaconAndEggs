PROJECT      = bacon_and_eggs
BUILD_DIR    = build

PICO_SDK_PATH       ?= /usr/share/pico-sdk
CMAKE               ?= cmake
NINJA               ?= ninja
PICOTOOL            ?= picotool

SERIAL_PORT ?= /dev/ttyACM0
SERIAL_BAUD ?= 115200

DEFAULT_AUTOWAKE ?= 0

-include .env

export PICO_SDK_PATH

.PHONY: all build flash serial clean

all: build

build: clean
	$(CMAKE) -B $(BUILD_DIR) -G Ninja \
		-DWIFI_SSID="$(WIFI_SSID)" \
		-DWIFI_PASSWORD="$(WIFI_PASSWORD)" \
		-DDEFAULT_SERVERS="$(subst ;,|,$(DEFAULT_SERVERS))" \
		-DDEFAULT_AUTOWAKE=$(DEFAULT_AUTOWAKE)
	$(NINJA) -C $(BUILD_DIR)

flash: build
	$(PICOTOOL) load $(BUILD_DIR)/$(PROJECT).uf2 -fx

serial:
	screen $(SERIAL_PORT) $(SERIAL_BAUD)

clean:
	rm -rf $(BUILD_DIR)
