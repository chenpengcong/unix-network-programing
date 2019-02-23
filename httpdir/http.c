#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include "http_parse.h"

#define MAX_EVENTS 10
#define BUFFER_LEN 2048
#define MAX_CONN_NUM 1024
#define CRLF "\r\n"
#define NOT_FOUND 404
#define INTERNAL_ERR 500
#define SUCCESS 200


struct RecvMsg {
    char data[BUFFER_LEN];
    int len;
};


struct RecvMsg *recvMap[MAX_CONN_NUM];

static int epollfd;
static int listenport;
static char* bindip;
static char* rootdir;


void process(int fd);
void process_core(struct RequestLine *req, int fd);
int send_file(char *path, int conn_sock);
int send_dirlist(char *path, int conn_sock);
void send_404(int conn_sock);
void send_500(int conn_sock);
void send_statusline(int conn_sock, int statuscode, char *reason);
void send_header(int conn_sock, char *name, char *value);
void Write(int fd, char *buf, int len);




int main(int argc, char **argv)
{
    struct epoll_event ev, events[MAX_EVENTS];
    int listen_sock, conn_sock, nfds;
    struct sockaddr_in srv_addr;
    char buf[BUFFER_LEN];
    int len;
    int n = 0;
    int writedLen = 0;

    if (argc != 4) {
        fprintf(stderr, "usage: <listen ip> <listen port> <dirname>\n");
        exit(EXIT_FAILURE);
    }
    listenport = atoi(argv[2]);
    bindip = argv[1];
    chdir(argv[3]);
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port = htons(atoi(argv[2]));

    if (bind(listen_sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    listen(listen_sock, 8);
    epollfd = epoll_create(10);
    if (epollfd == -1) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN;
    ev.data.fd = listen_sock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1) {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }
    for (;;) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        fprintf(stdout, "epoll_wait: ndfs = %d\n", nfds);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }
        for (n = 0;n < nfds;n++) {
            if (events[n].data.fd == listen_sock) {
                conn_sock = accept(listen_sock, NULL, NULL);
                if (conn_sock == -1) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
                ev.events = EPOLLIN;
                ev.data.fd = conn_sock;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
                    perror("epoll_ctl: conn_sock");
                    exit(EXIT_FAILURE);
                }
                recvMap[conn_sock] = malloc(sizeof(struct RecvMsg));//为socket创建接收数据的缓存
                recvMap[conn_sock]->len = 0;
                memset(recvMap[conn_sock]->data, 0, BUFFER_LEN);
            } else {
                process(events[n].data.fd);
            }
        }
    }
    return 0;
}



void process(int conn_sock) 
{
    struct RecvMsg *recv = recvMap[conn_sock];
    struct RequestLine *req = NULL;
    int len = 0;
    if (recv->len == BUFFER_LEN) {//当缓冲区满了将解析失败的数据丢掉，如果正确的请求出现在缓冲区末尾，可能会被丢掉
        recv->len = 0;
    }
    len = read(conn_sock, recv->data + recv->len, BUFFER_LEN - recv->len);
    if (len == -1) {
        perror("read");
        exit(EXIT_FAILURE);
    } 

    if (len == 0) {
        fprintf(stdout, "remote cli close connection\n");
        epoll_ctl(epollfd, EPOLL_CTL_DEL, conn_sock, NULL);
        close(conn_sock);
        free(recvMap[conn_sock]);//连接关闭，释放缓存
        return;
    } 

    recv->len += len;
    req = parse_http_msg(recv->data);

    if (req == NULL) {
        fprintf(stderr, "parse http msg failed");
        return;
    }

    //处理完请求，清空缓冲区
    recv->len = 0;
    memset(recv->data, 0, BUFFER_LEN);

    //解析成功
    process_core(req, conn_sock);
    free(req->method);
    free(req->target);
    free(req->version);
    free(req);
    close(conn_sock);

}

void process_core(struct RequestLine *req, int conn_sock)
{
    int res;
    struct stat stat_buf;
    char *path;
    char *full_path[1024];
    int len = 0;
    mode_t mode;
    int code = SUCCESS;
    if ((strlen(req->target) == 1) && (*(req->target) == '/')) {
        path = ".";//访问根目录就是访问当前目录
    } else {
        path = req->target + 1;//地址+1是为了去掉左边的/
        len = strlen(path);
        if (*(path + len - 1) == '/') {
            *(path + len - 1) = 0;//将右边的/去掉，这样返回的a标签的href不会出现//的情况
        }
    }

    if (lstat(path, &stat_buf) == -1) {
        perror("lstat");
        if (errno == ENOENT) {
            code = NOT_FOUND;
        } else {
            code = INTERNAL_ERR;
        }
    }
    if (code = SUCCESS) {
        mode = stat_buf.st_mode;
        //只处理文件和目录
        if (S_ISREG(mode)) {
            code = send_file(path, conn_sock);
        } else if (S_ISDIR(mode)) {
            code = send_dirlist(path, conn_sock);
        } else {
            code = NOT_FOUND;
        }
    }
    switch (code) {
        case NOT_FOUND:
            send_404(conn_sock);
            break;
        case INTERNAL_ERR:
            send_500(conn_sock);
            break;
        default:
            break;
    }
}

