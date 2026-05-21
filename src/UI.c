#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <poll.h>
#include <fcntl.h>
#include "uart.h"
#include "UI.h"
#include "utils.h"


/* Paste ASCII file into UART TTY */
static int menu_paste_file(struct pollfd *poll_fds, nfds_t poll_fds_count, int uart_fd)
{
	char c = '\0';
	char file_path[PATH_MAX];
	int i = 0;

	MENU_TITLE("Paste file");
	MENU_OPTS("Paste an ASCII file into the device's STDIN.\n\n"
		  "Menu options:\n\tC-a|ESC\texit menu");
	MENU_PROMPT("Specify the file path");

	while (true) {

		poll(poll_fds, poll_fds_count, -1);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			close_uart(-1);
			MENU_ERROR("Device disconnected, exiting.");
			exit(ENOENT);
		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		// build a path
		read(STDIN_FILENO, &c, 1);
		switch (c) {
		case '\t':
			continue;
		case '\r':
		case '\n':
			if (i == 0) {
				continue;
			}
			file_path[i] = '\0';
			i = 0;
			break;

		case MENU:
		case ESC:
			TTY_READY;

			return 0;
		case '\b':
		case DEL:
			if (i == 0) {
				continue;
			}
			fprintf(stderr, "\033[D\033[P");
			if (i == PATH_MAX - 1) {
				MENU_CLEAR_EVENT;
			}
			file_path[i] = '\0';
			--i;
			continue;
		default:
			if (i < PATH_MAX - 1) {
				fprintf(stderr, "%c", c);
				file_path[i] = c;
				++i;
			} else {
				MENU_EVENT("Exceeded path length, last char ignored!");
			}
			continue;
		}

		int fd = open(file_path, O_RDONLY);
		if (fd < 0) {
			MENU_ERROR("Error opening file.");
			perror(__func__);
			MENU_PROMPT("Specify the file path");
			continue;
		}

		int len = -1;
		char buf[1024];

		MENU_MSG("Pasting file into UART TTY...");
		while (true) {

			len = read(fd, buf, sizeof(buf));
			// EOF
			if (len == 0) {
				break;
			}
			// Error reading file
			if (len < 0) {
				MENU_ERROR("Error reading file.");
				perror(__func__);
				return 0;
			}

			len = write(uart_fd, buf, len);
			// Error writing file
			if (len <= 0 && errno != 0) {
				MENU_ERROR("Error pasting file.");
				perror(__func__);
				return 0;
			}
		}

		close(fd);
		MENU_MSG("File \"%s\" pasted successfully.", file_path);
		TTY_READY;
		return 0;
	}
}

/* Set baud in the interactive menu, poll() checks if dev was disconnected */
static int menu_baud(struct pollfd *poll_fds, nfds_t poll_fds_count, int uart_fd, unsigned *baud)
{
	char c = '\0';
	int ret = -1;
	char buf[100] = {0};
	int i = 0;

	MENU_TITLE("Baud selection");
	MENU_OPTS("Valid values: 0 - %u\n"
		  "(0 sets baud to default value 115200)\n\n"
		  "\tC-a|ESC\tback to terminal\n"
		  "\tq\tquit", UINT_MAX);
	MENU_PROMPT("Enter your choice");

	while (true) {

		poll(poll_fds, poll_fds_count, -1);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			close_uart(-1);
			MENU_ERROR("Device disconnected, exiting.");
			exit(ENOENT);
		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		read(STDIN_FILENO, &c, 1);
		switch (c) {
		default:
			fprintf(stderr, "%c", c);
			buf[i] = c;
			i++;
			break;
		case '\n':
		case '\r':
			if (i == 0) {
				continue;
			}
			buf[i] = '\0';
			i = 0;

			unsigned u = strtouint(buf);
			if (errno != 0) {
				MENU_ERROR("Invalid input");
				MENU_PROMPT("Enter your choice");
				continue;
			}

			ret = set_baud(uart_fd, &u, true);
			if (ret < 0) {
				MENU_ERROR("Error setting baud, try again");
				MENU_PROMPT("Enter your choice");
				continue;
			}

			*baud = u;
			MENU_MSG("Baud was set to %u.", u);
			/* fallthrough */
		case MENU:
		case ESC:
			TTY_READY;

			return 0;
		case 'q':

			return -1;
		}
	}
}

