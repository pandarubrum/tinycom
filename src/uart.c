#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "uart.h"


static struct termios oldt_stdin, newt_stdin, oldt_uart, newt_uart;

static void setup_stdin()
{
	tcgetattr(STDIN_FILENO, &oldt_stdin);
	newt_stdin = oldt_stdin;
	newt_stdin.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
	newt_stdin.c_iflag &= ~(IXON | IXOFF | IXANY | INLCR | ICRNL | IGNCR);
	newt_stdin.c_cc[VMIN] = 1;
	newt_stdin.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSANOW, &newt_stdin);
}

static int open_dev(char **dev)
{
	int uart_fd = -1;
	// if user did not specify dev path, automatically attempt opening a path from list
	if (*dev == NULL) {
		printf("\e[35mDevice path not specified, attemping to find a device...\e[m\n");
		char *default_dev[] = {
			"/dev/ttyUSB0",
			"/dev/ttyUSB1",
			"/dev/ttyACM0",
			"/dev/ttyACM1",
		};
		for (int i = 0; i < sizeof(default_dev) / sizeof(default_dev[0]); i++) {

			// O_NONBLOCK would result in busy-wait implementation, while poll with infinite timeout idles in kernel
			uart_fd = open(default_dev[i], O_RDWR | O_NOCTTY);
			if (uart_fd >= 0) {
				*dev = default_dev[i];
				break;
			}
		}
		if (uart_fd == -1) {
			fprintf(stderr, "\e[31mDevice could not be found automatically, please try the following:\n"
					"1) Run with root permissions.\n"
					"2) Specify the device path in the arguments.\e[m\n");
			errno = EPERM;
			return -1;
		}

	} else {
		uart_fd = open(*dev, O_RDWR | O_NOCTTY);
	}

	if (uart_fd == -1) {
		fprintf(stderr, "\e[31mCould not open device.\e[m\n");
	}

	return uart_fd;
}

bool set_baud(int uart_fd, unsigned *baud, bool set_now)
{
	static_assert(B115200 == 115200U, "Baud rate macros are not equivalent to"
			"corresponding int values. They should be manually converted.");

	struct termios tmp = {0};

	if (*baud == 0)
		*baud = B115200;

	cfsetspeed(&newt_uart, *baud);

	// set attr right away and check if settings were applied successfully (for interactive baud selection)
	if (set_now == true) {

		int ret = tcsetattr(uart_fd, TCSANOW, &newt_uart);
		WARN_HANDLER(ret < 0, "Failed to set baud")

		tcgetattr(uart_fd, &tmp);

		if ((cfgetispeed(&tmp) != *baud) || (cfgetospeed(&tmp) != *baud)) {
			fprintf(stderr, "\e[31mFailed to set baud to %d\e[m\n", *baud);
			errno = EINVAL;
			return false;
		}

		tcflush(uart_fd, TCIOFLUSH);
	}

	return true;
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
		fprintf(stderr, "\e[31mUnsupported data bit value: %d\e[m\n", *data_bits);
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
		fprintf(stderr, "\e[31mMark/Space parity not supported on this system\e[m\n");
		errno = ENOTSUP;
		return -1;
#endif

	default:
		fprintf(stderr, "\e[31mUnsupported parity: %c\e[m\n", *parity_bit);
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
		fprintf(stderr, "\e[31mUnsupported stop bits: %d\e[m\n", *stop_bits);
		errno = EINVAL;
		return -1;
	}
}

// after setting UART params in init_uart, apply them to the dev and verify if settings were applied successfully
static bool verify_tcsetattr(int uart_fd)
{
	struct termios tmp = {0};
	tcgetattr(uart_fd, &tmp);

	//check if baud was set properly
	if ((cfgetispeed(&newt_uart) != cfgetispeed(&tmp)) || (cfgetospeed(&newt_uart) != cfgetospeed(&tmp))) {
		errno = ENOTTY;
		return false;
	}
	// check all other settings
	if (newt_uart.c_cflag != tmp.c_cflag) {
		errno = ENOTTY;
		return false;
	}

	return true;
}

int init_uart(struct uart_conf_t *uart_conf)
{
	if (uart_conf == NULL) {
		fprintf(stderr, "\e[31mPlease set up UART config struct.\e[m\n");
		return -1;
	}

	int ret = 0;

	// TODO: proper verification needed
	setup_stdin();

	int uart_fd = open_dev(&uart_conf->dev);
	if (uart_fd == -1) {
		perror(__func__);
		return -1;
	}

	tcgetattr(uart_fd, &oldt_uart);
	newt_uart = oldt_uart;

	// set up uart tty
	newt_uart.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
        		     | INLCR | IGNCR | ICRNL | IXON);
	newt_uart.c_oflag &= ~OPOST;
	newt_uart.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	newt_uart.c_cflag |= (CREAD | CLOCAL);
	newt_uart.c_cc[VMIN] = 1;
	newt_uart.c_cc[VTIME] = 0;

	set_baud(uart_fd, &uart_conf->baud, false);

	ret = set_data_bits(&uart_conf->data_bits);
	if (ret == -1) {
		return -1;
	}

	ret = set_parity_bit(&uart_conf->parity_bit);
	if (ret == -1) {
		return -1;
	}

	ret = set_stop_bits(&uart_conf->stop_bits);
	if (ret == -1) {
		return -1;
	}

	// set up UART config and verify changes
	ret = tcsetattr(uart_fd, TCSANOW, &newt_uart);
	ERROR_HANDLER(ret < 0, __func__, close_uart(uart_fd))
	ret = verify_tcsetattr(uart_fd);
	ERROR_HANDLER(ret == 0, __func__, close_uart(uart_fd))
	tcflush(uart_fd, TCIOFLUSH);

	return uart_fd;
}

void close_uart(int uart_fd)
{
	// TODO: proper verification needed
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt_stdin);
	if (uart_fd != -1) {
		tcsetattr(uart_fd, TCSANOW, &oldt_uart);

		close(uart_fd);
	}
}
