#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include "http_utils.h"
#include "network_utils.h"

void encode_city(const char *input, char *output, int max_len){
    int j = 0;
    for(int i = 0; input[i] != '\0' && j < max_len - 4; i++){
        /* Percent-encode all characters outside the RFC 3986 unreserved set. */
        if(!isalnum((unsigned char)input[i]) && input[i] != '-' && input[i] != '_'
            && input[i] != '.' && input[i] != '~'){
            sprintf(output + j, "%%%02X", (unsigned char)input[i]);
            j += 3;
        } 
        else{
            output[j++] = input[i];
        }
    }
    output[j] = '\0';
}

/* Read and process the HTTP response; returns 0 on HTTP 200 OK, 1 on error. */
int handle_response(int sockfd, int raw_mode){
    static char response[65536];
    int total = 0;
    int n = 0;

    /* Read until EOF while enforcing the shared 10-second request deadline via poll(). */
    while(total < (int)sizeof(response) - 1){
        wait_for_socket(sockfd, POLLIN);
        n = recv(sockfd, response + total, sizeof(response) - 1 - total, 0);
        if(n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if(n <= 0)
            break;
        total += n;
    }
    if(n < 0)
        die("recv failed");
    response[total] = '\0';

    if(total == 0){
        printf("tempest: empty response\n");
        return 1;
    }

    if(raw_mode){
        /* Prefix response headers with "< " and write the raw body to stdout. */
        char *end = strstr(response, "\r\n\r\n");
        int head_len = (end == NULL) ? total : (int)(end - response) + 4;
        print_with_prefix(response, head_len, "<");
        fwrite(response + head_len, 1, total - head_len, stdout);
        /* Return non-zero exit status on non-200 HTTP responses even in --raw mode. */
        if(strncmp(response, "HTTP/1.1 200", 12) != 0)
            return 1;
        return 0;
    }

    /* Verify HTTP/1.1 200 status line. */
    if(strncmp(response, "HTTP/1.1 200", 12) != 0){
        printf("tempest: invalid location\n");
        return 1;
    }

    /* Locate start of HTTP response body after CRLFCRLF delimiter. */
    char *body = strstr(response, "\r\n\r\n");
    if(body == NULL)
        return 1;
    body = body + 4;

    /* Perform case-insensitive check for Transfer-Encoding: chunked. */
    char headers[4096];
    int header_len = body - response;
    if(header_len > (int)sizeof(headers) - 1)
        header_len = sizeof(headers) - 1;
    for(int i = 0; i < header_len; i++)
        headers[i] = tolower((unsigned char)response[i]);
    headers[header_len] = '\0';

    if(strstr(headers, "transfer-encoding: chunked") == NULL){
        fwrite(body, 1, response + total - body, stdout);
        return 0;
    }

    /* Decode HTTP/1.1 chunked body: <hex size>\r\n<data>\r\n ... 0\r\n\r\n */
    while (body < response + total) {
        char *endptr = NULL;
        long size = strtol(body, &endptr, 16);
        if (endptr == body || size <= 0)
            break;
        body = strstr(endptr, "\r\n");
        if (body == NULL)
            break;
        body += 2;
        if (body >= response + total)
            break;
        if (body + size > response + total)
            size = response + total - body;
        fwrite(body, 1, (size_t)size, stdout);
        body += size + 2;
    }
    return 0;
}

/* Print text line-by-line with a prefix, stripping trailing \r. */
void print_with_prefix(const char *text, int len, const char *prefix) {
    int start = 0;
    for (int i = 0; i < len; i++) {
        if (text[i] == '\n') {
            int line_len = i - start;
            if (line_len > 0 && text[i - 1] == '\r')
                line_len--;
            if (line_len > 0)
                printf("%s %.*s\n", prefix, line_len, text + start);
            else
                printf("%s\n", prefix);
            start = i + 1;
        }
    }
    if (start < len) {
        int line_len = len - start;
        if (line_len > 0 && text[len - 1] == '\r')
            line_len--;
        if (line_len > 0)
            printf("%s %.*s\n", prefix, line_len, text + start);
    }
}
