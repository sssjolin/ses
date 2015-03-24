#ifndef SWS_CGI_H
#define SWS_CGI_H

#define DEFAULT_ENV_SIZE 20
#define URL_MAX 2083
#define CGI_BUFFERSIZE 65535

int parse_CGI(char *path, int fd, int req_flags);

#endif
