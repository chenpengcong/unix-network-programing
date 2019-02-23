#include "http_parse.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

//只解析出request line
struct RequestLine *parse_http_msg(char *msg)
{
    int i = 0;
    struct RequestLine *p_req_line = NULL;
    char *p_tmp = NULL;
    char *p_crlf;
    char space_num = 0;
    char *sp_addr[2];
    int len = 0;
    p_req_line = malloc(sizeof(struct RequestLine));
    if (p_req_line == NULL) {
        fprintf(stderr, "malloc p_req_line failed\n");
        return NULL;
    }

    p_crlf = strstr(msg, "\r\n");
    if (p_crlf == NULL) {
        fprintf(stderr, "http msg not contains CRLF");
        return NULL;
    }

    p_tmp = msg;
    while (p_tmp < p_crlf) {
        if (*p_tmp == ' ') {
            space_num++;
            if (space_num <= 2) {
                sp_addr[space_num - 1] = p_tmp;//记录空格地址
            }
        }
        p_tmp++;
    }
    //请求行必须包含两个空格
    if (space_num != 2) {
        fprintf(stderr, "parse http request line failed");
        return NULL;
    }
    
    p_tmp = msg;
    len = sp_addr[0] - p_tmp;
    p_req_line->method = malloc(len + 1);
    memset(p_req_line->method, 0, len + 1);
    memcpy(p_req_line->method, p_tmp, len);


    p_tmp = sp_addr[0] + 1;
    len = sp_addr[1] - p_tmp;
    p_req_line->target = malloc(len + 1);
    memset(p_req_line->target, 0, len + 1);
    memcpy(p_req_line->target, p_tmp, len);

    p_tmp = sp_addr[1] + 1;
    len = p_crlf - p_tmp;
    p_req_line->version = malloc(len + 1);
    memset(p_req_line->version, 0, len + 1);
    memcpy(p_req_line->version, p_tmp, len);
    
    printf("method:%s\n", p_req_line->method);
    printf("target:%s\n", p_req_line->target);
    printf("version:%s\n", p_req_line->version);
    return p_req_line;
}

