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


/*
 * tinycom - a minimal serial communication program
 *
 * Allows bidirectional communication with a serial device.
 * An interactive menu allows for tweaking settings, which can
 * also be done with flags at program startup.
 *
 * Usage: build/tinycom -b 115200 -d 8 -p N -s 1 /dev/ttyUSB0
 */
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

	/* Initialize UART device and report on failure */
	int ret = init_uart(&uart_conf);
	if (ret == -1) {
		return EXIT_FAILURE;
	}

	// status bar
	init_ui(&uart_conf);

	/* Monitor both UART device and stdin for incoming data */
	struct pollfd poll_fds[] = {
		[UART_PFD] = {uart_conf.fd, POLLIN, 0},
		[STDIN_PFD] = {STDIN_FILENO, POLLIN, 0}
	};
	char buf[4096];
	ssize_t rw_len = 0;

	/* Manage communication between UART device and host */
	while (true) {
		poll(poll_fds, ARRAY_SIZE(poll_fds), -1);

		/* Device disconnected */
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			close_uart(-1);
			MENU_ERROR("%s disconnected, exiting.", uart_conf.dev);
			close_ui();
			return ENOENT;

		/* Data from device */
		} else if (poll_fds[UART_PFD].revents & POLLIN) {
			rw_len = read(uart_conf.fd, buf, sizeof(buf));
			if (rw_len < 0) {
				MENU_ERROR("Reading from UART failed...");
			}

			printfUI(&uart_conf, buf, rw_len);

		/* Data from user */
		} else if (poll_fds[STDIN_PFD].revents & POLLIN) {
			rw_len = read(STDIN_FILENO, buf, sizeof(buf));
			if (rw_len < 0) {
				MENU_ERROR("Reading from STDIN failed...");
			}

			/* CTRL + A opens menu */
			if (rw_len == 1 && buf[0] == MENU) {
				ret = menu(&uart_conf, poll_fds, ARRAY_SIZE(poll_fds));
				if (ret < 0) {
					close_uart(uart_conf.fd);
					MENU_MSG("Connection has been terminated, exiting.");
					close_ui();
					return EXIT_SUCCESS;
				}

			} else {
				rw_len = write(uart_conf.fd, buf, rw_len);
				if (rw_len < 0) {
					MENU_ERROR("Writing to UART failed...");
				}
			}
		}
	}
}
