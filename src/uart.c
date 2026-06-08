#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <assert.h>
#include "uart.h"
#include "utils.h"


static struct termios oldt_stdin, newt_stdin, oldt_uart, newt_uart;


/*
 * Verify that terminal changes were actually applied.
 *
 * tcsetattr() can silently fail to apply some settings, so partial
 * failures must be caught and reported.
 */
static bool verify_tcsetattr(int fd, struct termios *termios_st)
{
	struct termios tmp = {0};

	tcgetattr(fd, &tmp);

	if (memcmp(termios_st, &tmp, sizeof(struct termios)) != 0) {
		MENU_ERROR("Changing settings failed");
		errno = ENOTTY;
		return false;
	}
	return true;
}

/*
 * Open the UART device automatically and report if it fails
 *
 * Attempts to open common USB-to-serial device paths automatically.
 * Currently checks ttyUSB* and ttyACM* devices (most USB adapters).
 *
 * Note: Built-in serial ports (ttyS*) are NOT in the auto-detect list.
 * They can be specified via command-line argument if needed.
 *
 * TODO: Search filesystem dynamically instead of using hardcoded list.
 */
static int open_dev(char **dev)
{
	int uart_fd = -1;

	if (*dev == NULL) {

		MENU_WARN("Device path not specified, attemping to find a device...");
		char *default_dev[] = {
			"/dev/ttyUSB0",
			"/dev/ttyUSB1",
			"/dev/ttyACM0",
			"/dev/ttyACM1",
		};
		for (size_t i = 0; i < ARRAY_SIZE(default_dev); i++) {

			/* O_NONBLOCK would result in busy-wait implementation,
			 * while poll() with infinite timeout idles in kernel */
			uart_fd = open(default_dev[i], O_RDWR | O_NOCTTY);
			if (uart_fd >= 0) {
				*dev = default_dev[i];
				break;
			}
		}
		if (uart_fd < 0) {
			MENU_ERROR("Device could not be found automatically, please try "
				  "the following:\n1) Run with root permissions.\n"
				  "2) Specify the device path in the arguments.\n"
				  "3) Is the device plugged?");
			errno = EPERM;
			return -1;
		}

	} else {
		// discard path if it's not a serial (char) device
		struct stat st;

		int ret = stat(*dev, &st);
		if (ret < 0) {
			return -1;
		}
		if (!S_ISCHR(st.st_mode)) {
			errno = ENOTTY;
			return -1;
		}
		uart_fd = open(*dev, O_RDWR | O_NOCTTY);
	}

	return uart_fd;
}

/*
 * Set UART baud rate
 *
 * Allowed values are from 0 to UINT_MAX.
 *
 * Some systems use different encoding values and an assertion is made to verify that
 * corresponding uint values can be used. If it fails, a switch-case should be manually
 * added to map user input to the correct macro.
 *
 * If being set in interactive mode (set_now == true), apply changes immediately and
 * verify that they were successfully applied.
 */
int set_baud(int uart_fd, unsigned *baud, bool set_now)
{
	static_assert(B115200 == 115200U, "Baud rate macros are not equivalent to"
			"corresponding int values. They should be manually converted.");

	if (*baud == 0)
		*baud = B115200;

	cfsetspeed(&newt_uart, *baud);

	if (set_now == true) {

		int ret = tcsetattr(uart_fd, TCSANOW, &newt_uart);
		if (ret < 0 || !verify_tcsetattr(uart_fd, &newt_uart)) {
			MENU_ERROR("Failed to set baud to %d", *baud);
			return -1;
		}

		tcflush(uart_fd, TCIOFLUSH);
	}

	return 0;
}

/*
 * Set data bits in the range 5-8
 */
int set_data_bits(int uart_fd, unsigned *data_bits, bool set_now)
{
	newt_uart.c_cflag &= ~CSIZE;

	switch (*data_bits) {
	case 0:
		*data_bits = 8;	/* if user doesn't specify value, by default data_bits set to 8 */
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
		MENU_ERROR("Unsupported data bit value: %d", *data_bits);
		errno = EINVAL;

		return -1;
	}

	if (set_now == true) {

		int ret = tcsetattr(uart_fd, TCSANOW, &newt_uart);
		if (ret < 0 || !verify_tcsetattr(uart_fd, &newt_uart)) {
			MENU_ERROR("Failed to set data bits to %u", *data_bits);
			return -1;
		}

		tcflush(uart_fd, TCIOFLUSH);
	}

	return 0;
}

/*
 * Set parity bit
 *
 * Set the parity bit to an allowed value. On some systems, MARK and SPACE parity require
 * the CMSPAR flag which is not available on all platforms, and a check is made.
 */
int set_parity_bit(int uart_fd, char *parity_bit, bool set_now)
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
		MENU_ERROR("Mark/Space parity not supported on this system.");
		errno = ENOTSUP;

		return -1;
#endif

	default:
		MENU_ERROR("Unsupported parity: %c", *parity_bit);
		errno = EINVAL;

		return -1;
	}

	if (set_now == true) {

		int ret = tcsetattr(uart_fd, TCSANOW, &newt_uart);
		if (ret < 0 || !verify_tcsetattr(uart_fd, &newt_uart)) {
			MENU_ERROR("Failed to set parity bit to %c", *parity_bit);
			return -1;
		}

		tcflush(uart_fd, TCIOFLUSH);
	}

	return 0;
}

