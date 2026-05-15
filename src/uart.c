#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "uart.h"


static struct termios oldt_stdin, newt_stdin, oldt_uart, newt_uart;


// helper function to verify if changes to a termios struct were applied successfully
static bool verify_tcsetattr(int fd, struct termios *termios_st)
{
	struct termios tmp = {0};

	tcgetattr(fd, &tmp);

	// compare termios structs that contain what was meant to be changed
	// and what actually got changed
	if (memcmp(termios_st, &tmp, sizeof(struct termios)) != 0) {
		LOG_ERROR("Changing settings failed");
		errno = ENOTTY;
		return false;
	}
	return true;
}

static int setup_stdin()
{
	tcgetattr(STDIN_FILENO, &oldt_stdin);
	newt_stdin = oldt_stdin;
	newt_stdin.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
	newt_stdin.c_iflag &= ~(IXON | IXOFF | IXANY | INLCR | ICRNL | IGNCR);
	newt_stdin.c_cc[VMIN] = 1;
	newt_stdin.c_cc[VTIME] = 0;
	int ret = tcsetattr(STDIN_FILENO, TCSANOW, &newt_stdin);

	// verify if new terminal settings were applied
	if (ret < 0 || !verify_tcsetattr(STDIN_FILENO, &newt_stdin)) {
		LOG_ERROR("Could not set up terminal settings.");
		return -1;
	}

	tcflush(STDIN_FILENO, TCIOFLUSH);

	return 0;
}

static int open_dev(char **dev)
{
	int uart_fd = -1;
	// if user did not specify dev path, automatically attempt opening a path from list
	if (*dev == NULL) {
		// TODO: search for devs in FS instead of using a hardcoded list
		LOG_WARN("Device path not specified, attemping to find a device...");
		char *default_dev[] = {
			"/dev/ttyUSB0",
			"/dev/ttyUSB1",
			"/dev/ttyACM0",
			"/dev/ttyACM1",
		};
		for (int i = 0; i < sizeof(default_dev) / sizeof(default_dev[0]); i++) {

			// O_NONBLOCK would result in busy-wait implementation,
			// while poll with infinite timeout idles in kernel
			uart_fd = open(default_dev[i], O_RDWR | O_NOCTTY);
			if (uart_fd >= 0) {
				*dev = default_dev[i];
				break;
			}
		}
		if (uart_fd < 0) {
			LOG_ERROR("Device could not be found automatically, please try"
				  "the following:\n1) Run with root permissions.\n"
				  "2) Specify the device path in the arguments.");
			errno = EPERM;
			return -1;
		}

	} else {
		uart_fd = open(*dev, O_RDWR | O_NOCTTY);
	}

	if (uart_fd < 0) {
		LOG_ERROR("Could not open device.");
	}

	return uart_fd;
}

int set_baud(int uart_fd, unsigned *baud, bool set_now)
{
	static_assert(B115200 == 115200U, "Baud rate macros are not equivalent to"
			"corresponding int values. They should be manually converted.");

	if (*baud == 0)
		*baud = B115200;

	cfsetspeed(&newt_uart, *baud);

	// set attr right away and check if settings were applied successfully
	// (for interactive baud selection)
	if (set_now == true) {
		struct termios tmp = {0};

		int ret = tcsetattr(uart_fd, TCSANOW, &newt_uart);
		if (ret < 0 || !verify_tcsetattr(uart_fd, &newt_uart)) {
			LOG_ERROR("Failed to set baud to %d", *baud);
			return -1;
		}

		/*
		tcgetattr(uart_fd, &tmp);

		if ((cfgetispeed(&tmp) != *baud) || (cfgetospeed(&tmp) != *baud)) {
			LOG_ERROR("Failed to set baud to %d", *baud);
			errno = EINVAL;
			return -1;
		}
		*/

		tcflush(uart_fd, TCIOFLUSH);
	}

	return 0;
}

static int set_data_bits(int *data_bits)
{
	newt_uart.c_cflag &= ~CSIZE;

	switch (*data_bits) {
	case 0:
		*data_bits = 8;	// if user doesn't specify value, by default data_bits set to 8
		/* fallthrough */
	case 8:
		newt_uart.c_cflag |= CS8;
		break;
	case 7:
		newt_uart.c_cflag |= CS7;
		break;
	case 6:
		newt_uart.c_cflag |= CS6;
		break;
	case 5:
		newt_uart.c_cflag |= CS5;
		break;
	default:
		LOG_ERROR("Unsupported data bit value: %d", *data_bits);
		errno = EINVAL;

		return -1;
	}
}

