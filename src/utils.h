#pragma once

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/*
 * Macros for keystrokes used in the menu
 */
#define ENTER		0x0d
#define ESC		0x1b
#define BACKSPACE	0x7f

/*
 * List of macros for printing info, warning, error msgs
 */
#define MENU_PROMPT(fmt) \
	fprintf(stderr, "\033[2K\033[G" fmt " \033[1m>\033[m ");

#define MENU_INFO(fmt, ...) \
	fprintf(stderr, "\033[32m" fmt "\033[m\n", ##__VA_ARGS__)

#define MENU_WARN(fmt, ...) \
	fprintf(stderr, "\033[33m" fmt "\033[m\n", ##__VA_ARGS__)

#define MENU_ERROR(fmt, ...) \
	fprintf(stderr, "\033[31m" fmt "\033[m\n", ##__VA_ARGS__)

#define MENU_PERROR(fmt, ...) \
	fprintf(stderr, "\033[31m" fmt ": %s\033[m\n", ##__VA_ARGS__, strerror(errno))

#define MENU_END	fprintf(stderr, "\033[G\033[K\033[2;3m(Menu closed)\033[m\n")

#define POPUP_INFO(fmt, ...) \
	fprintf(stderr, "\033[s\033[A\033[G\033[32m" fmt "\033[m\033[u", ##__VA_ARGS__)

#define POPUP_INVAL(fmt, ...) \
	fprintf(stderr, "\033[s\033[A\033[G\033[31m\"" fmt "\": Invalid input\033[m\033[u", \
		##__VA_ARGS__)

#define POPUP_PERROR(fmt, ...) \
	fprintf(stderr, "\033[s\033[A\033[G\033[31m" fmt ": %s\033[m\033[u", \
		##__VA_ARGS__, strerror(errno))

#define CLEAR_POPUP	fprintf(stderr, "\033[s\033[A\033[2K\033[u")


/*
 * Used to avoid magic-number inconsistencies
 */
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
