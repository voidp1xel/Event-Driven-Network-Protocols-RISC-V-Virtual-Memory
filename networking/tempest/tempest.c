#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "network_utils.h"
#include "http_utils.h"

int main(int argc, char *argv[]){
    if (argc < 2) {
        printf("tempest: missing city argument\n");
        return 1;
    }

    int raw_mode = 0;
    if(argc == 3){
        if(strcmp(argv[2], "--raw") == 0)
            raw_mode = 1;
        else{
            printf("tempest: too many arguments\n");
            return 1;
        }
    } 
    else if(argc > 3){
        printf("tempest: too many arguments\n");
        return 1;
    }

    char encoded_city[1024];
    encode_city(argv[1], encoded_city, 1024);

    /* Enforce a shared 10-second wall-clock deadline across connect, send, and recv. */
    start_timer(10);

    int sockfd = connect_to_server("wttr.is", "80");

    char request[4096];
    snprintf(request, sizeof(request), "GET /%s?0T HTTP/1.1\r\nHost: wttr.is\r\nConnection: close\r\n\r\n", encoded_city);

    send_all(sockfd, request, strlen(request));
    /* In --raw mode, print the outgoing HTTP request prefixed with "> ". */
    if(raw_mode)
        print_with_prefix(request, strlen(request), ">");
    int status = handle_response(sockfd, raw_mode);

    close(sockfd);
    return status;
}
