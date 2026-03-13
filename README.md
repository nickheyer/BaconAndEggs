# BaconAndEggs

Wake-on-LAN server for Raspberry Pi Pico W.

## What

A tiny always-on WoL appliance. Plug it into power, it joins your wifi, and sits there waiting for you to tell it which machines need waking up. Manages everything over TCP — connect with netcat, do stuff.

Config persists to flash. You can set it to auto-wake all your servers on boot for ez mode.

## Setup

1. Install [Pico SDK](https://github.com/raspberrypi/pico-sdk) (VS Code ext does this, or set `PICO_SDK_PATH` manually)
2. `cp .env.example .env` and fill in your wifi creds + default servers
3. `make flash`

That's it. Your Pico W will connect to wifi and start listening on port 4242.

## Usage

```
nc 192.168.1.x 4242
```

| Command | Does |
|---------|-------------|
| `wake <name>` | Wake a specific server |
| `wake all` | The whole breakfast buffet |
| `list` | Show configured servers |
| `add <name> <mac>` | Add a server, e.g. `add mypc AA:BB:CC:DD:EE:FF` |
| `remove <name>` | Remove a server |
| `autowake on\|off` | Wake everything on boot (for the truly lazy) |
| `save` | Persist config to flash |
| `factory` | Burn it all down, start fresh |
| `help` | If you're reading this and still need help |

## Build Targets

| Target | What it does |
|--------|-------------|
| `make build` | Compile the firmware |
| `make flash` | Build + flash to Pico W via picotool |
| `make serial` | Open serial debug console |
| `make clean` | Nuke the build directory |

## Debug

Serial console over USB:

```
make serial
```

Defaults to `/dev/tty.usbmodem141101` at 115200 baud. Override with `SERIAL_PORT` / `SERIAL_BAUD` in your `.env` or on the command line.

## Name

WoL -> waking up -> breakfast -> BaconAndEggs.