/* Set data bits in the interactive menu, poll() checks if the dev was disconnected */
static int menu_data_bits(struct pollfd *poll_fds, nfds_t poll_fds_count,
			  int uart_fd, unsigned *data_bits)
{
	int ret = -1;
	char c = '\0';
	char buf[2] = {0};
	unsigned u = 0;


	MENU_TITLE("Data bit selection");
	MENU_OPTS("Please choose an option:\n\n"
		  "\t8|0\t8 bits (default)\n"
		  "\t7\t7 bits\n"
		  "\t6\t6 bits\n"
		  "\t5\t5 bits\n\n"
		  "\tC-a|ESC\tback to terminal\n"
		  "\tq\tquit");
	MENU_PROMPT("Enter your choice");

	while (true) {
		poll(poll_fds, poll_fds_count, -1);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			close_uart(-1);
			MENU_ERROR("Device disconnected, exiting.");
			exit(ENOENT);
		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		read(STDIN_FILENO, &c, 1);

		switch (c) {
		default:
			INVAL_INPUT("%c", c);
			MENU_PROMPT("Enter your choice");
			/* fallthrough */
		case ENTER:
			continue;
		case '0':
		case '8':
		case '7':
		case '6':
		case '5':
			buf[0] = c;
			u = strtouint(buf);
			if (errno != 0) {
				INVAL_INPUT("%c", c);
				MENU_PROMPT("Enter your choice");
				continue;
			}

			ret = set_data_bits(uart_fd, &u, true);
			if (ret < 0) {
				MENU_ERROR("Error setting data bits, try again");
				MENU_PROMPT("Enter your choice");
				continue;
			}
			*data_bits = u;

			MENU_MSG("Data bits set to %u successfully.", u);
			/* fallthrough */
		case MENU:
		case ESC:
			TTY_READY;
			return 0;
		case 'q':
			return -1;
		}
	}
}

/* Set parity bit in the interactive menu, poll() checks if the dev was disconnected */
static int menu_parity_bit(struct pollfd *poll_fds, nfds_t poll_fds_count,
			   int uart_fd, char *parity_bit)
{
	char c = 0;
	int ret = 0;

	MENU_TITLE("Parity bit selection");
	MENU_OPTS("Please choose an option:\n\n"
		  "\tN|n|0\tnone (default)\n"
		  "\tO|o\todd\n"
		  "\tE|e\teven\n"
		  "\tM|m\tmark parity\n"
		  "\tS|s\tspace parity\n\n"
		  "\tC-a|ESC\tback to terminal\n"
		  "\tq\tquit");
	MENU_PROMPT("Enter your choice");

	while (true) {
		poll(poll_fds, poll_fds_count, -1);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			close_uart(-1);
			MENU_ERROR("Device disconnected, exiting.");
			exit(ENOENT);
		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		read(STDIN_FILENO, &c, 1);

		switch (c) {
		default:
			INVAL_INPUT("%c", c);
			MENU_PROMPT("Enter your choice");
			/* fallthrough */
		case ENTER:
			continue;
		case '0':
			c = 'N';
			/* fallthrough */
		case 'N':
		case 'n':
		case 'O':
		case 'o':
		case 'E':
		case 'e':
		case 'M':
		case 'm':
		case 'S':
		case 's':
			ret = set_parity_bit(uart_fd, &c, true);
			if (ret < 0) {
				INVAL_INPUT("%c", c);
				MENU_PROMPT("Enter your choice");
				continue;
			}

			*parity_bit = toupper(c);
			MENU_MSG("Parity bit set to %c successfully.", *parity_bit);
			/* fallthrough */
		case MENU:
		case ESC:
			TTY_READY;
			return 0;
		case 'q':
			return -1;
		}
	}
}

