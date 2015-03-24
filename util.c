#include "util.h"

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

/*
 * OmniOS does not conform to POSIX.1-2008 specification.
 * It does not have dprintf(3) function, we implement it ourselves.
 */
#ifdef __sun__
#include <stdarg.h>
int dprintf(int fd, const char *format, ...)
{
    if ((fd = dup(fd)) == -1)
        return -1;
    FILE *f = fdopen(fd, "a");
    if (f == NULL)
        return -1;
    va_list args;
    va_start(args, format);
    int r = vfprintf(f, format, args);
    va_end(args);
    fclose(f);
    return r;
}
#endif
#define URL_MAX 2083

time_t str_to_time(const char *s)
{
    struct tm tm;
    memset(&tm, 0, sizeof(struct tm));

    /* rfc1123-date RFC 822 time: wkday, DD MTH YEAR(4DIGIT) HH:MM:SS GMT */
    /* rfc850-date RFC 850 time: weekday, DD-MTH-YEAR(2DIGIT) HH:MM:SS GMT */
    /* asctime-date time: wkday MTH DD(2DIGIT|(SP 1DIGIT)) HH:MM:SS YEAR(4DIGIT) */
    if ((strptime(s, "%a, %d %b %Y %H:%M:%S GMT", &tm)) ||
            (strptime(s, "%a, %d-%b-%y %H:%M:%S GMT", &tm)) ||
            (strptime(s, "%a %b %d %H:%M:%S %Y", &tm)))
        return mktime(&tm);
    return 0;
}

/* return rfc1123-date RFC 822 time: wkday, DD MTH YEAR(4DIGIT) HH:MM:SS GMT */
char *time_to_str(time_t t)
{
    struct tm *tm;
    static char s[TIME_STR_MAX_LEN];
    tm = gmtime(&t);
    if (strftime(s, sizeof(s), "%a, %d %b %Y %H:%M:%S GMT", tm))
        return s;
    warn("strftime");
    return "";
}

bool is_hex_char(char c)
{
    if (c >= '0' && c <= '9')
        return true;
    c &= ~0x20;
    return c >= 'A' && c <= 'F';
}

int decode_uri(char *input)
{
    int n = (int)strlen(input), i, j;
    for (i = 0, j = 0; j < n; i++, j++) {
        if (input[j] == '%' && j < n - 2 &&
                is_hex_char(input[j + 1]) && is_hex_char(input[j + 2])) {
            input[i] = input[++j] & 0xF;
            if (input[j] & 0x40)
                input[i] += 9;
            input[i] = (input[i] << 4) | (input[++j] & 0xF);
            if (input[j] & 0x40)
                input[i] += 9;
        } else
            input[i] = input[j];
    }
    input[i] = '\0';
    /* return how many times % been replaced */
    return (j - i) / 2;
}

/*
* If path is outside current directory, set it to current directory.
* Return 0 on success, -1 on error.
*/
int sanitize_path(char *path)
{
    char *real_path = realpath(path, NULL);
    if (real_path == NULL)
        return -1;

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return -1;

    if (memcmp(cwd, real_path, strlen(cwd)) != 0) {
        path[0] = '.';
        path[1] = 0;
    }
    free(real_path);
    return 0;
}
