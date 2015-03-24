#ifndef SWS_UTIL_H
#define SWS_UTIL_H

#include <time.h>

#define TIME_STR_MAX_LEN 200

#ifdef __sun__
int dprintf(int fd, const char *format, ...);
#endif

time_t str_to_time(const char *s);
char *time_to_str(time_t t);
int decode_uri(char *input);
int sanitize_path(char *path);
#endif
