PROJECT      = bacon_and_eggs
BUILD_DIR    = build
GEN_DIR		 = gen

PICO_SDK_PATH       ?= /usr/share/pico-sdk
CMAKE               ?= cmake
NINJA               ?= ninja
PICOTOOL            ?= picotool

SERIAL_PORT ?= /dev/ttyACM0
SERIAL_BAUD ?= 115200

DEFAULT_AUTOWAKE    ?= 0
DEFAULT_UTC_OFFSET  ?= 0
DEFAULT_NTP_SERVER  ?= pool.ntp.org
DEFAULT_MQTT_HOST   ?=
DEFAULT_MQTT_PORT   ?= 1883
DEFAULT_MQTT_USER   ?=
DEFAULT_MQTT_PASS   ?=
DEFAULT_WEBHOOK_URL ?=

-include .env

export PICO_SDK_PATH

.PHONY: all build flash serial clean fsdata

all: build

fsdata:
	python3 tools/makefsdata.py web $(GEN_DIR)/fsdata_custom.c

build: clean fsdata
	$(CMAKE) -B $(BUILD_DIR) -G Ninja \
		-DWIFI_SSID="$(WIFI_SSID)" \
		-DWIFI_PASSWORD="$(WIFI_PASSWORD)" \
		-DDEFAULT_SERVERS="$(subst ;,|,$(DEFAULT_SERVERS))" \
		-DDEFAULT_AUTOWAKE=$(DEFAULT_AUTOWAKE) \
		-DDEFAULT_UTC_OFFSET=$(DEFAULT_UTC_OFFSET) \
		-DDEFAULT_NTP_SERVER="$(DEFAULT_NTP_SERVER)" \
		-DDEFAULT_MQTT_HOST="$(DEFAULT_MQTT_HOST)" \
		-DDEFAULT_MQTT_PORT=$(DEFAULT_MQTT_PORT) \
		-DDEFAULT_MQTT_USER="$(DEFAULT_MQTT_USER)" \
		-DDEFAULT_MQTT_PASS="$(DEFAULT_MQTT_PASS)" \
		-DDEFAULT_WEBHOOK_URL="$(DEFAULT_WEBHOOK_URL)"
	$(NINJA) -C $(BUILD_DIR)

flash: build
	$(PICOTOOL) load $(BUILD_DIR)/$(PROJECT).uf2 -fx

serial:
	screen $(SERIAL_PORT) $(SERIAL_BAUD)

clean:
	rm -rf $(BUILD_DIR) $(GEN_DIR)
