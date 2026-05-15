#pragma once

#define LOG_ERROR(fmt, ...) \
    fprintf(stderr, "\e[31m%s:%d (%s): \e[1m" fmt "\e[m\n", \
            __FILE__, __LINE__, __func__, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    fprintf(stderr, "\e[35m%s:%d (%s): \e[1m" fmt "\e[m\n", \
            __FILE__, __LINE__, __func__, ##__VA_ARGS__)

// Struct that will contain all arguments from the user, must be initialized in main
struct uart_conf_t {
	char *dev;
	unsigned int baud;
	int data_bits;
	char parity_bit;
	int stop_bits;
};

int set_baud(int uart_fd, unsigned *baud, bool set_now);

int init_uart(struct uart_conf_t *uart_conf);

void close_uart(int uart_fd);
