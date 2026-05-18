#pragma once


/*
 * Interactive menu:
 *
 * uart_fd		fd of the UART device
 * uart_conf		pointer to the configuration struct where dev, baud, data, parity and
 * 			stop bits values are stored
 * poll_fds		pointer to the poll() structs needed to check if there is data waiting
 * 			from the dev, from the user's input, or report if the dev was disconnected
 * poll_fds_count	number of fds of the poll() structs to monitor for data
 *
 * returns:	0 on success, -1 on failure
 */
int menu(int uart_fd, struct uart_conf_t *uart_conf, struct pollfd *poll_fds, int poll_fds_count);
