#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <poll.h>
#include <fcntl.h>
#include <string.h>
#include "uart.h"
#include "UI.h"
#include "utils.h"

#define STATUS_BAR_SIZE	2U


static struct winsize ws;


static void status_bar(struct uart_conf_t *uart_conf, char *event_msg)
{
	char empty_event[1] = {'\0'};
	if (event_msg == NULL)
		event_msg = empty_event;

	fprintf(stderr, "\033[s\033[%dH\033[2K\033[1;7m %s \033[32m %u %d%c%d \033[m "
			"\033[1;32mMenu: C-a\033[m\t\033[1;33m%s\033[m\033[u", USHRT_MAX,
			uart_conf->dev, uart_conf->baud, uart_conf->data_bits,
			uart_conf->parity_bit, uart_conf->stop_bits, event_msg);
}

static int control_seq(char *final_str, char *ctrl_seq, ssize_t len,
		       unsigned *cur_pos, unsigned *last_idx)
{
	// return if it's not an escape sequence
	if (len < 3 || ctrl_seq[0] != ESC || ctrl_seq[1] != '[') {
		return -1;
	}

	switch (ctrl_seq[2]) {
	case 'C': // move cursor to the right
		if (*cur_pos < *last_idx) {
			fprintf(stderr, "\033[C");
			++*cur_pos;
		} else {
			// end of text reached
			putc('\a', stderr);
		}
		break;

	case 'D': // move cursor to the left
		if (*cur_pos > 0) {
			fprintf(stderr, "\033[D");
			--*cur_pos;
		} else {
			// beginning of text reached
			putc('\a', stderr);
		}
		break;

	case 'H': // move cursor to the start of the line
		if (*cur_pos > 0) {
			fprintf(stderr, "\033[%dD", *cur_pos);
			*cur_pos = 0;
		}
		break;

	case 'F': // move cursor to the end of the line
		if (*cur_pos < *last_idx) {
			fprintf(stderr, "\033[%dC", *last_idx - *cur_pos);
			*cur_pos = *last_idx;
		}
		break;

	case '3': // could be <DEL> key
		if (len < 4 || ctrl_seq[3] != '~') {
			return -1;
		}

		if (*cur_pos < *last_idx) {
			memmove(&final_str[*cur_pos], &final_str[*cur_pos + 1],
				*last_idx - *cur_pos);
			--*last_idx;
			final_str[*last_idx] = '\0';
			fprintf(stderr, "\033[s\033[K%s\033[u", final_str + *cur_pos);
		} else {
			// end of text reached
			putc('\a', stderr);
		}
		break;
	default:
		return -1;
	}
	return 0;
}

/* Paste ASCII file into UART TTY */
static int menu_paste_file(struct pollfd *poll_fds, nfds_t poll_fds_count,
			   struct uart_conf_t *uart_conf)
{
	char buf[PATH_MAX];
	char file_path[PATH_MAX];
	ssize_t len = -1;
	unsigned last_idx = 0;
	unsigned cur_pos = 0;
	int fd = -1;

	MENU_TITLE("Paste file");
	MENU_OPTS("Paste an ASCII file into the device's STDIN.\n\n"
		  "Menu options:\n\tC-a|ESC\texit menu");
	MENU_PROMPT("Specify the file path");

