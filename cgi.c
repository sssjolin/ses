#include "cgi.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "http.h"
#include "util.h"

/****************************************************************************
 *  Some idea from https://github.com/lighttpd/scgi-cgi/ (MIT license)      *
 ****************************************************************************/
typedef struct string_buffer string_buffer;
struct string_buffer {
    /* always 0-terminated string (unless data == NULL), but don't count terminating 0 in `used' */
    unsigned char *data;
    unsigned int used, size;
};

typedef struct cgi_req_parser cgi_req_parser;
struct cgi_req_parser {
    unsigned int header_length, is_scgi;
    unsigned long long content_length;

    unsigned char **environ; /* list terminated by NULL entry */
    unsigned int environ_used, environ_size;

    string_buffer key_value;
    unsigned int key_length;
};

typedef struct enviro enviro;
struct enviro {
    char **environ;
    unsigned int environ_used, environ_size;
};

static void cgi_parser_init(enviro *parser)
{
    parser->environ = (char **)malloc(sizeof(char *)*DEFAULT_ENV_SIZE);
    if (parser->environ == NULL) {
        perror("init parser malloc error: ");
        exit(EXIT_FAILURE);
    }
    parser->environ_used = 0;
    parser->environ_size = DEFAULT_ENV_SIZE;
}

static void cgi_parser_resize(enviro *parser)
{
    parser->environ = (char **)realloc(parser->environ,
                                       sizeof(char *) * (parser->environ_size + DEFAULT_ENV_SIZE));
    if (parser->environ == NULL) {
        perror("init parser realloc error: ");
        exit(EXIT_FAILURE);
    }
    parser->environ_size += DEFAULT_ENV_SIZE;
}

static int cgi_parser_environ_append(enviro *parser, char *kvstr, int keylength)
{
    unsigned int i;
    /* if reach the limit */
    if ((parser->environ_size + 1) == parser->environ_used)
        cgi_parser_resize(parser);
    for (i = 0; i < parser->environ_used; ++i) {
        const char *e = (const char *) parser->environ[i];
        if (0 == strncmp(e, (const char *) kvstr, keylength + 1)) {
            fprintf(stderr, "duplicate environment\n");
            return 0;
        }
    }

    parser->environ[parser->environ_used++] = kvstr;
    parser->environ[parser->environ_used] = NULL;

    return 1;
}
/* known bug if path have '?' this won't work */
static int split_input(char *in, char *query)
{
    int found = 0;
    int j = 0;
    for (int i = 0; in[i]; i++) {
        if (found == 0) {
            if (in[i] == '?') {
                in[i] = '\0';
                found = 1;
            }
        } else {
            query[j] = in[i];
            if (++j == URL_MAX - 1)
                break;
        }
    }
    query[j] = '\0';
    return j;
}

/*
 * this function returns http response code. if exec error, child process
 * return error code, else parent process return 200 or error code.
 */
int parse_CGI(char *path, int fd, int req_flags)
{

    char *args[] = { NULL, NULL };
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    fd_set read_set;
    char query_string[URL_MAX] = "";
    char QUERY[URL_MAX + 13] = "QUERY_STRING=";
    int have_query = split_input(path, query_string);

    decode_uri(path);
    if (sanitize_path(path))
        return errno_to_http_status();
    if (path[1] == 0)   /* path_uri is outside of current directory */
        return 403;

    struct stat s;
    if ( stat(path, &s) == 0 ) {
        // if its not a file that can exec
        if (!((s.st_mode & S_IFREG) && (s.st_mode & S_IXUSR)))
            return 403;
    } else
        return errno_to_http_status();
    long n;
    int it;
    int n_read = 0;
    time_t date = time(NULL);
    enviro *parser = (enviro *)malloc(sizeof(enviro));
    if (parser == NULL) {
        perror("malloc error");
        return 500;
    }
    /* init and add enviro */
    cgi_parser_init(parser);
    /* todo add get if get */
    if (req_flags & NO_ENTITY_BODY)
        cgi_parser_environ_append(parser, "REQUEST_METHOD=HEAD", 14);
    else
        cgi_parser_environ_append(parser, "REQUEST_METHOD=GET", 14);
    cgi_parser_environ_append(parser, "SERVER_PROTOCOL=HTTP/1.0", 14);
    cgi_parser_environ_append(parser, "GATEWAY_INTERFACE=CGI/1.1", 17);

    if (have_query)
        strncat(QUERY, query_string, URL_MAX);
    cgi_parser_environ_append(parser, QUERY, 12);


    int pipefd[2];
    if (-1 == pipe(pipefd)) {
        perror("pipe error");
        return 500;
    }

    if (fork() == 0) {
        close(pipefd[0]);    // close reading end in the child

        dup2(pipefd[1], STDOUT_FILENO);  // send stdout to the pipe

        close(pipefd[1]);    // this descriptor is no longer needed

        if (execve(path, args, parser->environ) != 0) {
            perror("exec failed");
            switch (errno) {
            case EACCES:
                return 403;
            case ENOENT:
                return 404;
            default:
                return 500;
                break;
            }
        }
        exit(EXIT_SUCCESS);
    } else {
        /* parent */
        char buffer[CGI_BUFFERSIZE];
        FD_ZERO(&read_set);
        /* Parent process closes up output side of pipe */
        FD_SET(pipefd[0], &read_set);
        close(pipefd[1]);  /* close the write end of the pipe in the parent */
        /* add interval */
        if (select(pipefd[0] + 1, &read_set, NULL, NULL, &tv) == -1) {
            perror("select fail");
            wait(NULL);
            if (errno == EINVAL)
                return 522;
            else
                return 500;
        }
        if (!(req_flags & NO_ENTITY_BODY)) {
            while ((n = read(pipefd[0], buffer, sizeof(buffer))) != 0) {
                if (n_read == 0 && n == 0)
                    exit(EXIT_FAILURE);
                else if (n_read == 0 && !(req_flags & NO_HEADER)) {
                    dprintf(fd, "HTTP/1.0 200 OK\r\n");
                    dprintf(fd, "Date: %s\r\n", time_to_str(date));
                    dprintf(fd, "Server: sws\r\n");
                }

                //if the first line is empty then quit
                if (write(fd, buffer, n) == -1)
                    perror("write socket");
                n_read++;
            }

            wait(NULL);
        } else {
            if (!(req_flags & NO_HEADER)) {
                dprintf(fd, "HTTP/1.0 200 OK\r\n");
                dprintf(fd, "Date: %s\r\n", time_to_str(date));
                dprintf(fd, "Server: sws\r\n");
            }
            n = read(pipefd[0], buffer, sizeof(buffer));
            //know bug: won't work with '\r' or whatever stupid thing windows provide
            //know bug no2: fail if= head have more than 65535 char

            for (it = 0; it < n && buffer[it] != '\n'; it++) {}
            if (write(fd, buffer, it + 2) == -1)
                perror("write socket");

        }
    }
    return 200;
}

