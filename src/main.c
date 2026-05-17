#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include "uart.h"
#include "UI.h"
#include "utils.h"


int main(int argc, char *argv[])
{
	const char *filename = strrchr(argv[0], '/');
	++filename;

	struct uart_conf_t uart_conf = {0};

	int opt;
	while ((opt = getopt(argc, argv, "b:d:p:s:h")) != -1) {
		unsigned u;
		switch (opt) {
		case 'b':
			u = strtouint(optarg);
			if (errno != 0) {
				MENU_ERROR("Invalid baud rate.");
				return EINVAL;
			}
			uart_conf.baud = u;
			break;
		case 'd':
			u = strtouint(optarg);
			if (errno != 0) {
				MENU_ERROR("Invalid data bit value.");
				return EINVAL;
			}
			uart_conf.data_bits = u;
			break;
		case 'p':
			uart_conf.parity_bit = optarg[0];
			break;
		case 's':
			u = strtouint(optarg);
			if (errno != 0) {
				MENU_ERROR("Invalid stop bit value.");
				return EINVAL;
			}
			uart_conf.stop_bits = u;
			break;
		case 'h':
			print_usage(argv[0]);
			return EXIT_SUCCESS;
		default:
			print_usage(argv[0]);
			return EINVAL;
		}
	}
	if (optind < argc) {
		uart_conf.dev = argv[optind];
	}

	// termios setup for stdin and UART dev
	int uart_fd = init_uart(&uart_conf);
	if (uart_fd == -1) {
		close_uart(uart_fd);
		return EXIT_FAILURE;
	}

	fprintf(stderr, "\033[2J\033[H\033[1;7m "
			"Connected to \033[4m%s\033[24m | %u %d%c%d | \033[22mExit: CTRL + A "
			"\033[m\n", uart_conf.dev, uart_conf.baud, uart_conf.data_bits,
			uart_conf.parity_bit, uart_conf.stop_bits);
	TTY_READY();

	struct pollfd poll_fds[] = {
		{uart_fd, POLLIN, 0},
		{STDIN_FILENO, POLLIN, 0}
	};
	char c = 0;
	int rw_len = 0;

	while (1) {

		poll(poll_fds, 2, -1);

		if (poll_fds[0].revents & POLLHUP) {	// if dev disconnects, exit
			close_uart(-1);
			fprintf(stderr, "\033[31m%s disconnected, exiting.\033[m\n", uart_conf.dev);
			return ENOENT;

		} else if (poll_fds[0].revents & POLLIN) { // incoming data from dev
			rw_len = read(uart_fd, &c, 1);
			if (rw_len < 0) {
				MENU_ERROR("Reading from UART failed...");
			}

			printf("%c", c);
			fflush(stdout);
		} else if (poll_fds[1].revents & POLLIN) { // incoming data from stdin
			rw_len = read(0, &c, 1);
			if (rw_len < 0) {
				MENU_ERROR("Reading from STDIN failed...");
			}

			// CTRL + A opens menu
			if (c == MENU) {
				int ret = menu(uart_fd, &uart_conf, poll_fds, 2);
				if (ret < 0) {
					close_uart(uart_fd);
					MENU_ERROR("Connection has been terminated.");
					exit(EXIT_SUCCESS);
				}

			} else {
				rw_len = write(uart_fd, &c, 1);
				if (rw_len < 0) {
					MENU_ERROR("Writing to UART failed...");
				}
			}
		}
	}
}
