#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include "uart.h"

#define QUIT	0x01


static void print_usage(const char *prog)
{
	printf("Usage: %s [-b baud] [-d data_bits] [-p parity] [-s stop_bits] [device]\n", prog);
	printf("\nExample: %s -b 115200 -d 8 -p N -s 1 /dev/ttyUSB0\n\nAll flags are completely optional "
	       "and if dev path is not specified,\nan attempt at automatically finding a device will be made.\n", prog);
}

int main(int argc, char *argv[])
{
	const char *filename = strrchr(argv[0], '/');
	++filename;

	struct uart_conf_t uart_conf = {0};

	int opt;
	while ((opt = getopt(argc, argv, "b:d:p:s:h")) != -1) {
		switch (opt) {
		case 'b':
			uart_conf.baud = atoi(optarg);
			break;
		case 'd':
			uart_conf.data_bits = atoi(optarg);
			break;
		case 'p':
			uart_conf.parity_bit = optarg[0];
			break;
		case 's':
			uart_conf.stop_bits = atoi(optarg);
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

	printf("\e[2J\e[H\e[1;7m Connected to %s | %u %d%c%d | \e[22mExit: CTRL + A "
		"\e[m\n\n\e[35mTerminal is ready\e[m\n", uart_conf.dev, uart_conf.baud,
		uart_conf.data_bits, uart_conf.parity_bit, uart_conf.stop_bits);

	struct pollfd fds[] = {
		{uart_fd, POLLIN, 0},
		{STDIN_FILENO, POLLIN, 0}
	};
	char c = 0;
	int rw_len = 0;

	while (1) {

		poll(fds, 2, -1);

		if (fds[0].revents & POLLHUP) {	// if dev disconnects, exit
			fprintf(stderr, "\n\e[31m%s disconnected, exiting.\e[m\n", uart_conf.dev);
			close_uart(uart_fd);
			return ENOENT;

		} else if (fds[0].revents & POLLIN) { // incoming data from dev
			rw_len = read(uart_fd, &c, 1);
			WARN_HANDLER(rw_len <= 0, "Reading from UART failed...")

			printf("%c", c);
			fflush(stdout);
		} else if (fds[1].revents & POLLIN) { // incoming data from stdin
			rw_len = read(0, &c, 1);
			WARN_HANDLER(rw_len < 0, "Reading from STDIN failed")

			// CTRL + A exits program
			if (c == QUIT) {
				fprintf(stderr, "\n\e[35mConnection has been terminated.\e[m\n");
				close_uart(uart_fd);
				return EXIT_SUCCESS;
			} else {
				rw_len = write(uart_fd, &c, 1);
				WARN_HANDLER(rw_len < 0, "Writing to UART failed...")
			}
		}
	}
}
