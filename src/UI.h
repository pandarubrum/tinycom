#pragma once


int menu(int uart_fd, struct uart_conf_t *uart_conf, struct pollfd *poll_fds, int poll_fds_count);