	do {
		poll(poll_fds, poll_fds_count, -1);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			uart_conf->fd = -1;
			MENU_ERROR("%s disconnected.", uart_conf->dev);
			return -1;
		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		// build a path
		len = read(STDIN_FILENO, buf, sizeof(buf));
		if (len < 0) {
			MENU_ERROR("Reading from STDIN failed...");
			continue;
		} else if (len > 1) {
			unsigned old_idx = last_idx;
			int ret = control_seq(file_path, buf, len, &cur_pos, &last_idx);
			if (ret == 0) {
				if (old_idx > last_idx) {
					status_bar(uart_conf, NULL);
				}
				continue;
			}
		}

		for (ssize_t i = 0; i < len; ++i) {
			switch (buf[i]) {
			case '\r':
			case '\n':
				if (last_idx == 0) {
					putc('\a', stderr);
					continue;
				}
				file_path[last_idx] = '\0';
				last_idx = 0;
				cur_pos = 0;
				break;

			case ESC:
			case MENU:
				status_bar(uart_conf, NULL);
				TTY_READY;
				return 0;

			// backspace
			case '\b':
			case DEL:
				if (cur_pos == 0) {
					putc('\a', stderr);
					continue;
				}

				if (last_idx == PATH_MAX - 1) {
					status_bar(uart_conf, NULL);
				}

				if (cur_pos < last_idx) {
					memmove(&file_path[cur_pos - 1], &file_path[cur_pos],
						last_idx - cur_pos + 1);

					file_path[last_idx - 1] = '\0';
					fprintf(stderr, "\033[s\033[D\033[K%s\033[%uD",
						file_path + cur_pos - 1, last_idx - cur_pos);
				} else {
					fprintf(stderr, "\033[D\033[P");
				}
				--last_idx;
				--cur_pos;
				continue;

			default:
				// discard non-printable chars
				if (buf[i] < ' ' || buf[i] > '~') {
					putc('\a', stderr);
					continue;
				}

				if (last_idx >= PATH_MAX - 1) {
					status_bar(uart_conf, "Exceeded max path length, last character ignored!");
					continue;
				}

				if (cur_pos < last_idx) {
					memmove(&file_path[cur_pos + 1], &file_path[cur_pos],
						last_idx - cur_pos + 1);
					file_path[cur_pos] = buf[i];
					file_path[last_idx + 1] = '\0';
					fprintf(stderr, "\033[s\033[K%s\033[u\033[C", file_path + cur_pos);
				} else {
					file_path[last_idx] = buf[i];
					fprintf(stderr, "%c", buf[i]);
				}
				++last_idx;
				++cur_pos;

				continue;
			}

			fd = open(file_path, O_RDONLY);
			if (fd >= 0) {
				break;
			}
			MENU_ERROR("Error opening file \"%s\"", file_path);
			perror(__func__);
			MENU_PROMPT("Specify the file path");
		}
	} while (fd < 0);

	MENU_MSG("Pasting file into UART TTY...");
	while (true) {

		len = read(fd, buf, sizeof(buf));
		// EOF
		if (len == 0) {
			MENU_MSG("File \"%s\" pasted successfully.", file_path);
			break;
		}
		// Error reading file
		if (len < 0) {
			MENU_ERROR("Error reading file.");
			perror(__func__);
			break;
		}

		len = write(uart_conf->fd, buf, len);
		// Error writing file
		if (len <= 0 && errno != 0) {
			MENU_ERROR("Error pasting file.");
			perror(__func__);
			break;
		}
	}

	TTY_READY;
	close(fd);
	status_bar(uart_conf, NULL);
	return 0;
}

/* Set baud in the interactive menu, poll() checks if dev was disconnected */
static int menu_baud(struct pollfd *poll_fds, nfds_t poll_fds_count, struct uart_conf_t *uart_conf)
{
	int ret = -1;
	char temp_buf[1024];
	char final_buf[11] = {0};
	unsigned last_idx = 0;
	unsigned cur_pos = 0;

	MENU_TITLE("Baud selection");
	MENU_OPTS("Valid values: 0 - %u\n"
		  "(0 sets baud to default value 115200)\n\n"
		  "\tC-a|ESC\tback to terminal\n"
		  "\tq\tquit", UINT_MAX);
	MENU_PROMPT("Enter your choice");

	while (true) {

		poll(poll_fds, poll_fds_count, -1);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			uart_conf->fd = -1;
			MENU_ERROR("%s disconnected.", uart_conf->dev);
			return -1;
		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		ssize_t len = read(STDIN_FILENO, temp_buf, sizeof(temp_buf));
		if (len < 0) {
			MENU_ERROR("Reading from STDIN failed...");
			continue;
		} else if (len > 1) {
			unsigned old_idx = last_idx;
			ret = control_seq(final_buf, temp_buf, len, &cur_pos, &last_idx);
			if (ret == 0) {
				if (old_idx > last_idx) {
					status_bar(uart_conf, NULL);
				}
				continue;
			}
		}

		for (ssize_t i = 0; i < len; ++i) {
			switch (temp_buf[i]) {
			default:
				// discard non-printable chars
				if (temp_buf[i] < ' ' || temp_buf[i] > '~') {
					putc('\a', stderr);
					continue;
				}

				if (last_idx >= sizeof(final_buf) - 1) {
					status_bar(uart_conf, "Too many digits, last digit ignored!");
					continue;
				}

				if (cur_pos < last_idx) {
					memmove(&final_buf[cur_pos + 1], &final_buf[cur_pos], last_idx - cur_pos + 1);
					final_buf[cur_pos] = temp_buf[i];
					final_buf[last_idx + 1] = '\0';
					fprintf(stderr, "\033[s\033[K%s\033[u\033[C", final_buf + cur_pos);
				} else {
					final_buf[last_idx] = temp_buf[i];
					fprintf(stderr, "%c", temp_buf[i]);
				}
				++last_idx;
				++cur_pos;

				break;
			case '\n':
			case '\r':
				if (last_idx == 0) {
					putc('\a', stderr);
					continue;
				}
				final_buf[last_idx] = '\0';
				last_idx = 0;
				cur_pos = 0;

				unsigned u = strtouint(final_buf);
				if (errno != 0) {
					MENU_ERROR("Invalid input");
					MENU_PROMPT("Enter your choice");
					continue;
				}

				ret = set_baud(uart_conf->fd, &u, true);
				if (ret < 0) {
					MENU_ERROR("Error setting baud, try again");
					MENU_PROMPT("Enter your choice");
					continue;
				}

				uart_conf->baud = u;
				status_bar(uart_conf, NULL);
				MENU_MSG("Baud was set to %u.", u);
				TTY_READY;
				return 0;

			case ESC:
			case MENU:
				status_bar(uart_conf, NULL);
				TTY_READY;
				return 0;

			// backspace
			case '\b':
			case DEL:
				if (cur_pos == 0) {
					putc('\a', stderr);
					continue;
				}

				if (last_idx == sizeof(final_buf) - 1) {
					status_bar(uart_conf, NULL);
				}

				if (cur_pos < last_idx) {
					memmove(&final_buf[cur_pos - 1], &final_buf[cur_pos], last_idx - cur_pos + 1);

					final_buf[last_idx - 1] = '\0';
					fprintf(stderr, "\033[s\033[D\033[K%s\033[%uD",
						final_buf + cur_pos - 1, last_idx - cur_pos);
				} else {
					fprintf(stderr, "\033[D\033[P");
				}
				--last_idx;
				--cur_pos;
				continue;

			case 'q':
				return -1;
			}
		}
	}
}

