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
#include <signal.h>
#include <fcntl.h>

static int connfd = -1;
static int nsec = 0;
static int count = 0;
static int max_count = 0;

#define SHUT_CONN_FD do {                                   \
                        shutdown(connfd, SHUT_RDWR);        \
                        close(connfd);                      \
                        connfd = -1;                        \
                    }while(0)       


int max(int a, int b);
void str_cli(int stdinfd, int stdoutfd);
void heartbeat(int cnt, int max_cnt);
void urg_handler(int sig);
void alrm_handler(int sig);
void set_signal(int signo, void (*handler)(int));


int main(int argc, char **argv)
{
    //int connfd;
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
    heartbeat(1, 10);
    str_cli(STDIN_FILENO, STDOUT_FILENO);
    return 0;
}

int max(int a, int b)
{
    return a > b ? a: b;
}

void str_cli(int stdinfd, int stdoutfd) {
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
                fprintf(stdout, "%s\n", "select return with EINTR");
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
            fprintf(stdout, "recevie %zd bytes from stdin\n", read_count);
            if (connfd > 0) {
                while (write_index != read_count) {
                    if ((write_count = write(connfd, buf + write_index, read_count - write_index)) == -1) {
                        perror("write");
                        exit(EXIT_FAILURE);
                    }
                    write_index += write_count;
                }        
            }

        }

        if (FD_ISSET(connfd, &readfds)) {
            read_count = read(connfd, buf, 1024);
            fprintf(stdout, "recevie %zd bytes from remote server\n", read_count);
            if (read_count == -1) {
                if (errno != ECONNRESET) {//经验证，当客户端/服务端加上处理OOB数据的功能后，当程序被kill时直接发送RST而不是FIN
                    perror("read");
                    exit(EXIT_FAILURE);
                }
                fprintf(stderr, "%s\n", "receive RST, close connection");
                SHUT_CONN_FD;
            } else if (read_count == 0) {//receive FIN
                fprintf(stdout, "receive FIN\n");
                SHUT_CONN_FD;
            } else {
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
}


void urg_handler(int sig)
{
    char c;
    if(recv(connfd, &c, 1, MSG_OOB) == -1) {
        if (errno != EWOULDBLOCK) {
            perror("recv");
            exit(EXIT_FAILURE);    
        }
        fprintf(stdout, "%s\n", "recv return with EWOULDBLOCK");
    }
    fprintf(stdout, "receive OOB data:%c\n", c);
    count = 0;
}


void alrm_handler(int sig) {
    count++;
    fprintf(stdout, "%s\n", "alrm_handler");
    if (count > max_count) {
        fprintf(stderr, "%s\n", "server is unreachable");
        SHUT_CONN_FD;
        count = max_count + 1;
        return;
    } else {
        send(connfd, "1", 1, MSG_OOB);
    }
    alarm(nsec);
}

void heartbeat(int n, int max_cnt)
{
    struct sigaction act;

    nsec = n < 1 ? 1: n;
    max_count = max_cnt < n ? n: max_cnt;

    fcntl(connfd, F_SETOWN, getpid());
    set_signal(SIGALRM, alrm_handler);
    set_signal(SIGURG, urg_handler);
    alarm(nsec);
}

void set_signal(int signo, void (*handler)(int))
{
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_handler = handler;
    act.sa_flags = 0;
    sigaction(signo, &act, NULL);
}