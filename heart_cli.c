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

int max(int a, int b);
void str_cli(int stdinfd, int stdoutfd, int connfd);

int main(int argc, char **argv)
{
    int connfd;
    struct sockaddr_in srv_addr;
    if (argc != 3) {
        fprintf(stderr, "usage: <remote-ip> <remote-port>\n");
        exit(EXIT_FAILURE);
    }
    if (inet_pton(AF_INET, argv[1], &(srv_addr.sin_addr)) == -1) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(atoi(argv[2]));
    connfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(connfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) == -1) {
        perror("connect");
        exit(EXIT_FAILURE);
    }
    str_cli(STDIN_FILENO, STDOUT_FILENO, connfd);
    return 0;
}


int max(int a, int b)
{
    return a > b ? a: b;
}

void str_cli(int stdinfd, int stdoutfd, int connfd) {
    int ndfs;
    ssize_t read_count = 0;
    ssize_t write_count = 0;
    ssize_t write_index = 0;
    fd_set readfds;
    char buf[1024];
    ndfs = max(stdinfd, connfd) + 1;
    for(;;) {
        FD_ZERO(&readfds);
        FD_SET(stdinfd, &readfds);
        if (connfd > 0)
            FD_SET(connfd, &readfds);
        if((select(ndfs, &readfds, NULL, NULL, NULL)) == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                perror("select");
                exit(EXIT_FAILURE);
            }
        }

        if (FD_ISSET(stdinfd, &readfds)) {
            if ((read_count = read(stdinfd, buf, 1024)) == -1) {
                perror("read");
                exit(EXIT_FAILURE);
            }
            write_index = 0;
            fprintf(stdout, "recevie %d bytes from stdin\n", read_count);
            while (write_index != read_count) {
                if ((write_count = write(connfd, buf + write_index, read_count - write_index)) == -1) {
                    perror("write");
                    exit(EXIT_FAILURE);
                }
                write_index += write_count;
            }
        }

        if (FD_ISSET(connfd, &readfds)) {
            read_count = read(connfd, buf, 1024);
            fprintf(stdout, "recevie %d bytes from remote server\n", read_count);
            if (read_count == -1) {
                perror("read");
                exit(EXIT_FAILURE);
            }
            if (read_count == 0) {//receive FIN
                fprintf(stdout, "receive FIN\n");
                close(connfd);
                connfd = -1;
            }
            fprintf(stdout, "receive msg from remote server\n");
            write_index = 0;
            while (write_index != read_count) {
                if ((write_count = write(stdoutfd, buf + write_index, read_count - write_index)) == -1) {
                    perror("write");
                    exit(EXIT_FAILURE);
                }
                write_index += write_count;
            }
        }
    }
}

