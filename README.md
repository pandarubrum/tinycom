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

There is an interactive menu inside `tinycom` that should be intutitive enough for usage. It is accessible via `CTRL+A`. It can set baud rate, data bits, parity, and stop bits. 

The same settings can also be set when running tinycom with its flags:
- `-b` for baud rate
- `-d` for data bits
- `-p` for parity bit
- `-s` for stop bit

The device can be given as the last argument without a flag.

An example is printed when running:
```bash
build/tinycom -h
```


## Tested on

- `tinycom` running on Debian (`x86_64`), Linux kernel `7.0.4+deb14-amd64`, glibc `2.42-16`
- LicheePi 4A (`riscv64`) running Debian, Linux kernel `6.6.119-th1520`
- ESP32-C3 (`riscv64`)
- Arduino Uno R3 Atmega328P