/* Set stop bits in the interactive menu, poll() checks if the dev was disconnected */
static int menu_stop_bits(struct pollfd *poll_fds, nfds_t poll_fds_count,
				int uart_fd, unsigned *stop_bits)
{
	int ret = -1;
	unsigned u = 0;
	char c = 0;
	char buf[2] = {0};

	MENU_TITLE("Stop bit selection");
	MENU_OPTS("Please choose an option:\n\n"
		  "\t1|0\t1 bit (default)\n"
		  "\t2\t2 bits\n\n"
		  "\tC-a|ESC\tback to terminal\n"
		  "\tq\tquit");
	MENU_PROMPT("Enter your choice");

	while (true) {
		poll(poll_fds, poll_fds_count, 0);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			close_uart(-1);
			MENU_ERROR("Device disconnected, exiting.");
			exit(ENOENT);
		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		read(STDIN_FILENO, &c, 1);

		switch (c) {
		default:
			buf[0] = c;
			u = strtouint(buf);

			if (errno != 0) {
				INVAL_INPUT("%c", c);
				MENU_PROMPT("Enter your choice");
				continue;
			}

			ret = set_stop_bits(uart_fd, &u, true);
			if (ret < 0) {
				MENU_ERROR("Error setting stop bits, try again.");
				MENU_PROMPT("Enter your choice");
				continue;
			}
			*stop_bits = u;

			MENU_MSG("Stop bit set to %u successfully.", u);
			/* fallthrough */
		case MENU:
		case ESC:
			TTY_READY;
			return 0;
		case 'q':
			return -1;
		case ENTER:
			continue;
		}
	}
}

/* Interactive menu for setting baud, data, parity and stop bits. poll() checks if there is
 * incoming data from UART while the menu is on, and if the dev disconnected.
 * Data incoming from the dev will be buffered while interacting with the menu, and shown on the
 * screen after going back to terminal. */
int menu(int uart_fd, struct uart_conf_t *uart_conf, struct pollfd *poll_fds, int poll_fds_count)
{
	char c = '\0';
	bool incoming_data = false;

	MENU_TITLE("tinycom menu");
	MENU_OPTS("Connected to \033[4m%s\033[24m\n"
			"Current settings: \033[1m%u %u%c%u\033[m\n\n"
			"Menu options:\n"
			"\tb\tset baud\n"
			"\td\tset data bits\n"
			"\tp\tset parity bit\n"
			"\ts\tset stop bits\n"
			"\tv\tpaste an ASCII file\n"
			"\tC-a|ESC\texit menu\n"
			"\tq\tquit",
			uart_conf->dev, uart_conf->baud, uart_conf->data_bits,
			uart_conf->parity_bit, uart_conf->stop_bits);
	MENU_PROMPT("Enter your choice");

	while (true) {

		poll(poll_fds, poll_fds_count, -1);

		/* Device disconnected */
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			close_uart(-1);
			MENU_ERROR("%s disconnected, exiting.", uart_conf->dev);
			exit(ENOENT);

		/* Data coming from UART */
		} else if (!incoming_data && poll_fds[UART_PFD].revents & POLLIN) {
			incoming_data = true;
			MENU_EVENT("Data incoming from UART dev.");

		/* Data coming from user */
		} else if (poll_fds[STDIN_PFD].revents & POLLIN) {
			read(STDIN_FILENO, &c, 1);

			switch (c) {
			default:
				INVAL_INPUT("%c", c);
				MENU_PROMPT("Enter your choice");
				/* fallthrough */
			case ENTER:
				continue;
			case MENU:
			case ESC:
				TTY_READY;
				return 0;
			case 'v':
				return menu_paste_file(poll_fds, poll_fds_count, uart_fd);
			case 'q':
				return -1;
			case 'b':
				return menu_baud(poll_fds, poll_fds_count,
						 uart_fd, &uart_conf->baud);
			case 'd':
				return menu_data_bits(poll_fds, poll_fds_count,
						      uart_fd, &uart_conf->data_bits);
			case 'p':
				return menu_parity_bit(poll_fds, poll_fds_count,
						       uart_fd, &uart_conf->parity_bit);
			case 's':
				return menu_stop_bits(poll_fds, poll_fds_count,
						      uart_fd, &uart_conf->stop_bits);
			}
		}
	}
}
