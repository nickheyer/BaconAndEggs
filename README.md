# BaconAndEggs

Wake-on-LAN server for Raspberry Pi Pico W.

![BaconAndEggs](LOGO.png)

## What

A tiny always-on WoL appliance. Plug it into power, it joins your wifi, and sits there waiting for you to tell it which machines need waking up. Manages everything over TCP — connect with netcat, do stuff.

Config persists to flash. You can set it to auto-wake all your servers on boot.

## Setup

1. Install [Pico SDK](https://github.com/raspberrypi/pico-sdk)
2. `cp .env.example .env` and fill in your wifi creds + default servers
3. `make flash`

Pico connects to wifi and starts listening on port 4242.

## Usage

```
nc 192.168.1.x 4242
```

> Type `help` for commands


## Debug

Serial console:

```
make serial
```

Defaults to `/dev/tty.usbmodem141101`. Override with `SERIAL_PORT` / `SERIAL_BAUD` in your `.env` or on the command line.

## Name

WoL -> waking up -> breakfast -> BaconAndEggs.
