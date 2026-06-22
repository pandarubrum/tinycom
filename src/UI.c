#define _GNU_SOURCE
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

#define BETWEEN(var, x, y)	(var >= x && var <= y)
#define STATUS_BAR_SIZE		2U
#define MENU_WIDTH		40U


static struct winsize ws;


/*
 * Draw the status bar at the bottom of the screen
 *
 * It dynamically changes when a change in UART parameters is made, or when the user switches
 * between the terminal and the menu (set by is_TTY_mode).
 *
 * The status bar also shows event messages, if specified.
 */
static void status_bar(struct uart_conf_t *uart_conf, char *event_msg, bool is_TTY_mode)
{
	char empty_event[1] = {0};
	if (event_msg == NULL) {
		event_msg = empty_event;
	}

	char menu_shortcut[] = "Menu: Alt-M";
	// calculate column where to start inserting text aligned to the right
	unsigned right_align_offset = ws.ws_col - sizeof(menu_shortcut);
	if (is_TTY_mode) {
		// TTY mode
		fprintf(stderr, "\033[s\033[m\033[%dH\033[1;39;7m %s \033[32m %u %d%c%d "
				"\033[m\033[48;5;238m\033[K \033[1;33m%s\033[22;39m\033[%dG"
				"\033[22;2;48;5;238m %s \033[m\033[u",
				USHRT_MAX, uart_conf->dev, uart_conf->baud, uart_conf->data_bits,
				uart_conf->parity_bit, uart_conf->stop_bits, event_msg,
				right_align_offset, menu_shortcut);
	} else {
		// menu mode
		fprintf(stderr, "\033[s\033[m\033[%dH\033[48;5;238m\033[2K\033[2m %s  %u %d%c%d  "
				"\033[22;1;33m%s\033[%dG"
				"\033[22;1;39;7m %s \033[m\033[u",
				USHRT_MAX, uart_conf->dev, uart_conf->baud, uart_conf->data_bits,
				uart_conf->parity_bit, uart_conf->stop_bits, event_msg,
				right_align_offset, menu_shortcut);
	}
}

/*
 * Draw the menu on screen
 *
 * The menu title, menu options and the menu table width must be specified in the args.
 * The text has to be adjusted manually so that it doesn't exceed the menu width.
 * The height is dynamically determined by the number of newlines in the opts string.
 */
static void menu_ui(char *title, char *opts, unsigned width)
{
#define	PADDING_SIZE	5
	// print menu title
	fprintf(stderr, "\n\n\033[7m%-*s\033[m\033[G\033[1;7m %s \033[m\n", width, "", title);
	int count = 0;
	// calculate newlines and draw bg for menu
	for (char *p = opts; *p; p++) {
		if (*p == '\n') {
			count++;
		}
	}
	for (int i = 0; i < count + PADDING_SIZE; ++i) {
		fprintf(stderr, "\033[48;5;238m%-*s\033[m\n", width, "");
	}
	// print menu content
	fprintf(stderr, "\033[%dA", count + 5);
	fprintf(stderr, "\n\033[48;5;238m%s\n\n    \033[1mESC/Alt-M\033[22m exit menu, "
			"\033[1mAlt-Q\033[22m quit\033[m\n\n\n", opts);
#undef	PADDING_SIZE
}

/*
 * Process a few control sequences
 *
 * Minimal line-editing implementation which allows some functionalities like moving cursor
 * left/right, at the beginning/end of line, and <Del>.
 */
