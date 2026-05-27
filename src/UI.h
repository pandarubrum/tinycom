#pragma once


/*
 * Prepare UI, clean the screen, print status bar, and set VT220 terminal settings.
 */
void init_ui(struct uart_conf_t *uart_conf);

/*
 * Print helper function that deals with filtering some VT220 escape sequences which interfere
 * with the UI. Printing relies on size (rw_len) and not C-style null-terminated strings.
 */
ssize_t printfUI(struct uart_conf_t *uart_conf, char *buf, ssize_t rw_len);

/*
 * Interactive menu:
 *
 * uart_conf		pointer to the configuration struct where dev, baud, data, parity and
 * 			stop bits values are stored
 * poll_fds		pointer to the poll() structs needed to check if there is data waiting
 * 			from the dev, from the user's input, or report if the dev was disconnected
 * poll_fds_count	number of fds of the poll() structs to monitor for data
 *
 * returns:	0 on success, -1 on failure
 */
int menu(struct uart_conf_t *uart_conf, struct pollfd *poll_fds, int poll_fds_count);

/*
 * Close UI, clean the screen and reset VT220 terminal settings.
 */
void close_ui(void);
