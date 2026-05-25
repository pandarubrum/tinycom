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
$ build/tinycom [-b baud] [-d data_bits] [-p parity] [-s stop_bits] [device]
```

| Flag | Action |
|------|--------|
| `-b` | set baud rate |
| `-d` | set data bits |
| `-p` | set parity |
| `-s` | set stop bit |


## Menu

There is an interactive menu inside `tinycom`, it is accessible via `CONTROL+A`.

| Options | Action |
|---------|--------|
| `b` | set baud rate |
| `d` | set data bits |
| `p` | set parity |
| `s` | set stop bit |
| `v` | paste ASCII file |
| `ESC` | go back to TTY |
| `q` | quit tinycom |