/* Set data bits in the interactive menu, poll() checks if the dev was disconnected */
static int menu_data_bits(struct pollfd *poll_fds, nfds_t poll_fds_count,
			  struct uart_conf_t *uart_conf)
{
	int ret = -1;
	char c = '\0';
	char buf[2] = {0};
	unsigned u = 0;


	MENU_TITLE("Data bit selection");
	MENU_OPTS("Please choose an option:\n\n"
		  "\t8|0\t8 bits (default)\n"
		  "\t7\t7 bits\n"
		  "\t6\t6 bits\n"
		  "\t5\t5 bits\n\n"
		  "\tC-a|ESC\tback to terminal\n"
		  "\tq\tquit");
	MENU_PROMPT("Enter your choice");

	while (true) {
		poll(poll_fds, poll_fds_count, -1);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			uart_conf->fd = -1;
			MENU_ERROR("%s disconnected.", uart_conf->dev);
			return -1;
		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		read(STDIN_FILENO, &c, 1);

		switch (c) {
		default:
			INVAL_INPUT("%c", c);
			MENU_PROMPT("Enter your choice");
			/* fallthrough */
		case ENTER:
			continue;
		case '0':
		case '8':
		case '7':
		case '6':
		case '5':
			buf[0] = c;
			u = strtouint(buf);
			if (errno != 0) {
				INVAL_INPUT("%c", c);
				MENU_PROMPT("Enter your choice");
				continue;
			}

			ret = set_data_bits(uart_conf->fd, &u, true);
			if (ret < 0) {
				MENU_ERROR("Error setting data bits, try again");
				MENU_PROMPT("Enter your choice");
				continue;
			}

			uart_conf->data_bits = u;
			status_bar(uart_conf, NULL);
			MENU_MSG("Data bits set to %u successfully.", u);
			/* fallthrough */
		case MENU:
		case ESC:
			TTY_READY;
			return 0;
		case 'q':
			return -1;
		}
	}
}

/* Set parity bit in the interactive menu, poll() checks if the dev was disconnected */
static int menu_parity_bit(struct pollfd *poll_fds, nfds_t poll_fds_count,
			   struct uart_conf_t *uart_conf)
{
	char c = 0;
	int ret = 0;

	MENU_TITLE("Parity bit selection");
	MENU_OPTS("Please choose an option:\n\n"
		  "\tN|n|0\tnone (default)\n"
		  "\tO|o\todd\n"
		  "\tE|e\teven\n"
		  "\tM|m\tmark parity\n"
		  "\tS|s\tspace parity\n\n"
		  "\tC-a|ESC\tback to terminal\n"
		  "\tq\tquit");
	MENU_PROMPT("Enter your choice");

