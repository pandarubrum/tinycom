#pragma once

#include <stdbool.h>


/* UART configuration struct */
struct uart_conf_t {
	char *dev;		/* device path, e.g. "/dev/ttyUSB0" */
	unsigned int baud;	/* baud rate, e.g. 115200 */
	unsigned int data_bits;	/* data bits, 5, 6, 7 or 8 */
	char parity_bit;	/* parity bit: N (none), E (even), O (odd), M (mark), S (space) */
	unsigned int stop_bits;	/* stop bits: 1 or 2 */
	int fd;			/* ttyUSB file desc */
};

/*
 * Set baud rate:
 *
 * uart_fd	fd of the UART device
 * *baud	pointer to baud rate value in the uart_conf_t struct, if 0 is modified to 115200
 * set_now	flag to immediately apply settings to TTY (according to manual page, it's not good
 * 		to apply changes frequently so this is set to true only from interactive menu)
 *
 * returns:	0 on success, -1 on failure
 */
int set_baud(int uart_fd, unsigned *baud, bool set_now);

/*
 * Set data bits:
 *
 * uart_fd	fd of the UART device
 * *data_bits	pointer to data bits value in the uart_conf_t struct, if 0 is modified to 8
 * set_now	flag to immediately apply settings to TTY (according to manual page, it's not good
 * 		to apply changes frequently so this is set to true only from interactive menu)
 *
 * returns:	0 on success, -1 on failure
 */
int set_data_bits(int uart_fd, unsigned *data_bits, bool set_now);

/*
 * Set parity bit:
 *
 * uart_fd	fd of the UART device
 * *parity_bit	pointer to parity bit value in the uart_conf_t struct, if '\0' is modified to N
 * set_now	flag to immediately apply settings to TTY (according to manual page, it's not good
 * 		to apply changes frequently so this is set to true only from interactive menu)
 *
 * returns:	0 on success, -1 on failure
 */
int set_parity_bit(int uart_fd, char *parity_bit, bool set_now);

/*
 * Set stop bits:
 *
 * uart_fd	fd of the UART device
 * *stop_bits	pointer to stop bits value in the uart_conf_t struct, if 0 is modified to 1
 * set_now	flag to immediately apply settings to TTY (according to manual page, it's not good
 * 		to apply changes frequently so this is set to true only from interactive menu)
 *
 * returns:	0 on success, -1 on failure
 */
int set_stop_bits(int uart_fd, unsigned *stop_bits, bool set_now);

/*
 * Initialize UART and configure terminal for serial communication:
 *
 * uart_conf	configuration struct where dev, baud, data, parity and stop bits values are stored
 *
 * returns:	fd on success, -1 on failure
 */
int init_uart(struct uart_conf_t *uart_conf);

/*
 * Restore original settings for terminal, and if UART is still connected, restore original UART
 * settings and close UART device as well.
 *
 * Always call this before exiting to leave the terminal in a usable state.
 * Restores both stdin and UART device to their original settings.
 *
 * Prints messages informing the user if the operations were carried out successfully or not.
 *
 * uart_fd	fd of the UART device
 */
void close_uart(int uart_fd);
