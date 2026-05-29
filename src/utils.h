#pragma once

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* Macros for key strokes used in the menu */
#define ENTER	0x0d
#define ESC	0x1b
#define MENU	0x01
#define DEL	0x7f

/* List of macros for printing info, warning, error msgs or menu TUI */
#define TTY_READY fprintf(stderr, "\n\n\033[1;32;7m Terminal is ready \033[m\n\n")

#define MENU_TITLE(fmt) \
	fprintf(stderr, "\033[m\n\n\033[1m........................................\033[m\n" \
			"\033[1;7m " fmt " \033[m\n\n")

#define MENU_OPTS(fmt, ...) \
	fprintf(stderr, fmt "\n\n\n", \
            ##__VA_ARGS__)

#define MENU_PROMPT(fmt) \
	fprintf(stderr, fmt " \033[1m>\033[m ");

#define MENU_MSG(fmt, ...) \
	fprintf(stderr, "\n\033[32m" fmt "\033[m\n", \
            ##__VA_ARGS__)

#define MENU_WARN(fmt, ...) \
	fprintf(stderr, "\n\033[33m" fmt "\033[m\n", \
            ##__VA_ARGS__)

#define MENU_ERROR(fmt, ...) \
	fprintf(stderr, "\n\033[31m" fmt "\033[m\n", \
            ##__VA_ARGS__)

#define INVAL_INPUT(fmt, ...) \
	fprintf(stderr, "\033[31m" fmt ": invalid input\033[m\n", \
            ##__VA_ARGS__)


/* Used to avoid magic-number inconsistencies */
enum {
	STDIN_PFD,
	UART_PFD
};

/*
 * Prints usage:
 *
 * prog		program name
 */
void print_usage(const char *prog);

/*
 * Convert str to unsigned int:
 *
 * str		pointer to a string that needs to be converted to a uint
 *
 * returns:	value on success, 0 is success if errno == 0, otherwise it is treated as an error
 */
unsigned strtouint(const char *str);