int send_file(char *path, int conn_sock)
{
    struct stat stat_buf;
    long file_size;
    int fd = -1;
    int len = 0;
    char buf[1024];
    char content_len[16];
    int code = SUCCESS;
    do {
        if (stat(path, &stat_buf) == -1) {
            perror("stat");
            code = INTERNAL_ERR;
            break;
        }
        file_size = stat_buf.st_size;
        fd = open(path, O_RDONLY);
        if (fd == -1) {
            perror("open");
            code = INTERNAL_ERR;
            break;
        }
        snprintf(content_len, 16, "%ld", file_size);
        send_statusline(conn_sock, 200, "OK");
        send_header(conn_sock, "content-type", "application/octet-stream");
        send_header(conn_sock, "content-length", content_len);
        Write(conn_sock, CRLF, strlen(CRLF));
        while ((len = read(fd, buf, 1024)) > 0) {
            Write(conn_sock, buf, len);
        }
    } while(0);

    if (fd != -1) {
        close(fd);
    }
    return code;
}

int send_dirlist(char *path, int conn_sock)
{
    int i;
    DIR *dir;
    char content_len[16];
    struct dirent *entry;
    char file_path[NAME_MAX];
    struct stat stat_buf;
    int entry_num = 0;
    char *names[NAME_MAX];
    char line[1024];
    long len = 0;
    int code = SUCCESS;

    do {
        dir = opendir(path);
        if (dir == NULL) {
            perror("opendir");
            code = INTERNAL_ERR;
            break;
        }
        while ((entry = readdir(dir))) {
            snprintf(file_path, NAME_MAX, "%s/%s", path, entry->d_name);//如果读取的不是当前工作目录下的文件，需要加上所在路径
            if (lstat(file_path, &stat_buf) == -1) {
                perror("lstat");
                code = INTERNAL_ERR;
                break;
            }
            if (!S_ISREG(stat_buf.st_mode) && !S_ISDIR(stat_buf.st_mode)) {//不是文件或目录则不返回
                continue;
            }
            len = strlen(entry->d_name);
            names[entry_num] = malloc(len + 1);
            memset(names[entry_num], 0, len + 1);
            memcpy(names[entry_num], entry->d_name, len);
            entry_num++;
        }   

        /*
        这里不能使用errno来判断，因为当errno被置为非0值后，不保证下一次进入该函数errno会被置0，导致一直满足非0条件
        if (errno != 0) {
            break;
        }*/
        if (code != SUCCESS) {
            break;
        } 
        len = 0;
        for (i = 0; i < entry_num; i++) {
            len += snprintf(line, 1024, "<a href=\"http://%s:%d/%s/%s\">%s</a><br>", bindip, listenport, path, names[i], names[i]);
        }
        snprintf(content_len, 16, "%ld", len);

        send_statusline(conn_sock, 200, "OK");
        send_header(conn_sock, "content-type", "text/html; charset=UTF-8");
        send_header(conn_sock, "content-length", content_len);
        Write(conn_sock, CRLF, strlen(CRLF));
        for (i = 0; i < entry_num; i++) {
            len = snprintf(line, 1024, "<a href=\"http://%s:%d/%s/%s\">%s</a><br>", bindip, listenport, path, names[i], names[i]);
            Write(conn_sock, line, len);
        }
    } while(0);

    if (dir != NULL) {
        closedir(dir);
    }

    return code;
}

void send_404(int conn_sock)
{
    char *body = "404 Not found\r\n";
    char content_len[16];
    snprintf(content_len, 16, "%ld", strlen(body));

    send_statusline(conn_sock, 404, "Not found");
    send_header(conn_sock, "content-type", "text/html; charset=UTF-8");
    send_header(conn_sock, "content-length", content_len);
    Write(conn_sock, CRLF, strlen(CRLF));
    Write(conn_sock, body, strlen(body));
}

void send_500(int conn_sock)
{
    char *body = "500 Internal Server Error\r\n";
    char content_len[16];
    snprintf(content_len, 16, "%ld", strlen(body));

    send_statusline(conn_sock, 500, "Internal Server Error");
    send_header(conn_sock, "content-type", "text/html; charset=UTF-8");
    send_header(conn_sock, "content-length", content_len);
    Write(conn_sock, CRLF, strlen(CRLF));
    Write(conn_sock, body, strlen(body));
}

void Write(int fd, char *buf, int len)
{
    int writed_len = 0;
    while (writed_len < len) {
        writed_len += write(fd, buf + writed_len, len - writed_len);
    }
}

void send_statusline(int conn_sock, int statuscode, char *reason)
{
    char statusline[256];
    int len = snprintf(statusline, 256, "HTTP/1.1 %d %s\r\n", statuscode, reason);
    Write(conn_sock, statusline, len);
}

void send_header(int conn_sock, char *name, char *value)
{
    char header[256];
    int len = snprintf(header, 256, "%s: %s\r\n", name, value);
    Write(conn_sock, header, len);
}