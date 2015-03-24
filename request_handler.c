#include "request_handler.h"

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cgi.h"
#include "http.h"
#include "index.h"
#include "util.h"

/*
 * return one line of request at a time, NULL terminated
 * return empty string on end of request
 * return NULL on recv() fail or timeout
 * cache entire request, much faster than call recv 1 byte at a time
 */
static char *
get_line(int fd)
{
    /* Apache uses 8K buffer */
    static char buf[8192], *line = NULL, *line_end;
    static int buf_len;
    if (line == NULL || *line == 0) {
        line = buf;
        if ((buf_len = recv(fd, buf, sizeof(buf) - 1, 0)) == -1)
            return NULL;
    } else {
        line = line_end + 1;
        if (line >= buf + buf_len)  /* no more line */
            return --line;
    }

    for (line_end = line; line_end < buf + buf_len; line_end++)
        if (*line_end == '\n')
            break;
    *line_end = 0;

    return line;
}


void
handle_request(int fd, const char *ip_addr, const char *cgi_path)
{
    int flags = 0;

    /* set recv() timeout limit to 4 minutes */
    struct timeval tv = { .tv_sec = 240, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));

    char *line =  get_line(fd);
    if (line == NULL) {
        error_response(fd, errno_to_http_status(), flags, ip_addr, "");
        return;
    }

    int line_len = strlen(line) - 1;
    line[line_len] = 0;

    char *uri = line + 5;
    if (memcmp(line, "HEAD /", 6) == 0) {
        uri++;
        flags |= NO_ENTITY_BODY;
    } else if (memcmp(line, "GET /", 5) != 0) {
        if (memcmp(line, "POST /", 6) == 0)
            error_response(fd, 501, flags, ip_addr, line);
        else
            error_response(fd, 400, flags, ip_addr, line);
        return;
    }
    char *uri_end = strchr(uri, ' ');
    if (uri_end == NULL) {
        if (flags & NO_ENTITY_BODY) {
            error_response(fd, 400, flags, ip_addr, line);
            return;
        }
        uri_end = line + line_len;
        flags |= NO_HEADER; /* Simple Request */
    }

    if ((flags & NO_HEADER) == 0) {
        int major = -1, minor = -1;
        if (sscanf(uri_end + 1, "HTTP/%d.%d", &major, &minor) != 2) {
            error_response(fd, 400, flags, ip_addr, line);
            return;
        }
        if (major != 1 || minor != 0) {
            error_response(fd, 505, flags, ip_addr, line);
            return;
        }
    }

    char first_line[line_len + 1];
    memcpy(first_line, line, line_len + 1);

    int uri_len = uri_end - uri;
    char print_uri[uri_len + 3], path_uri[uri_len + 3];
    memcpy(print_uri, uri - 1, uri_len + 1);
    print_uri[uri_len + 1] = 0;

    path_uri[0] = '.';
    if (uri[0] == '~') {
        char *uname_end = strchr(uri, '/');
        if (uname_end == NULL || uname_end > uri_end)
            uname_end = uri_end;
        memcpy(path_uri + 1, uname_end, uri_end - uname_end);
        path_uri[uri_end - uname_end + 1] = 0;
        *uname_end = 0;
        struct passwd *pw;
        if ((pw = getpwnam(uri + 1)) == NULL) {
            error_response(fd, 404, flags, ip_addr, first_line);
            return;
        }
        if (chdir(pw->pw_dir) || chdir("sws")) {
            error_response(fd, errno_to_http_status(), flags, ip_addr, first_line);
            return;
        }
    } else {
        memcpy(path_uri + 1, print_uri, uri_len + 2);

        if (cgi_path != NULL && memcmp(path_uri + 2, "cgi-bin/", 8) == 0) {
            if (chdir(cgi_path) == -1) {
                error_response(fd, 500, flags, ip_addr, first_line);
                return;
            }
            int status = parse_CGI(path_uri + 10, fd, flags);
            if (status != 200)
                error_response(fd, status, flags, ip_addr, first_line);
            else
                response_header_and_log(fd, 200, flags | NO_HEADER, 0, 0, ip_addr, first_line);
            return;
        }
    }

    time_t if_modified_since = 0;
    do {
        if ((line = get_line(fd)) == NULL) {
            error_response(fd, errno_to_http_status(), flags, ip_addr, first_line);
            return;
        }

        if (memcmp(line, "If-Modified-Since: ", 19) == 0)
            if_modified_since = str_to_time(line + 19);
    } while (*line != '\r');

    decode_uri(path_uri);
    if (sanitize_path(path_uri)) {
        error_response(fd, errno_to_http_status(), flags, ip_addr, first_line);
        return;
    }

    struct stat sb;
    int uri_fd = open(path_uri, O_RDONLY);
    if (uri_fd == -1 || fstat(uri_fd, &sb) == -1) {
        error_response(fd, errno_to_http_status(), flags, ip_addr, first_line);
        return;
    }

    /*
     * According to RFC1945, if an If-Modified-Since header field is included
     * with a HEAD request, it should be ignored.
     */
    if ((flags & NO_ENTITY_BODY) == 0 && if_modified_since > sb.st_mtime) {
        response_header_and_log(fd, 304, flags | NO_ENTITY_HEADER, 0, 0, ip_addr, first_line);
        return;
    }

    /* Directory Indexing */
    if (S_ISDIR(sb.st_mode)) {
        close(uri_fd);
        if (print_uri[uri_len] != '/') {
            print_uri[uri_len + 1] = '/';
            print_uri[uri_len + 2] = 0;
        }
        char index_path[PATH_MAX];
        strncpy(index_path, path_uri, sizeof(index_path));
        strncat(index_path, "/index.html", sizeof(index_path));
        if ((uri_fd = open(index_path, O_RDONLY)) == -1 || fstat(uri_fd, &sb) == -1) {
            if (errno != ENOENT) {
                error_response(fd, errno_to_http_status(), flags, ip_addr, first_line);
                return;
            }
        }
        if (S_ISDIR(sb.st_mode)) {
            char *html = NULL;
            int status = index_html_function(path_uri, print_uri, &html);
            if (status == 200) {
                int len = strlen(html);
                response_header_and_log(fd, status, flags, sb.st_mtime, len, ip_addr, first_line);
                if ((flags & NO_ENTITY_BODY) == 0)
                    if (send(fd, html, len, 0) == -1)
                        warn("send");
            } else
                error_response(fd, status, flags, ip_addr, first_line);
            if (html)
                free(html);
            return;
        }
    }

    if ((flags & NO_ENTITY_BODY) == 0 && sb.st_size) {
        char *buf = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, uri_fd, 0);
        if (buf == MAP_FAILED)
            error_response(fd, errno_to_http_status(), flags, ip_addr, first_line);
        else {
            response_header_and_log(fd, 200, flags, sb.st_mtime, sb.st_size, ip_addr, first_line);

            if (send(fd, buf, sb.st_size, 0) == -1)
                warn("send");

            if (munmap(buf, sb.st_size) == -1)
                warn("unable to unmap %s", path_uri);
        }
    } else
        response_header_and_log(fd, 200, flags, sb.st_mtime, sb.st_size, ip_addr, first_line);

    close(uri_fd);
}
