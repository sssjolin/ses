/*
 * sws
 *
 * Advanced Programming in the UNIX Environment - Final Project
 * http://www.cs.stevens.edu/~jschauma/631/f14-final-project.html
 *
 */

/* for daemon(3) */
#if defined(__NetBSD__) && !defined(_NETBSD_SOURCE)
#define _NETBSD_SOURCE
#elif defined(__sun__) && !defined(__EXTENSIONS__)
#define __EXTENSIONS__
#endif

#include <sys/stat.h>
#include <sys/types.h>

#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "log.h"
#include "net.h"

static void
usage(void)
{
    (void)fprintf(stderr, "Usage: sws [-dh] [-c dir] [-i address] [-l file] [-p port] dir\n");
    exit(EXIT_FAILURE);
}


int
main(int argc, char *argv[])
{
    int log_fd = -1;
    const char *logfile_path = NULL;
    const char *cgi_path = NULL;
    const char *bind_address = NULL;
    const char *listen_port = "8080";

    char opt;
    while ((opt = getopt(argc, argv, "dhc:i:l:p:")) != -1) {
        switch (opt) {
        case 'd':
            log_fd = STDOUT_FILENO;
            break;
        case 'c':
            if ((cgi_path = realpath(optarg, NULL)) == NULL)
                err(EXIT_FAILURE, "illegal CGI directory");
            break;
        case 'i':
            bind_address = optarg;
            break;
        case 'l':
            logfile_path = optarg;
            break;
        case 'p':
            listen_port = optarg;
            break;
        case 'h':
        default:
            usage();
        }
    }

    if (argc != optind + 1)
        usage();

    if (log_fd < 0 && logfile_path != NULL)
        if ((log_fd = open(logfile_path, O_WRONLY | O_CREAT | O_APPEND, 0644)) == -1)
            err(EXIT_FAILURE, "illegal log file");

    set_log_fd(log_fd);

    if (chdir(argv[optind]) == -1)
        err(EXIT_FAILURE, "illegal document root");

    if (log_fd != STDOUT_FILENO)
        if (daemon(1, 0) == -1)
            err(EXIT_FAILURE, "daemon");

    start_server(bind_address, listen_port, cgi_path);

    if (log_fd >= 0)
        close(log_fd);

    if (cgi_path != NULL)
        free((void *)cgi_path);

    return EXIT_SUCCESS;
}
