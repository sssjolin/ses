#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "index.h"

#define MAX_HTML_LENGTH 2000  /* html length */
#define MAX_FILE_COUNT 500    /* the file name length */
#define LENGTH 1000

/* strcpy with auto realloc */
void my_strcpy(char **dest, char *sor)
{
    if (*dest == NULL || sor == NULL)
        return;
    int rate = 1;
    while (strlen(sor) >= (MAX_FILE_COUNT * sizeof(char) * rate)) {
        rate++;
        *dest = (char *)realloc(*dest, MAX_FILE_COUNT * rate * sizeof(char));
        if (*dest == NULL) {
            warn("dest realloc");
            return;
        }
    }
    memcpy(*dest, sor, strlen(sor) + 1);
}

/* strcat with auto realloc */
void my_strcat(char **org, char *sor)
{
    if (*org == NULL || sor == NULL)
        return;
    int rate = strlen(*org) / MAX_HTML_LENGTH + 1;
    while (strlen(sor) >= (MAX_HTML_LENGTH * sizeof(char) * rate - strlen(*org))) {
        rate++;
        *org = (char *)realloc(*org, MAX_HTML_LENGTH * rate * sizeof(char));
        if (*org == NULL) {
            warn("org realloc");
            return;
        }
    }
    memcpy(*org + strlen(*org), sor, strlen(sor) + 1);
}

/* use strcasecmp to compare */
int compar_words(const void *p, const void *q)
{
    return strcasecmp(*(char **)p, *(char **)q);
}


/* path_uri is dir path and result is the string of html */
/* return http status code */
int index_html_function(char *path_uri, char *print_uri, char **result)
{

    DIR *fd;
    int count, i, rate;
    struct dirent *dirp;
    char **fileList;
    count = 0;
    rate = 1;
    *result = (char *)malloc(MAX_HTML_LENGTH * sizeof(char));
    if (*result == NULL) {
        warn("malloc");
        return 500;
    }
    **result = 0;
    fileList = (char **)malloc(LENGTH * sizeof(char *));
    if (fileList == NULL) {
        warn("malloc");
        return 500;
    }
    for (i = 0; i < LENGTH; i++) {
        fileList[i] = (char *)malloc(MAX_FILE_COUNT * sizeof(char));
        if (fileList[i] == NULL)  {
            warn("malloc");
            return 500;
        }
        memset(fileList[i], 0, strlen(fileList[i]));
    }

    if ((fd = opendir(path_uri)) == NULL) {
        warn("opendir");
        closedir(fd);
        for (i = 0; i < LENGTH; i++) {
            if (fileList[i] != NULL)
                free(fileList[i]);
        }
        if (fileList != NULL) {
            free(fileList);
            fileList = NULL;
        }
        return 403;
    }
    my_strcat(result, "<!DOCTYPE html><html><head><title>Index of ");
    my_strcat(result, print_uri);
    my_strcat(result, "</title></head><h1>Index of ");
    my_strcat(result, print_uri);
    my_strcat(result, "</h1><body><table><table><tr><th>Name</th></tr><tr><th><hr></th></tr>");

    if (print_uri[1]) {
        my_strcat(result, "<tr><td><a href=\"");
        my_strcat(result, print_uri);
        my_strcat(result, "../\">Parent Directory</td></tr>");
    }

    while ((dirp = readdir(fd)) != NULL) {
        if (dirp->d_name[0] == '.')
            continue;
        if (count >= (MAX_FILE_COUNT * rate - 1)) {
            rate++;
            fileList = (char **)realloc(fileList, LENGTH * rate * sizeof(char *));
            if (fileList == NULL)  {
                warn("realloc");
                return 500;
            }
            for (i = LENGTH * (rate - 1); i < LENGTH * rate; i++) {
                fileList[i] = (char *)malloc(MAX_FILE_COUNT * sizeof(char));
                if (fileList[i] == NULL)  {
                    warn("malloc");
                    return 500;
                }
                memset(fileList[i], 0, strlen(fileList[i]));
            }
        }
        my_strcpy(&fileList[count], dirp->d_name);
        count++;
    }

    qsort(fileList, count, sizeof(char *), compar_words);

    for (i = 0; i < count; i++) {
        my_strcat(result, "<tr><td><a href=\"");
        my_strcat(result, print_uri);
        my_strcat(result, fileList[i]);
        my_strcat(result, "\">");
        my_strcat(result, fileList[i]);
        my_strcat(result, "</td></tr>");
    }

    my_strcat(result, "<tr><th><hr></th></tr></table>sws</body></html>");

    for (i = 0; i < LENGTH * rate; i++) {
        if (fileList[i] != NULL)
            free(fileList[i]);
    }
    if (fileList != NULL) {
        free(fileList);
        fileList = NULL;
    }
    return 200;
}
