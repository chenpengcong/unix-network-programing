#ifndef HTTP_PARSE_H
#define HTTP_PARSE_H

struct RequestLine{
    char *method;
    char *target;
    char *version;
};

struct RequestLine *parse_http_msg(char *msg);
#endif