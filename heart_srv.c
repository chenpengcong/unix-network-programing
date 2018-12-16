#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define FD_SET_SIZE 1024

void str_srv(int listenfd);
int max(int a, int b);

int main(int argc, char **argv)
{
    int listenfd;
    struct sockaddr_in srv_addr;
    if (argc != 2) {
        fprintf(stderr, "usage: <listen-port>");
    }
    bzero(&srv_addr, sizeof(srv_addr));
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port = htons(atoi(argv[1]));

    if (bind(listenfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    listen(listenfd, 8);
    str_srv(listenfd);
    return 0;
}


int max(int a, int b)
{
    return a > b ? a: b;
}

void str_srv(int listenfd)
{
    fd_set readfds;
    int nfds = listenfd + 1;
    int accept_fd;
    int fd_num = 1;
    int fd_arr[FD_SET_SIZE];
    int n_ready = 0;
    int i = 0;
    ssize_t read_count = 0;
    ssize_t write_count = 0;
    ssize_t write_index = 0;
    char buf[1024];

    FD_ZERO(&readfds);
    memset(fd_arr, -1, FD_SET_SIZE * sizeof(int));
    fd_arr[0] = listenfd;
    for(;;) {
        FD_ZERO(&readfds);
        for (i = 0;i < FD_SET_SIZE;i++) {
            if (fd_arr[i] >= 0) {
                FD_SET(fd_arr[i], &readfds);
            }
        }
        fprintf(stdout, "nfds:%d\n", nfds);
        if ((n_ready = select(nfds, &readfds, NULL, NULL, NULL)) == -1) {
            if (errno == EINTR) {
                continue;
            }else {
                perror("select");
                exit(EXIT_FAILURE);   
            }
        }
        fprintf(stdout, "n_ready:%d\n", n_ready);
        if (FD_ISSET(listenfd, &readfds)) {
            if ((accept_fd = accept(listenfd, NULL, NULL)) == -1) {
                perror("accept");
                exit(EXIT_FAILURE);
            }
            fprintf(stdout, "accept a new connection, accept_fd:%d\n", accept_fd);
            for (i = 1;i < FD_SET_SIZE;i++) {
                if (fd_arr[i] == -1) {
                    fd_arr[i] = accept_fd;
                    nfds = max(nfds, fd_arr[i]) + 1;
                    break;
                }
            }
            n_ready--;
        }

        while (n_ready > 0) {
            for (i = 1;i < FD_SET_SIZE;i++) {
                if (fd_arr[i] > 0 && FD_ISSET(fd_arr[i], &readfds)) {
                    read_count = read(fd_arr[i], buf, 1024);
                    if (read_count == -1) {
                        perror("read");
                        exit(EXIT_FAILURE);
                    }
                    if (read_count == 0) {//receive FIN
                        close(fd_arr[i]);
                        fd_arr[i] = -1;
                    }
                    fprintf(stdout, "receive %d bytes from remote cli:%s\n", read_count, buf);
                    write_index = 0;
                    while (write_index != read_count) {
                        if ((write_count = write(fd_arr[i], buf + write_index, read_count - write_index)) == -1) {
                            perror("write");
                            exit(EXIT_FAILURE);
                        }
                        write_index += write_count;
                    }
                    n_ready--;
                }
            }
        }

    }
}

