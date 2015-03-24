#include "http.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "util.h"


void
response_header_and_log(int fd, int status_code, int rsp_flags, time_t last_modified, int content_length, const char *ip_addr, const char *req_firstLine)
{
    char *date_str = "", *last_modified_str;
    time_t date = time(NULL);
    if (date != -1)
        date_str = time_to_str(date);

    log_info(ip_addr, date_str, req_firstLine, status_code, content_length);

    if ((rsp_flags & NO_HEADER) == 0) {
        const char header1[] =
            "HTTP/1.0 %s\r\n"
            "Date: %s\r\n"
            "Server: sws\r\n";

        dprintf(fd, header1, get_status_str(status_code), date_str);

        if ((rsp_flags & NO_ENTITY_HEADER) == 0) {
            const char header2[] =
                "Last-Modified: %s\r\n"
                "Content-Type: text/html\r\n"
                "Content-Length: %d\r\n\r\n";
            if (last_modified == 0)
                last_modified_str = date_str;
            else
                last_modified_str = time_to_str(last_modified);

            dprintf(fd, header2, last_modified_str, content_length);
        }
    }
}

int
errno_to_http_status()
{
    switch (errno) {
    case EACCES:
        return 403;
    case ENOENT:
        return 404;
    case EAGAIN:
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
        return 408;
    case ENAMETOOLONG:
        return 414;
    case EMFILE:
    case ENFILE:
    case ENOMEM:
    case ENOSPC:
        return 503;
    case EINVAL:
        return 522;
    default:
        return 500;
    }
}

const char *
get_status_str(int status_code)
{
    switch (status_code) {
    case 200:
        return "200 OK";
    case 304:
        return "304 Not Modified";
    case 400:
        return "400 Bad Request";
    case 403:
        return "403 Forbidden";
    case 404:
        return "404 Not Found";
    case 408:
        return "408 Request Timeout";
    case 414:
        return "414 Request-URI Too Long";
    case 500:
        return "500 Internal Server Error";
    case 501:
        return "501 Not Implemented";
    case 503:
        return "503 Service Unavailable";
    case 505:
        return "505 Version Not Supported";
    case 522:
        return "522 Connection Timed Out";
    }
    return "";
}

void
error_response(int fd, int status_code, int flags, const char *ip_addr, const char *req_firstLine)
{
    const char response_body[] = "<html><head><title>%s</title><center><h1>%s</h1></center><hr><center>sws</center>";
    const char *http_status = get_status_str(status_code);

    response_header_and_log(fd, status_code, flags, 0, sizeof(response_body) - 4 - 1 + strlen(http_status) * 2, ip_addr, req_firstLine);

    if ((flags & NO_ENTITY_BODY) == 0)
        dprintf(fd, response_body, http_status, http_status);
}