static int control_seq(char *final_str, char *ctrl_seq, ssize_t len,
		       unsigned *cur_pos, unsigned *last_idx)
{
	// return if it's not a control sequence triggered by a single keypress
	if (!(ctrl_seq[0] == ESC && len > 2)) {
		return -1;	// not a control sequence, return failure (e.g., pasted filename)
	}

	// return if it's not CSI (ESC + [)
	if (ctrl_seq[1] != '[') {
		putc('\a', stderr);
		return 0;	// control sequence but not implemented, return success early
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

	case 'H': // move cursor to the start of the line <Home>
		if (*cur_pos > 0) {
			fprintf(stderr, "\033[%dD", *cur_pos);
			*cur_pos = 0;
		}
		break;

	case 'F': // move cursor to the end of the line <End>
		if (*cur_pos < *last_idx) {
			fprintf(stderr, "\033[%dC", *last_idx - *cur_pos);
			*cur_pos = *last_idx;
		}
		break;

	case '3': // delete <Del>
		if (len < 4 || ctrl_seq[3] != '~') {
			return -1;
		}

		if (*cur_pos < *last_idx) {
			memmove(final_str + *cur_pos, final_str + *cur_pos + 1,
				*last_idx - *cur_pos);
			--*last_idx;
			final_str[*last_idx] = '\0';
			fprintf(stderr, "\033[s\033[K%s\033[u", final_str + *cur_pos);
		} else {
			// end of text reached
			putc('\a', stderr);
		}
		break;

	default: // unimplemented
		putc('\a', stderr);
		break;
	}

	return 0;
}

/*
 * Paste ASCII file into UART TTY
 *
 * A file will be opened by specifying its path and written into the device's STDIN.
 */
static int menu_paste_file(struct pollfd *poll_fds, nfds_t poll_fds_count,
			   struct uart_conf_t *uart_conf)
{
	bool incoming_data = false;
	char buf[PATH_MAX];
	char file_path[PATH_MAX];
	ssize_t len = -1;
	unsigned last_idx = 0;
	unsigned cur_pos = 0;
	int fd = -1;

	char title[] = "Paste file";
	char opts[] = "    Paste an ASCII file into the\n    device's STDIN.";
	menu_ui(title, opts, MENU_WIDTH);
	MENU_PROMPT("Specify the file path");

