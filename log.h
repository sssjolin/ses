#ifndef SWS_LOG_H
#define SWS_LOG_H

void set_log_fd(int log_fd);

void log_info(const char *ip_addr, const char *time_recv, const char *req_firstLine,
              int req_stat, int resp_size);

#endif