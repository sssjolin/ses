#ifndef SWS_HTTP_H
#define SWS_HTTP_H

#include <time.h>


enum ResponseFlags {
    NO_HEADER = 0x1,
    NO_ENTITY_BODY = 0x2,
    NO_ENTITY_HEADER = 0x4
};

int
errno_to_http_status();

void
error_response(int fd, int status_code, int flags, const char *ip_addr, const char *req_firstLine);

void
response_header_and_log(int fd, int status_code, int rsp_flags, time_t last_modified, int content_length, const char *ip_addr, const char *req_firstLine);

const char *
get_status_str(int status_code);


#endif
