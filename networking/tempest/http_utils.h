#ifndef HTTP_UTILS_H
#define HTTP_UTILS_H

void encode_city(const char *input, char *output, int max_len);
int handle_response(int sockfd, int raw_mode);
void print_with_prefix(const char *text, int len, const char *prefix);

#endif