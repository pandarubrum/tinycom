#pragma once


/*
 * Prepare UI, clean the screen, print status bar, and set VT220 terminal settings.
 */
void init_ui(struct uart_conf_t *uart_conf);

/*
 * Deal with reading data from device, filtering some escape sequences which interfere with the UI,
 * and writing device data to terminal.
 *
 * uart_conf		pointer to the configuration struct where dev, baud, data, parity, stop
 * 			bits and device file desc are stored
 *
 * returns:	count of written chars on success, -1 on failure
 */
ssize_t process_dev_data(struct uart_conf_t *uart_conf);

/*
 * Interactive menu:
 *
 * uart_conf		pointer to the configuration struct where dev, baud, data, parity, stop
 * 			bits and device file desc are stored
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
