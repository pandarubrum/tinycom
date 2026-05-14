#pragma once

#define	ERROR_HANDLER(COND, MSG, CLEANUP)	if (COND) { \
							perror(MSG); \
							CLEANUP; \
							exit(errno); \
						}
#define	WARN_HANDLER(COND, MSG)			if (COND) { \
							perror(MSG); \
						}

// Struct that will contain all arguments from the user, must be initialized in main
struct uart_conf_t {
	char *dev;
	unsigned int baud;
	int data_bits;
	char parity_bit;
	int stop_bits;
};

bool set_baud(int uart_fd, unsigned *baud, bool set_now);

int init_uart(struct uart_conf_t *uart_conf);

void close_uart(int uart_fd);
