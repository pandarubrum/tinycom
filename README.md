# tinycom

Is a work-in-progress minimal serial communication program.


## Installation

```bash
git clone https://github.com/pandarubrum/tinycom.git
cd tinycom
cmake -B build
cmake --build build
```


## Usage

`tinycom` without any flags/arguments can run with default settings `115200 8N1` and will try to find a device automatically.

Synopsis:
```bash
$ build/tinycom [-b baud rate] [-d data bits] [-p parity bit] [-s stop bits] [device]
```

| Flag | Action |
|------|--------|
| `-b` | set baud rate |
| `-d` | set data bits |
| `-p` | set parity bit |
| `-s` | set stop bits |


## Menu

There is an interactive menu inside `tinycom`, it is accessible via `Alt-M`.

| Option | Action |
|--------|--------|
| `B` | set baud rate |
| `D` | set data bits |
| `P` | set parity bit |
| `S` | set stop bits |
| `V` | paste ASCII file |
| `ESC/Alt-M` | go back to TTY |
| `Alt-Q` | quit tinycom |


## Tested on

- `tinycom` running on Debian (`x86_64`), Linux kernel `7.0.4+deb14-amd64`, glibc `2.42-16`
- LicheePi 4A (`riscv64`) running Debian, Linux kernel `6.6.119-th1520`
- Raspberry Pi 3 Model B Rev 1.2, running Debian, Linux kernel `6.12.87+rpt-rpi-v8`
- ESP32-C3 (`riscv64`)
- Arduino Uno R3 ATmega328P