/* Set the stop bits to 1 or 2 */
int set_stop_bits(int uart_fd, unsigned *stop_bits, bool set_now)
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
		MENU_ERROR("Unsupported stop bits: %d", *stop_bits);
		errno = EINVAL;
		return -1;
	}

	if (set_now == true) {

		int ret = tcsetattr(uart_fd, TCSANOW, &newt_uart);
		if (ret < 0 || !verify_tcsetattr(uart_fd, &newt_uart)) {
			MENU_ERROR("Failed to set stop bits to %u", *stop_bits);
			return -1;
		}

		tcflush(uart_fd, TCIOFLUSH);
	}

	return 0;
}

/*
 * Configure UART device
 *
 * Disable all input/output processing (newline conversion, flow control, etc.)
 * for a raw byte-for-byte communication with the serial device
 *
 * Returns -1 on failure if the setup failed in setting the new values or applying them.
 */
static int setup_uart(struct uart_conf_t *uart_conf)
{
	tcgetattr(uart_conf->fd, &oldt_uart);
	newt_uart = oldt_uart;

	newt_uart.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
        		     | INLCR | IGNCR | ICRNL | IXON);
	newt_uart.c_oflag &= ~OPOST;
	newt_uart.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	newt_uart.c_cflag |= (CREAD | CLOCAL);
	newt_uart.c_cc[VMIN] = 1;
	newt_uart.c_cc[VTIME] = 0;

	set_baud(uart_conf->fd, &uart_conf->baud, false);

	int ret = set_data_bits(uart_conf->fd, &uart_conf->data_bits, false);
	if (ret < 0) {
		return -1;
	}
	ret = set_parity_bit(uart_conf->fd, &uart_conf->parity_bit, false);
	if (ret < 0) {
		return -1;
	}
	ret = set_stop_bits(uart_conf->fd, &uart_conf->stop_bits, false);
	if (ret < 0) {
		return -1;
	}

	ret = tcsetattr(uart_conf->fd, TCSANOW, &newt_uart);
	if (ret < 0 || !verify_tcsetattr(uart_conf->fd, &newt_uart)) {
		MENU_ERROR("Failed to setup UART TTY");
		return -1;
	}
	tcflush(uart_conf->fd, TCIOFLUSH);

	return 0;
}

/*
 * Set up TTY for raw, non-buffered input so that text and control characters
 * are properly sent to the device, and only the device's output is shown on terminal
 */
static int setup_stdin(void)
{
	tcgetattr(STDIN_FILENO, &oldt_stdin);
	newt_stdin = oldt_stdin;

	newt_stdin.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
	newt_stdin.c_iflag &= ~(IXON | IXOFF | IXANY | INLCR | ICRNL | IGNCR);
	newt_stdin.c_cc[VMIN] = 1;
	newt_stdin.c_cc[VTIME] = 0;
	int ret = tcsetattr(STDIN_FILENO, TCSADRAIN, &newt_stdin);

	if (ret < 0 || !verify_tcsetattr(STDIN_FILENO, &newt_stdin)) {
		MENU_ERROR("Could not set up terminal settings.");
		return -1;
	}

	return 0;
}

/*
 * Initialize UART device and configure terminal for serial communication
 *
 * Acts as the "main" of this library.
 */
int init_uart(struct uart_conf_t *uart_conf)
{
	if (uart_conf == NULL) {
		MENU_ERROR("Please set up UART config struct.");
		return -1;
	}

	uart_conf->fd = open_dev(&uart_conf->dev);
	if (uart_conf->fd < 0) {
		perror(__func__);
		return -1;
	}

	int ret = setup_uart(uart_conf);
	if (ret < 0) {
		perror(__func__);
		close_uart(uart_conf->fd);
		return -1;
	}

	ret = setup_stdin();
	if (ret < 0) {
		perror(__func__);
		close_uart(uart_conf->fd);
		return -1;
	}

	return uart_conf->fd;
}

/* If needed, restore original TTY settings for stdin and UART, and close UART device */
void close_uart(int uart_fd)
{
	/* if UART dev was open restore termios settings, verify and close fd */
	if (uart_fd >= 0) {
		if (memcmp(&oldt_uart, &newt_uart, (sizeof(struct termios))) != 0) {
			int ret = tcsetattr(uart_fd, TCSANOW, &oldt_uart);
			if (ret < 0 || !verify_tcsetattr(uart_fd, &oldt_uart)) {
				MENU_ERROR("Error restoring UART settings.");
			} else {
				tcflush(uart_fd, TCIOFLUSH);
				MENU_MSG("UART settings were restored.");
			}
		}
		close(uart_fd);
	}

	/* Restore and verify TTY settings */
	if (memcmp(&oldt_stdin, &newt_stdin, (sizeof(struct termios))) != 0) {
		int ret = tcsetattr(STDIN_FILENO, TCSADRAIN, &oldt_stdin);
		if (ret < 0 || !verify_tcsetattr(STDIN_FILENO, &oldt_stdin)) {
			MENU_ERROR("Error restoring terminal settings.");
		} else {
			MENU_MSG("Terminal settings were restored.");
		}
	}
}