	do {
		poll(poll_fds, poll_fds_count, -1);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			uart_conf->fd = -1;
			MENU_ERROR("\n%s disconnected.", uart_conf->dev);
			return -1;

		} else if (!incoming_data && poll_fds[UART_PFD].revents & POLLIN) {
			incoming_data = true;
			status_bar(uart_conf, "Data incoming from device.", false);

		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		// build a path
		len = read(STDIN_FILENO, buf, sizeof(buf));
		if (len < 0) {
			MENU_PERROR("\nReading from STDIN failed");
			continue;
		// esc seq or pasted path
		} else if (len > 1) {
			unsigned old_idx = last_idx;
			int ret = control_seq(file_path, buf, len, &cur_pos, &last_idx);
			if (ret == 0) {
				if (old_idx > last_idx) {
					// reset all events in status bar if chars get deleted
					incoming_data = false;
					status_bar(uart_conf, NULL, false);
				}
				continue;
			}
		}

		for (ssize_t i = 0; i < len; ++i) {
			switch (buf[i]) {
			case '\r':
			case '\n':
				/* reset event msgs in the status bar. if not cleared here, in the
				   next iter the stale value wouldn't be cleared */
				incoming_data = false;
				status_bar(uart_conf, NULL, false);
				if (last_idx == 0) {
					putc('\a', stderr);
					continue;
				}
				file_path[last_idx] = '\0';
				last_idx = 0;
				cur_pos = 0;
				break;

			case ESC:
				// if either ESC or Alt-M, close menu
				if (len == 1 || (len == 2 && buf[1] == 'm')) {
					status_bar(uart_conf, NULL, true);
					MENU_END;
					return 0;
				// quit the program
				} else if (len == 2 && buf[1] == 'q') {
					fprintf(stderr, "\033[31mquit\033[m\n");
					return -1;
				} else {
					putc('\a', stderr);
					continue;
				}
			// backspace
			case '\b':
			case BACKSPACE:
				if (cur_pos == 0) {
					putc('\a', stderr);
					continue;
				}

				if (last_idx == PATH_MAX - 1) {
					status_bar(uart_conf, NULL, false);
					incoming_data = false;
				}

				if (cur_pos < last_idx) {
					memmove(file_path + cur_pos - 1, file_path + cur_pos,
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
					status_bar(uart_conf, "Path too long, any extra character "
						   "will be ignored!", false);
				putc('\a', stderr);
					continue;
				}

				if (cur_pos < last_idx) {
					memmove(file_path + cur_pos + 1, file_path + cur_pos,
						last_idx - cur_pos + 1);
					file_path[cur_pos] = buf[i];
					file_path[last_idx + 1] = '\0';
					fprintf(stderr, "\033[s\033[K%s\033[u\033[C",
							file_path + cur_pos);
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
			MENU_PERROR("\nError opening file \"%s\"", file_path);
			putc('\a', stderr);
			MENU_PROMPT("Specify the file path");
		}
	} while (fd < 0);

	MENU_INFO("\nPasting file into UART TTY...");
	while (true) {

		len = read(fd, buf, sizeof(buf));
		// EOF
		if (len == 0) {
			MENU_INFO("File \"%s\" pasted successfully.", file_path);
			break;
		}
		// Error reading file
		if (len < 0) {
			MENU_PERROR("Error reading file");
			break;
		}

		len = write(uart_conf->fd, buf, len);
		// Error writing file
		if (len <= 0 && errno != 0) {
			MENU_PERROR("Error pasting file");
			break;
		}
	}

	MENU_END;
	close(fd);
	status_bar(uart_conf, NULL, true);
	return 0;
}

/*
 * Set baud rate in the interactive menu
 *
 * All changes made via menu/submenus are applied immediately via tcsetattr().
 */
static int menu_baud(struct pollfd *poll_fds, nfds_t poll_fds_count, struct uart_conf_t *uart_conf)
{
	bool incoming_data = false;
	int ret = -1;
	char temp_buf[256];
	char final_buf[11];
	unsigned last_idx = 0;
	unsigned cur_pos = 0;

	char title[] = "Baud rate selection";
	char opts[256];

// BAUD_MAX is not POSIX nor C standard
#ifndef BAUD_MAX
#ifdef	SPEED_MAX
#define BAUD_MAX SPEED_MAX
#elif defined(__MAX_BAUD)
#define BAUD_MAX __MAX_BAUD
#else
#define BAUD_MAX UINT_MAX
#endif
#endif
	snprintf(opts, sizeof(opts), "    \033[1m0 - %u\033[22m value range\n"
		 "    (\033[1m0\033[22m is for default - 115200)", BAUD_MAX);
	menu_ui(title, opts, MENU_WIDTH);
	MENU_PROMPT("Enter your choice");

	while (true) {

		poll(poll_fds, poll_fds_count, -1);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			uart_conf->fd = -1;
			MENU_ERROR("\n%s disconnected.", uart_conf->dev);
			return -1;

		} else if (!incoming_data && poll_fds[UART_PFD].revents & POLLIN) {
			incoming_data = true;
			status_bar(uart_conf, "Data incoming from device.", false);

		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		ssize_t len = read(STDIN_FILENO, temp_buf, sizeof(temp_buf));
		CLEAR_POPUP;
		if (len < 0) {
			MENU_PERROR("\nReading from STDIN failed");
			continue;
		} else if (len > 1) {
			unsigned old_idx = last_idx;
			ret = control_seq(final_buf, temp_buf, len, &cur_pos, &last_idx);
			if (ret == 0) {
				if (old_idx > last_idx) {
					status_bar(uart_conf, NULL, false);
					incoming_data = false;
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
					status_bar(uart_conf, "Too many characters, any extra "
						   "will be ignored!", false);
					putc('\a', stderr);
					continue;
				}

				if (cur_pos < last_idx) {
					memmove(final_buf + cur_pos + 1, final_buf + cur_pos,
						last_idx - cur_pos + 1);
					final_buf[cur_pos] = temp_buf[i];
					final_buf[last_idx + 1] = '\0';
					fprintf(stderr, "\033[s\033[K%s\033[u\033[C",
						final_buf + cur_pos);
				} else {
					final_buf[last_idx] = temp_buf[i];
					fprintf(stderr, "%c", temp_buf[i]);
				}
				++last_idx;
				++cur_pos;

				break;
			case '\n':
			case '\r':
				incoming_data = false;
				status_bar(uart_conf, NULL, false);
				if (last_idx == 0) {
					putc('\a', stderr);
					continue;
				}
				final_buf[last_idx] = '\0';
				last_idx = 0;
				cur_pos = 0;

				unsigned u = strtouint(final_buf);
				if (errno != 0) {
					POPUP_INVAL("%s", final_buf);
					putc('\a', stderr);
					MENU_PROMPT("Enter your choice");
					continue;
				}

				ret = set_baud(uart_conf->fd, &u, true);
				if (ret < 0) {
					POPUP_INVAL("Error setting baud");
					putc('\a', stderr);
					MENU_PROMPT("Enter your choice");
					continue;
				}

				uart_conf->baud = u;
				status_bar(uart_conf, NULL, true);
				POPUP_INFO("Baud was set to %u.", u);
				MENU_END;
				return 0;
			// backspace
			case '\b':
			case BACKSPACE:
				if (cur_pos == 0) {
					putc('\a', stderr);
					continue;
				}

				if (last_idx == sizeof(final_buf) - 1) {
					status_bar(uart_conf, NULL, false);
					incoming_data = false;
				}

				if (cur_pos < last_idx) {
					memmove(final_buf + cur_pos - 1, final_buf + cur_pos,
						last_idx - cur_pos + 1);

					final_buf[last_idx - 1] = '\0';
					fprintf(stderr, "\033[s\033[D\033[K%s\033[%uD",
						final_buf + cur_pos - 1, last_idx - cur_pos);
				} else {
					fprintf(stderr, "\033[D\033[P");
				}
				--last_idx;
				--cur_pos;
				continue;
			case ESC:
				if (len == 1 || (len == 2 && temp_buf[1] == 'm')) {
					status_bar(uart_conf, NULL, true);
					MENU_END;
					return 0;
                                } else if (len == 2 && temp_buf[1] == 'q') {
					fprintf(stderr, "\033[31mquit\033[m\n");
					return -1;
				} else {
					putc('\a', stderr);
					continue;
				}
			}
		}
	}
}

/*
 * Set data bits in the interactive menu
 */
static int menu_data_bits(struct pollfd *poll_fds, nfds_t poll_fds_count,
			  struct uart_conf_t *uart_conf)
{
	bool incoming_data = false;
	char buf[256];
	unsigned u = 0;

	char title[] = "Data bit selection";
	char opts[] = "    \033[1m8/0\t  \033[22m8 bits (default)\n"
		      "    \033[1m7\t  \033[22m7 bits\n"
		      "    \033[1m6\t  \033[22m6 bits\n"
		      "    \033[1m5\t  \033[22m5 bits";
	menu_ui(title, opts, MENU_WIDTH);
	MENU_PROMPT("Enter your choice");

	while (true) {
		poll(poll_fds, poll_fds_count, -1);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			uart_conf->fd = -1;
			MENU_ERROR("\n%s disconnected.", uart_conf->dev);
			return -1;

		} else if (!incoming_data && poll_fds[UART_PFD].revents & POLLIN) {
			incoming_data = true;
			status_bar(uart_conf, "Data incoming from device.", false);

		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		int len = read(STDIN_FILENO, buf, sizeof(buf));
		CLEAR_POPUP;
		if (len < 0) {
			MENU_PERROR("\nReading from STDIN failed");
			continue;
		} else if (buf[0] != ESC && len > 1) {
			putc('\a', stderr);
			continue;
		}
		buf[len] = '\0';

		switch (buf[0]) {
		default:
			if BETWEEN(buf[0], '!', '~') {
				POPUP_INVAL("%s", buf);
			}
			MENU_PROMPT("Enter your choice");
			/* fallthrough */
		case ENTER:
			putc('\a', stderr);
			continue;
		case '0':
		case '8':
		case '7':
		case '6':
		case '5':
			u = strtouint(buf);
			if (errno != 0) {
				POPUP_INVAL("%s", buf);
				putc('\a', stderr);
				MENU_PROMPT("Enter your choice");
				continue;
			}

			int ret = set_data_bits(uart_conf->fd, &u, true);
			if (ret < 0) {
				POPUP_INVAL("Error setting data bits");
				putc('\a', stderr);
				MENU_PROMPT("Enter your choice");
				continue;
			}

			uart_conf->data_bits = u;
			MENU_INFO("\nData bits set to %u successfully.", u);
			status_bar(uart_conf, NULL, true);
			/* fallthrough */
		case ESC:
			if (len == 1 || (len == 2 && buf[1] == 'm')) {
				MENU_END;
				status_bar(uart_conf, NULL, true);
				return 0;
			} else if (len == 2 && buf[1] == 'q') {
				fprintf(stderr, "\033[31mquit\033[m\n");
				return -1;
			} else {
				putc('\a', stderr);
				continue;
			}
		}
	}
}

/*
 * Set parity bit in the interactive menu, poll() checks if the dev was disconnected
 */
static int menu_parity_bit(struct pollfd *poll_fds, nfds_t poll_fds_count,
			   struct uart_conf_t *uart_conf)
{
	bool incoming_data = false;
	char buf[256];
	int ret = -1;

	char title[] = "Parity bit selection";
	char opts[] = "    \033[1mN/0\t  \033[22mnone (default)\n"
		      "    \033[1mO\t  \033[22modd\n"
		      "    \033[1mE\t  \033[22meven\n"
		      "    \033[1mM\t  \033[22mmark\n"
		      "    \033[1mS\t  \033[22mspace";
	menu_ui(title, opts, MENU_WIDTH);
	MENU_PROMPT("Enter your choice");

	while (true) {
		poll(poll_fds, poll_fds_count, -1);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			uart_conf->fd = -1;
			MENU_ERROR("\n%s disconnected.", uart_conf->dev);
			return -1;

		} else if (!incoming_data && poll_fds[UART_PFD].revents & POLLIN) {
			incoming_data = true;
			status_bar(uart_conf, "Data incoming from device.", false);

		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		int len = read(STDIN_FILENO, buf, sizeof(buf));
		CLEAR_POPUP;
		if (len < 0) {
			MENU_PERROR("\nReading from STDIN failed");
			continue;
		} else if (buf[0] != ESC && len > 1) {
			putc('\a', stderr);
			continue;
		}
		buf[len] = '\0';

		switch (buf[0]) {
		default:
			if BETWEEN(buf[0], '!', '~') {
				POPUP_INVAL("%s", buf);
			}
			MENU_PROMPT("Enter your choice");
			/* fallthrough */
		case ENTER:
			putc('\a', stderr);
			continue;
		case '0':
			buf[0] = 'N';
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
			ret = set_parity_bit(uart_conf->fd, &buf[0], true);
			if (ret < 0) {
				POPUP_PERROR("%s", buf);
				putc('\a', stderr);
				MENU_PROMPT("Enter your choice");
				continue;
			}

			uart_conf->parity_bit = toupper(buf[0]);
			POPUP_INFO("Parity bit set to %c successfully.", uart_conf->parity_bit);
			MENU_END;
			status_bar(uart_conf, NULL, true);
			return 0;
		case ESC:
			if (len == 1 || (len == 2 && buf[1] == 'm')) {
				MENU_END;
				status_bar(uart_conf, NULL, true);
				return 0;
			} else if (len == 2 && buf[1] == 'q') {
				fprintf(stderr, "\033[31mquit\033[m\n");
				return -1;
			} else {
				putc('\a', stderr);
				continue;
			}
		}
	}
}

/*
 * Set stop bits in the interactive menu
 */
static int menu_stop_bits(struct pollfd *poll_fds, nfds_t poll_fds_count,
			  struct uart_conf_t *uart_conf)
{
	bool incoming_data = false;
	unsigned u = 0;
	char buf[256];

	char title[] = "Stop bit selection";
	char opts[] = "    \033[1m1/0\t  \033[22m1 bit (default)\n"
		      "    \033[1m2\t  \033[22m2 bits";
	menu_ui(title, opts, MENU_WIDTH);
	MENU_PROMPT("Enter your choice");

	while (true) {
		poll(poll_fds, poll_fds_count, 0);
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			uart_conf->fd = -1;
			MENU_ERROR("\n%s disconnected.", uart_conf->dev);
			return -1;

		} else if (!incoming_data && poll_fds[UART_PFD].revents & POLLIN) {
			incoming_data = true;
			status_bar(uart_conf, "Data incoming from device.", false);

		} else if (!(poll_fds[STDIN_PFD].revents & POLLIN)) {
			continue;
		}

		int len = read(STDIN_FILENO, buf, sizeof(buf));
		CLEAR_POPUP;
		if (len < 0) {
			MENU_PERROR("\nReading from STDIN failed");
			continue;
		} else if (buf[0] != ESC && len > 1) {
			putc('\a', stderr);
			continue;
		}
		buf[len] = '\0';

		switch (buf[0]) {
		case '0':
			buf[0] = '1';	// 0 defaults to stop bit 1
			/* fallthrough */
		case '1':
		case '2':
			u = strtouint(buf);
			if (errno != 0) {
				POPUP_PERROR("\"%s\"", buf);
				putc('\a', stderr);
				MENU_PROMPT("Enter your choice");
				continue;
			}

			int ret = set_stop_bits(uart_conf->fd, &u, true);
			if (ret < 0) {
				POPUP_PERROR("\"%s\"", buf);
				putc('\a', stderr);
				MENU_PROMPT("Enter your choice");
				continue;
			}
			uart_conf->stop_bits = u;

			POPUP_INFO("Stop bit set to %u successfully.", u);
			MENU_END;
			status_bar(uart_conf, NULL, true);
			return 0;

		default:
			if BETWEEN(buf[0], '!', '~') {
				POPUP_INVAL("%s", buf);
			}
			MENU_PROMPT("Enter your choice");
			/* fallthrough */
		case ENTER:
			putc('\a', stderr);
			continue;

		case ESC:
			if (len == 1 || (len == 2 && buf[1] == 'm')) {
				MENU_END;
				status_bar(uart_conf, NULL, true);
				return 0;
			} else if (len == 2 && buf[1] == 'q') {
				fprintf(stderr, "\033[31mquit\033[m\n");
				return -1;
			} else {
				putc('\a', stderr);
				continue;
			}
		}
	}
}

/*
 * Set up TTY to reserve rows for the status bar.
 * This function MUST be called only once.
 *
 * If it fails, UI cannot be set up properly and will have unpredictable behavior.
 */
int init_ui(struct uart_conf_t *uart_conf)
{
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
		MENU_PERROR("Could not get TTY size");
		return -1;
	}

	fprintf(stderr, "\033[2J\033[1;%dr", ws.ws_row - STATUS_BAR_SIZE);
	fprintf(stderr, "\033[32mDevice ready...\033[m\n");
	status_bar(uart_conf, NULL, true);
	return 0;
}

/*
 * Read data from device.
 *
 * Filter out escape sequences that interfere with the non-scrollable region of TTY where the
 * status bar is.
 *
 * If an escape sequence is truncated when read() returns, the incomplete sequence will be merged
 * with the rest of the sequence in the next iteration for proper parsing and processing.
 */
ssize_t process_dev_data(struct uart_conf_t *uart_conf)
{
	static char trunc_seq[32];
	static size_t trunc_len = 0;
	int extra_buf_len = 128;
	char buf[4096 + extra_buf_len];
	ssize_t rd_len = 0;
	ssize_t wr_len = 0;
	bool restart_status_bar = false;

	// if there is a truncated esc seq from previous iteration, place it at beginning of buf
	if (trunc_len > 0) {
		memcpy(buf, trunc_seq, trunc_len);
		rd_len = trunc_len;
		trunc_len = 0;
	}

	// get new data from dev
	ssize_t tmp_len = read(uart_conf->fd, buf + rd_len, sizeof(buf) - extra_buf_len - rd_len);
	if (tmp_len < 0) {
		MENU_PERROR("\nReading from device failed");
		return -1;
	}

	/*
	 * Go through the last occurrence of escape sequences to check completeness. If it's not
	 * complete, the seq will be prepended to buf during the next iteration of this function.
	 */
	char *last_esc = memrchr(buf + rd_len, ESC, tmp_len);
	rd_len += tmp_len;
	if (last_esc != NULL) {
		ssize_t last_esc_idx = last_esc - buf;
		ssize_t i = last_esc_idx;
		bool is_OSC_seq = false;

		// check completeness, break early if esc seq was complete, TODO: deal with DCS/OSC
		for (; i < rd_len; ++i) {
			// first iteration
			if (buf[i] == ESC) {
				if (i + 1 >= rd_len)
					continue;
				// skip to not be caught in BETWEEN
				else if (buf[i + 1] == '[')
					++i;
				// OSC esc seq, final byte could be \a
				else if (buf[i + 1] == ']')
					is_OSC_seq = true;
				// skip DCS seq because it uses \033\\ as final "byte"
				else if (buf[i + 1] == 'P')
					break;
			} else if (is_OSC_seq) {
				if (buf[i] == '\a')
					break;
			// final byte must be in between 0x40 and 0x7e
			} else if (BETWEEN(buf[i], '@', '~')) {
				break;
			}
		}
		// for-loop didn't break early, esc seq was incomplete, prepare trunc_seq for
		// next iteration
		if (i >= rd_len) {
			trunc_len = rd_len - last_esc_idx;
			memcpy(trunc_seq, buf + last_esc_idx, trunc_len);
			rd_len -= trunc_len;
		}
	}

	for (ssize_t i = 0; i < rd_len; ++i) {
		if (buf[i] != ESC) {
			continue;
		}

		// next char, check if last
		++i;
		if (i >= rd_len) {
			break;
		}

		// \033c (hard reset) - append scrolling reagion esc seq (\033[X;Yr)
		if (buf[i] == 'c') {
			char scroll_area[16];
			snprintf(scroll_area, sizeof(scroll_area), "\033[1;%ur",
				 ws.ws_row - STATUS_BAR_SIZE);
			size_t l = strnlen(scroll_area, sizeof(scroll_area));
			extra_buf_len -= l;
			if (extra_buf_len < 0) {
				buf[i - 1] = 0;
				buf[i] = 0;
				continue;
			}

			memmove(buf + i + l + 1, buf + i + 1, rd_len - i - 1);
			rd_len += l;
			memcpy(buf + i + 1, scroll_area, l);
			i += l;
			restart_status_bar = true;
			continue;
		}

		// from now on, only interested in escape sequences that start with \033[
		if (buf[i] != '[') {
			continue;
		}

		// next char, check if last
		++i;
		if (i >= rd_len) {
			break;
		}

		// \033[r (resets scrollable region)
		if (buf[i] == 'r') {
			char scroll_area[16];
			// prepare allowed region
			snprintf(scroll_area, sizeof(scroll_area), "1;%u",
				 ws.ws_row - STATUS_BAR_SIZE);
			size_t l = strnlen(scroll_area, sizeof(scroll_area));
			extra_buf_len -= l;
			if (extra_buf_len < 0) {
				buf[i - 2] = 0;
				buf[i - 1] = 0;
				buf[i] = 0;
				continue;
			}

			// shift to the right to make space for scroll_area
			memmove(buf + i + l, buf + i, rd_len - i);
			rd_len += l;
			// place new values
			memcpy(buf + i, scroll_area, l);
			i += l;
			continue;
		}

		// \033[J (clears beneath the cursor), re-print status bar
		if (buf[i] == 'J') {
			restart_status_bar = true;
			continue;
		}

		// ESC + [ + <char> is stored for next iter if truncated
		if (i + 1 >= rd_len) {
			break;
		}

		// \033[NJ, re-print status bar
		if (buf[i + 1] == 'J') {
			restart_status_bar = true;
			++i;
			continue;
		}

		// \033[!p (soft reset), skip resetting terminal settings
		if (buf[i] == '!' && buf[i + 1] == 'p') {
			char scroll_area[16];
			snprintf(scroll_area, sizeof(scroll_area), "\033[s\033[1;%ur\033[u",
				 ws.ws_row - STATUS_BAR_SIZE);
			size_t l = strnlen(scroll_area, sizeof(scroll_area));
			extra_buf_len -= l;
			if (extra_buf_len < 0) {
				buf[i - 2] = 0;
				buf[i - 1] = 0;
				buf[i] = 0;
				buf[i + 1] = 0;
				++i;
				continue;
			}

			memmove(buf + i + l + 2, buf + i + 2, rd_len - i - 2);
			rd_len += l;
			memcpy(buf + i + 2, scroll_area, l);
			i += l + 1;
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

		for (; i < rd_len; ++i) {

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

	wr_len = write(STDOUT_FILENO, buf, rd_len);

	if (restart_status_bar) {
		status_bar(uart_conf, NULL, true);
	}

	return wr_len;
}

/*
 * Interactive menu for setting baud, data, parity, stop bits, etc. poll() checks if there is
 * incoming data from UART while the menu/submenus are on, and detects if the dev disconnected.
 * Data incoming from the dev will be buffered while interacting with the menu, and will be shown
 * on the screen after going back to terminal.
 */
int menu(struct uart_conf_t *uart_conf, struct pollfd *poll_fds, nfds_t poll_fds_count)
{
	char buf[256];
	bool incoming_data = false;

	char title[] = "Menu";
	char opts[] = "    \033[1mB\t  \033[22mset baud rate\n"
		      "    \033[1mD\t  \033[22mset data bits\n"
		      "    \033[1mP\t  \033[22mset parity bit\n"
		      "    \033[1mS\t  \033[22mset stop bits\n"
		      "    \033[1mV\t  \033[22mpaste an ASCII file";
	menu_ui(title, opts, MENU_WIDTH);
	MENU_PROMPT("Enter your choice");

	status_bar(uart_conf, NULL, false);

	while (true) {

		poll(poll_fds, poll_fds_count, -1);

		/* Device disconnected */
		if (poll_fds[UART_PFD].revents & POLLHUP) {
			uart_conf->fd = -1;
			MENU_ERROR("\n%s disconnected.", uart_conf->dev);
			return -1;

		/* Data coming from UART */
		} else if (!incoming_data && poll_fds[UART_PFD].revents & POLLIN) {
			incoming_data = true;
			status_bar(uart_conf, "Data incoming from device.", false);

		/* Data coming from user */
		} else if (poll_fds[STDIN_PFD].revents & POLLIN) {
			int len = read(STDIN_FILENO, buf, sizeof(buf));
			if (len < 0) {
				MENU_PERROR("\nReading from STDIN failed");
				continue;
			}
			buf[len] = '\0';

			CLEAR_POPUP;
			switch (buf[0]) {
			default:
				if BETWEEN(buf[0], '!', '~') {
					POPUP_INVAL("%s", buf);
				}
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
			case ESC:
				if (len == 1 || (len == 2 && buf[1] == 'm')) {
					status_bar(uart_conf, NULL, true);
					MENU_END;
					return 0;
				} else if (len == 2 && buf[1] == 'q') {
					fprintf(stderr, "\033[31mquit\033[m\n");
					return -1;
				} else {
					putc('\a', stderr);
					continue;
				}
			}
		}
	}
}

void close_ui(void)
{
	// clean status bar
	fprintf(stderr, "\033[s\033[0J\033[r\033[u");
}
