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
#include <signal.h>

static int nsec = 1;
static int max_count = 10;//持续10秒没有收到对端应答认为对端已不再存活
static int counts[FD_SETSIZE] = {0};//存放1024个计数器，counts[i]记录的是文件描述符fd_arr[i]的计数值
static int dis_conn_fds[FD_SETSIZE] = {0};//记录1024个文件描述符的状态，dis_conn_fds[i]的值为-1表示文件描述符fd_arr[i]已断开连接
static int fd_arr[FD_SETSIZE];//存放文件描述符

#define SHUT_FD(fd) do {\
                        shutdown(fd, SHUT_RDWR);        \
                        close(fd);                      \
                        fd = -1;                        \
                    }while(0)

void str_srv(int listenfd);
int max(int a, int b);
void alrm_handler(int sig);
void set_signal(int signo, void (*handler)(int));
int main(int argc, char **argv)
{
    int listenfd;
    struct sockaddr_in srv_addr;
    if (argc != 2) {
        fprintf(stderr, "usage: <listen-port>\n");
        exit(EXIT_FAILURE);
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
    set_signal(SIGALRM, alrm_handler);
    alarm(nsec);
    str_srv(listenfd);
    return 0;
}


int max(int a, int b)
{
    return a > b ? a: b;
}


void alrm_handler(int sig) 
{
    int i;
    for (i = 1;i < FD_SETSIZE;i++) {
        if (fd_arr[i] > 0) {
            counts[i]++;
            if (counts[i] >= max_count) {//对端连续超过max_count秒没有响应
                dis_conn_fds[i] = -1;
            }
        }
    }
    alarm(nsec);
}



void str_srv(int listenfd)
{
    fd_set readfds;
    fd_set exceptfds;
    int nfds = listenfd + 1;
    int accept_fd;
    int fd_num = 1;
    //int fd_arr[FD_SETSIZE];
    int n_ready = 0;
    int i = 0;
    ssize_t read_count = 0;
    ssize_t write_count = 0;
    ssize_t write_index = 0;
    char buf[1024];
    char c = 0;

    FD_ZERO(&readfds);
    FD_ZERO(&exceptfds);
    memset(fd_arr, -1, FD_SETSIZE * sizeof(int));
    fd_arr[0] = listenfd;
    for(;;) {
        FD_ZERO(&readfds);
        for (i = 0;i < FD_SETSIZE;i++) {
            if (fd_arr[i] >= 0) {
                FD_SET(fd_arr[i], &readfds);
                FD_SET(fd_arr[i], &exceptfds);
            }
        }
        if ((n_ready = select(nfds, &readfds, NULL, &exceptfds, NULL)) == -1) {
            if (errno == EINTR) {
                fprintf(stdout, "%s\n", "select return with EINTR");
                //处理完alrm信号后select会返回，在此断开一些无效的连接
                for (i = 1;i < FD_SETSIZE;i++) {//索引从1开始是因为fd_arr[0]存放的是listenfd，不需要判断是否断开连接
                    if (dis_conn_fds[i] < 0) {
                        fprintf(stdout, "close fd %d\n", fd_arr[i]);
                        SHUT_FD(fd_arr[i]);
                        fprintf(stderr, "fd_arr[%d]:%d\n", i, fd_arr[i]);
                        dis_conn_fds[i] = 0;
                    }
                }
                continue;
            }else {
                perror("select");
                exit(EXIT_FAILURE);   
            }
        }
        fprintf(stdout, "select return, n_ready:%d\n", n_ready);
        if (FD_ISSET(listenfd, &readfds)) {
            if ((accept_fd = accept(listenfd, NULL, NULL)) == -1) {
                perror("accept");
                exit(EXIT_FAILURE);
            }
            fprintf(stdout, "accept a new connection, accept_fd:%d\n", accept_fd);
            for (i = 1;i < FD_SETSIZE;i++) {
                if (fd_arr[i] == -1) {
                    fd_arr[i] = accept_fd;
                    nfds = max(nfds, fd_arr[i]) + 1;
                    counts[i] = 0;
                    dis_conn_fds[i] = 0;
                    break;
                }
            }
            n_ready--;
        }

        while (n_ready > 0) {
            for (i = 1;i < FD_SETSIZE;i++) {
                if (fd_arr[i] > 0 && FD_ISSET(fd_arr[i], &exceptfds)) {
                        if(recv(fd_arr[i], &c, 1, MSG_OOB) == -1) {
                            if (errno != EWOULDBLOCK) {
                                perror("recv");
                                exit(EXIT_FAILURE);    
                            }
                            fprintf(stdout, "%s\n", "recv return with EWOULDBLOCK");//errno为EWOULDBLOCK表示OOB数据还未到达，但其实也是一次心跳应答
                        }
                        fprintf(stdout, "receive OOB data:%c\n", c);
                        send(fd_arr[i], "1", 1, MSG_OOB);
                        n_ready--;
                        counts[i] = 0;//接收到OOB，心跳计数重置为0
                }
                if (fd_arr[i] > 0 && FD_ISSET(fd_arr[i], &readfds)) {
                    read_count = read(fd_arr[i], buf, 1024);
                    if (read_count == -1) {
                        if (errno != ECONNRESET) {//经验证，当客户端/服务端加上处理OOB数据的功能后，当程序被kill时直接发送RST而不是FIN
                            perror("read");
                            exit(EXIT_FAILURE);
                        }
                        fprintf(stderr, "%s\n", "receive RST, close connection");
                        SHUT_FD(fd_arr[i]);    
                    } else if (read_count == 0) {//receive FIN
                        fprintf(stdout, "%s\n", "revevie FIN, close connection");
                        SHUT_FD(fd_arr[i]);
                    } else {
                        fprintf(stdout, "receive %zd bytes from remote cli:%s\n", read_count, buf);
                        write_index = 0;
                        while (write_index != read_count) {
                            if ((write_count = write(fd_arr[i], buf + write_index, read_count - write_index)) == -1) {
                                perror("write");
                                exit(EXIT_FAILURE);
                            }
                            write_index += write_count;
                        }
                    }
                    n_ready--;
                }
            }
        }

    }
}

void set_signal(int signo, void (*handler)(int))
{
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_handler = handler;
    act.sa_flags = 0;
    sigaction(signo, &act, NULL);
}