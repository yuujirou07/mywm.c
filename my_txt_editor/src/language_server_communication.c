#include<stdio.h>
#include<string.h>
#include <unistd.h>
#include"language_server_communication.h"



void lsp_send(int fd, const char *json)
{
    char header[128];

    int json_len = strlen(json);

    int header_len = snprintf(
        header,
        sizeof(header),
        "Content-Length: %d\r\n\r\n",
        json_len
    );

    write(fd, header, header_len);
    write(fd, json, json_len);
}