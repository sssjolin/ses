#include "net.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "request_handler.h"

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif


void
start_server(const char *bind_address, const char *listen_port, const char *cgi_path)
{
    struct addrinfo *ai_list, *rp, hints = {
        .ai_family      = AF_UNSPEC,
        .ai_socktype    = SOCK_STREAM,
        .ai_flags       = AI_PASSIVE | AI_ADDRCONFIG
    };
    int sfd, err_code;

    if ((err_code = getaddrinfo(bind_address, listen_port, &hints, &ai_list)) != 0) {
        switch (err_code) {
        case EAI_NONAME:
            errx(EXIT_FAILURE, "Invalid address.");
        case EAI_SERVICE:
            errx(EXIT_FAILURE, "Invalid port.");
        default:
            errx(EXIT_FAILURE, "getaddrinfo: %s", gai_strerror(err_code));
        }
    }
    for (rp = ai_list; rp; rp = rp->ai_next) {
        if ((sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1)
            continue;

        setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));

        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(sfd);
    }

    if (rp == NULL)
        errx(EXIT_FAILURE, "bind failed");

    freeaddrinfo(ai_list);

    if (listen(sfd, SOMAXCONN) == -1)
        err(EXIT_FAILURE, "listen");

    /* no zombies */
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
        warn("signal");

    while (true) {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(struct sockaddr_storage);

        /* blocking is fine here, nothing else to do */
        int fd = accept(sfd, (struct sockaddr *)&client_addr, &addr_len);
        if (fd == -1) {
            warn("accept");
            continue;
        }

        if (fork() == 0) {
            char ip_addr[NI_MAXHOST];
            if ((err_code = getnameinfo((struct sockaddr *)&client_addr, addr_len, ip_addr, sizeof(ip_addr), NULL, 0, NI_NUMERICHOST)) != 0)
                warnx("getnameinfo: %s", gai_strerror(err_code));
            handle_request(fd, ip_addr, cgi_path);
            close(fd);
            break;
        }
        if (errno)
            warn("fork");
        close(fd);
    }

    close(sfd);
}