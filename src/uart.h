#pragma once

// Struct that will contain all arguments from the user, must be initialized in main
struct uart_conf_t {
	char *dev;
	unsigned int baud;
	unsigned int data_bits;
	char parity_bit;
	unsigned int stop_bits;
};

int set_baud(int uart_fd, unsigned *baud, bool set_now);
int set_data_bits(int uart_fd, unsigned *data_bits, bool set_now);
int set_parity_bit(int uart_fd, char *parity_bit, bool set_now);
int set_stop_bits(int uart_fd, unsigned *stop_bits, bool set_now);

int init_uart(struct uart_conf_t *uart_conf);
void close_uart(int uart_fd);