static int set_parity_bit(char *parity_bit)
{
	switch (*parity_bit) {
	case '\0':
		*parity_bit = 'N'; // set to N if not specified by user
		/* fallthrough */
	case 'N':
	case 'n':
		newt_uart.c_cflag &= ~(PARENB | PARODD);
#ifdef CMSPAR
		newt_uart.c_cflag &= ~CMSPAR;
#endif
		break;
	case 'E':
	case 'e':
		newt_uart.c_cflag |= PARENB;
		newt_uart.c_cflag &= ~PARODD;
#ifdef CMSPAR
		newt_uart.c_cflag &= ~CMSPAR;
#endif
		break;
	case 'O':
	case 'o':
		newt_uart.c_cflag |= PARENB;
		newt_uart.c_cflag |= PARODD;
#ifdef CMSPAR
		newt_uart.c_cflag &= ~CMSPAR;
#endif
		break;
#ifdef CMSPAR
	case 'M':
	case 'm':
		newt_uart.c_cflag |= PARENB;
		newt_uart.c_cflag |= CMSPAR;
		newt_uart.c_cflag |= PARODD;
		break;

	case 'S':
	case 's':
		newt_uart.c_cflag |= PARENB;
		newt_uart.c_cflag |= CMSPAR;
		newt_uart.c_cflag &= ~PARODD;
		break;
#else
	case 'M':
	case 'm':
	case 'S':
	case 's':
		LOG_ERROR("Mark/Space parity not supported on this system.");
		errno = ENOTSUP;

		return -1;
#endif

	default:
		LOG_ERROR("Unsupported parity: %c", *parity_bit);
		errno = EINVAL;

		return -1;
	}
}

static int set_stop_bits(int *stop_bits)
{
	switch (*stop_bits) {
	case 0:
		*stop_bits = 1;	// if not defined by user, set to 1
		/* fallthrough */
	case 1:
		newt_uart.c_cflag &= ~CSTOPB;
		break;
	case 2:
		newt_uart.c_cflag |= CSTOPB;
		break;
	default:
		LOG_ERROR("Unsupported stop bits: %d", *stop_bits);
		errno = EINVAL;
		return -1;
	}
}

static int setup_uart(int uart_fd, struct uart_conf_t *uart_conf)
{
	int ret = 0;

	tcgetattr(uart_fd, &oldt_uart);
	newt_uart = oldt_uart;

	// set up raw-like uart tty
	newt_uart.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
        		     | INLCR | IGNCR | ICRNL | IXON);
	newt_uart.c_oflag &= ~OPOST;
	newt_uart.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	newt_uart.c_cflag |= (CREAD | CLOCAL);
	newt_uart.c_cc[VMIN] = 1;
	newt_uart.c_cc[VTIME] = 0;

	set_baud(uart_fd, &uart_conf->baud, false);

	ret = set_data_bits(&uart_conf->data_bits);
	if (ret < 0) {
		return -1;
	}
	ret = set_parity_bit(&uart_conf->parity_bit);
	if (ret < 0) {
		return -1;
	}
	ret = set_stop_bits(&uart_conf->stop_bits);
	if (ret < 0) {
		return -1;
	}

	// set up UART config and verify changes
	ret = tcsetattr(uart_fd, TCSANOW, &newt_uart);
	if (ret < 0 || !verify_tcsetattr(uart_fd, &newt_uart)) {
		LOG_ERROR("Failed to setup UART TTY");
		close_uart(uart_fd);
		return -1;
	}
	tcflush(uart_fd, TCIOFLUSH);

	return 0;
}

int init_uart(struct uart_conf_t *uart_conf)
{
	if (uart_conf == NULL) {
		LOG_ERROR("Please set up UART config struct.");
		return -1;
	}

	int ret = 0;

	// set up terminal for communication with UART dev
	ret = setup_stdin();
	if (ret < 0) {
		perror(__func__);
		return -1;
	}

	int uart_fd = open_dev(&uart_conf->dev);
	if (uart_fd < 0) {
		perror(__func__);
		return -1;
	}

	ret = setup_uart(uart_fd, uart_conf);
	if (ret < 0) {
		perror(__func__);
		return -1;
	}

	return uart_fd;
}

void close_uart(int uart_fd)
{
	int ret = -1;

	fprintf(stderr, "\n");

	// Restore and verify terminal settings
	ret = tcsetattr(STDIN_FILENO, TCSANOW, &oldt_stdin);
	if (ret < 0 || !verify_tcsetattr(STDIN_FILENO, &oldt_stdin)) {
		LOG_ERROR("Error restoring terminal settings.");
	}
	tcflush(STDIN_FILENO, TCIOFLUSH);

	LOG_WARN("Terminal settings were restored.");

	// if UART dev was open restore termios settings, verify and close fd
	if (uart_fd >= 0) {
		ret = tcsetattr(uart_fd, TCSANOW, &oldt_uart);
		if (ret < 0 || !verify_tcsetattr(uart_fd, &oldt_uart)) {
			LOG_ERROR("Error restoring UART settings.");
		}
		tcflush(uart_fd, TCIOFLUSH);

		LOG_WARN("UART settings were restored.");
		close(uart_fd);
	}
	LOG_WARN("Exiting...");
}
