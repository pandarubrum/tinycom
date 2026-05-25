#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>


/* Print usage when running the program with the -h flag */
void print_usage(const char *prog)
{
	printf("Usage: %s [-b baud] [-d data_bits] [-p parity] [-s stop_bits] [device]\n\n"
	       "Example: %s -b 115200 -d 8 -p N -s 1 /dev/ttyUSB0\n\nAll flags are completely "
	       "optional. Running tinycom without any argument defaults to: 115200 8N1 "
	       "/dev/tty(USB|ACM)*\n", prog, prog);
}

/* Convert a str into an unsigned int */
unsigned strtouint(const char *str)
{
	errno = 0;

	if (str == NULL || str[0] == '\0') {
		errno = EINVAL;
		return 0;
	}

	char *end = NULL;

	uintmax_t ret = strtoumax(str, &end, 10);

	if (errno == ERANGE || end[0] != '\0' || ret > UINT_MAX) {
		errno = EINVAL;
		return 0;
	}

	return (unsigned)ret;
}