	while (true) {
		poll(poll_fds, poll_fds_count, -1);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			uart_conf->fd = -1;
			MENU_ERROR("%s disconnected.", uart_conf->dev);
			return -1;
		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		read(STDIN_FILENO, &c, 1);

		switch (c) {
		default:
			INVAL_INPUT("%c", c);
			MENU_PROMPT("Enter your choice");
			/* fallthrough */
		case ENTER:
			continue;
		case '0':
			c = 'N';
			/* fallthrough */
		case 'N':
		case 'n':
		case 'O':
		case 'o':
		case 'E':
		case 'e':
		case 'M':
		case 'm':
		case 'S':
		case 's':
			ret = set_parity_bit(uart_conf->fd, &c, true);
			if (ret < 0) {
				INVAL_INPUT("%c", c);
				MENU_PROMPT("Enter your choice");
				continue;
			}

			uart_conf->parity_bit = toupper(c);
			status_bar(uart_conf, NULL);
			MENU_MSG("Parity bit set to %c successfully.", uart_conf->parity_bit);
			/* fallthrough */
		case MENU:
		case ESC:
			TTY_READY;
			return 0;
		case 'q':
			return -1;
		}
	}
}

/* Set stop bits in the interactive menu, poll() checks if the dev was disconnected */
static int menu_stop_bits(struct pollfd *poll_fds, nfds_t poll_fds_count,
			  struct uart_conf_t *uart_conf)
{
	int ret = -1;
	unsigned u = 0;
	char c = 0;
	char buf[2] = {0};

	MENU_TITLE("Stop bit selection");
	MENU_OPTS("Please choose an option:\n\n"
		  "\t1|0\t1 bit (default)\n"
		  "\t2\t2 bits\n\n"
		  "\tC-a|ESC\tback to terminal\n"
		  "\tq\tquit");
	MENU_PROMPT("Enter your choice");

	while (true) {
		poll(poll_fds, poll_fds_count, 0);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			uart_conf->fd = -1;
			MENU_ERROR("%s disconnected.", uart_conf->dev);
			return -1;
		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		read(STDIN_FILENO, &c, 1);

		switch (c) {
		default:
			buf[0] = c;
			u = strtouint(buf);

			if (errno != 0) {
				INVAL_INPUT("%c", c);
				MENU_PROMPT("Enter your choice");
				continue;
			}

			ret = set_stop_bits(uart_conf->fd, &u, true);
			if (ret < 0) {
				MENU_ERROR("Error setting stop bits, try again.");
				MENU_PROMPT("Enter your choice");
				continue;
			}

			uart_conf->stop_bits = u;
			status_bar(uart_conf, NULL);
			MENU_MSG("Stop bit set to %u successfully.", u);
			/* fallthrough */
		case MENU:
		case ESC:
			TTY_READY;
			return 0;
		case 'q':
			return -1;
		case ENTER:
			continue;
		}
	}
}

/*
 * Set up TTY to reserve rows for the status bar.
 * This function MUST be called only once.
 */
void init_ui(struct uart_conf_t *uart_conf)
{
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
		perror(__func__);
		return;
	}

	fprintf(stderr, "\033[2J\033[1;%dr", ws.ws_row - STATUS_BAR_SIZE);
	fprintf(stderr, "\033[2mWelcome to tinycom!");
	status_bar(uart_conf, NULL);

	TTY_READY;
}

/*
 * Filter out escape sequences that interfere with the non-scrollable region of TTY where the
 * status bar is.
 */
