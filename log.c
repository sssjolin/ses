#include "log.h"

#include <stdio.h>

#include "util.h"

static int log_fd;

void set_log_fd(int l_fd)
{
    log_fd = l_fd;
}

/*
 * known bug: log to file will be much more slower than stdout,
 * still need to figure out why.
 */
void log_info(const char *ip_addr, const char *time_recv, const char *req_firstLine,
              int req_stat, int resp_size)
{
    if (log_fd >= 0)
        dprintf(log_fd, "%s %s \"%s\" %d %d\n", ip_addr, time_recv,
                req_firstLine, req_stat, resp_size);
}