ssize_t printfUI(struct uart_conf_t *uart_conf, char *buf, ssize_t rw_len)
{
	ssize_t len = 0;
	bool restart_status_bar = false;

	for (ssize_t i = 0; i < rw_len; ++i) {
		if (buf[i] != ESC) {
			continue;
		}

		++i;
		if (i >= rw_len)
			break;

		// \033c - clear screen and reset VT220 settings (hard reset)
		if (buf[i] == 'c') {
			buf[i - 1] = 0;	// ESC
			buf[i] = 0;	// c
			continue;
		}

		// from now on, only interested in escape sequences that start with \033[
		if (buf[i] != '[') {
			continue;
		}

		++i;
		if (i >= rw_len)
			break;

		// \033[J (clears beneath the cursor), re-print status bar
		if (buf[i] == 'J') {
			restart_status_bar = true;
			continue;
		}

		if (i + 1 >= rw_len)
			break;

		// \033[NJ, re-print status bar
		if (buf[i + 1] == 'J') {
			restart_status_bar = true;
			++i;
			continue;
		}

		// \033[!p (soft reset), skip resetting terminal settings
		if (buf[i] == '!' && buf[i + 1] == 'p') {
			buf[i - 2] = 0;	// ESC
			buf[i - 1] = 0;	// [
			buf[i] = 0;	// !
			buf[i + 1] = 0;	// p
			++i;
			continue;
		}

		/*
		 * \033[N;MH, prevent the cursor going to the last row where the status bar is,
		 * which is also important for the device that relies on this to get window size
		 * (\033[MAX;MAXH + \033[6n gets total window size)
		 */

		if (buf[i] < '0' || buf[i] > '9') {
			continue;
		}
		ssize_t replace_idx = i;
		char row_str[16];
		int row_len = 0;
		bool first_nums = true;

		for (; i < rw_len; ++i) {

			if (buf[i] >= '0' && buf[i] <= '9') {
				// only take in consideration the N digits in \033[N;MH for rows
				if (first_nums) {
					row_str[row_len] = buf[i];
					++row_len;
				}

			} else if (buf[i] == ';') {
				first_nums = false;
				row_str[row_len] = '\0';

			} else if (buf[i] == 'H') {
				unsigned rows = strtouint(row_str);
				if (rows == 0 && errno != 0) {
					break;
				}

				// check if cursor will jump into rows reserved for status bar
				if (rows > ws.ws_row - STATUS_BAR_SIZE) {
					snprintf(row_str, sizeof(row_str), "%0*u", row_len,
						 ws.ws_row - STATUS_BAR_SIZE);
					memcpy(buf + replace_idx, row_str, row_len);
				}

				break;
			} else {
				break;
			}
		}
	}

	len = write(STDOUT_FILENO, buf, rw_len);

	if (restart_status_bar) {
		status_bar(uart_conf, NULL);
	}

	return len;
}

/*
 * Interactive menu for setting baud, data, parity and stop bits. poll() checks if there is
 * incoming data from UART while the menu is on, and if the dev disconnected.
 * Data incoming from the dev will be buffered while interacting with the menu, and shown on the
 * screen after going back to terminal.
 */
int menu(struct uart_conf_t *uart_conf, struct pollfd *poll_fds, int poll_fds_count)
{
	char c = '\0';
	bool incoming_data = false;

	MENU_TITLE("tinycom menu");
	MENU_OPTS("Menu options:\n"
		  "\tb\tset baud\n"
		  "\td\tset data bits\n"
		  "\tp\tset parity bit\n"
		  "\ts\tset stop bits\n"
		  "\tv\tpaste an ASCII file\n\n"
		  "\tC-a|ESC\texit menu\n"
		  "\tq\tquit");
	MENU_PROMPT("Enter your choice");

	while (true) {

		poll(poll_fds, poll_fds_count, -1);

		/* Device disconnected */
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			uart_conf->fd = -1;
			MENU_ERROR("%s disconnected.", uart_conf->dev);
			return -1;

		/* Data coming from UART */
		} else if (!incoming_data && poll_fds[UART_PFD].revents & POLLIN) {
			incoming_data = true;
			status_bar(uart_conf, "Data incoming from UART dev.");

		/* Data coming from user */
		} else if (poll_fds[STDIN_PFD].revents & POLLIN) {
			read(STDIN_FILENO, &c, 1);

			switch (c) {
			default:
				INVAL_INPUT("%c", c);
				MENU_PROMPT("Enter your choice");
				/* fallthrough */
			case ENTER:
				putc('\a', stderr);
				continue;
			case 'v':
				return menu_paste_file(poll_fds, poll_fds_count, uart_conf);
			case 'b':
				return menu_baud(poll_fds, poll_fds_count, uart_conf);
			case 'd':
				return menu_data_bits(poll_fds, poll_fds_count, uart_conf);
			case 'p':
				return menu_parity_bit(poll_fds, poll_fds_count, uart_conf);
			case 's':
				return menu_stop_bits(poll_fds, poll_fds_count, uart_conf);
			case MENU:
			case ESC:
				status_bar(uart_conf, NULL);
				TTY_READY;
				return 0;
			case 'q':
				return -1;
			}
		}
	}
}

void close_ui(void)
{
	// clean status bar
	fprintf(stderr, "\033[!p\033[0J");
}
