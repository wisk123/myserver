#include<sys/socket.h>
#include<arpa/inet.h>//这个头文件包含了<netinet/in.h>，不用再次包含了
#include<string.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<errno.h>
#include<iostream>


#define MAX_EVENTS 1024
#define READ_BUFFER 1024

void setnonblocking(int fd){
    fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);
}

void errif(bool condition,const char*errmsg){
    if(condition){
        perror(errmsg);
        exit(EXIT_FAILURE);
    }
}
int main()
{



int sockfd=socket(AF_INET,SOCK_STREAM,0);//向操作系统申请一个「TCP 网络通信的插座」，返回它的身份证号（sockfd）。
std::cout<<sockfd<<std::endl;
errif(sockfd==-1,"socket create error");

struct sockaddr_in serv_addr;
bzero(&serv_addr, sizeof(serv_addr));//==memset(&serv_addr,0,sizeof(serv_addr));
serv_addr.sin_family=AF_INET;//AF_INET格式192.168.1.1//AF_INET6	IPv6格式2001:db8::1	下一代 IP，现在慢慢普及	
serv_addr.sin_addr.s_addr=inet_addr("127.0.0.1");//// 把 "192.168.1.100" 这种字符串 → 转成网络能用的整数
serv_addr.sin_port=htons(8888);//host to network short 把端口号转换成「网络能识别的格式」


errif(bind(sockfd,(sockaddr*)&serv_addr,sizeof(serv_addr))==-1,"socket bind error");
//sockfd 是内核 socket 对象的编号，不是对象本身。bind 通过编号找到对象，再把 IP、端口绑到对象上。
errif(listen(sockfd,SOMAXCONN)==-1,"socket listen error");//把 socket 变成「监听模式」，让它可以接收客户端的连接请求。
// SOMAXCONN 系统定义的最大监听队列长度 意思：最多同时排队等待处理的连接数

int epfd=epoll_create1(0);//创建一个 epoll 监控池（红黑树实现），返回它的文件描述符 epfd。后面所有 epoll 操作，全靠这个 epfd 来识别。
//socket() → bind() → listen() →epoll_create1() → epoll_ctl(ADD) → epoll_wait()
errif(epfd==-1,"epoll create error");

struct epoll_event events[MAX_EVENTS],ev;//events[] —— 接收用  ev —— 注册用
//ev 是单个因为每次只注册一个 fd。//events[] 是数组因为 epoll_wait 一次会返回很多个触发事件（可能几十上百个）。
//bzero(&evstruct epoll_event {
//  uint32_t events;    // 事件类型：EPOLLIN / EPOLLET 等//EPOLLIN = 有数据 / 有连接 → 触发！//EPOLLET = 边缘触发（Edge Triggered）只有状态变化时，才通知一次！
//  epoll_data_t data;  // 自定义数据：通常存 fd
//};ents,sizeof(events));
//epoll默认采用LT触发模式，即水平触发，只要fd上有事件，就会一直通知内核。这样可以保证所有事件都得到处理、不容易丢失，但可能发生的大量重复通知也会影响epoll的性能。




bzero(&ev,sizeof(ev));
ev.data.fd=sockfd;/// 事件触发时返回这个 fd
ev.events=EPOLLIN | EPOLLET; // 设置事件：读事件 + 边缘触发
setnonblocking(sockfd);//sockfd（socket）从「阻塞模式」改成「非阻塞模式」
epoll_ctl(epfd,EPOLL_CTL_ADD,sockfd,&ev);//把一个 socket(sockfd) 交给内核的 epoll 红黑树去监听！
//从此内核会自动帮你盯着这个 socket 有没有事件发生！
//监听什么事件？（EPOLLIN 读事件 / EPOLLET 边缘触发）//出事件返回什么？（通常返回 sockfd）
//EPOLLIN = 内核，当这个 fd 有数据可读时，告诉我！

while(true){
    int nfds=epoll_wait(epfd,events,MAX_EVENTS,-1);//程序阻塞在这里，等待内核告诉我：有多少个 socket 发生事件了！
    //int nfds = epoll_wait(
    //epfd,        // 1. 你要等哪个 epoll（红黑树）
    //events,      // 2. 内核把触发的事件 填到这个数组里
    //MAX_EVENTS,  // 3. 最多一次拿多少个事件（如 100/1024）
    //-1           // 4. 超时时间：-1 = 永久阻塞，直到有事件
    //);
    errif(nfds==-1,"epoll wait error");
    for(int i=0;i<nfds;++i){
        if(events[i].data.fd==sockfd){ //新客户端连接
            struct sockaddr_in clnt_addr;
            bzero(&clnt_addr,sizeof(clnt_addr));
            socklen_t clnt_addr_len=sizeof(clnt_addr);//Linux 专门用来表示地址长度的整数类型（必须用这个，不能用 int）
            //（底层通常是unsigned int/unsigned short），作用是统一网络地址长度的变量类型，

            int clnt_sockfd=accept(sockfd,(sockaddr*)&clnt_addr,&clnt_addr_len);
            errif(clnt_sockfd==-1,"socket accept error");
            printf("new clint fd %d! IP:%s  Port:%d\n",clnt_sockfd,inet_ntoa(clnt_addr.sin_addr),ntohs(clnt_addr.sin_port));
            
            bzero(&ev,sizeof(ev));
            ev.data.fd=clnt_sockfd;
            ev.events=EPOLLIN|EPOLLET;
            setnonblocking(clnt_sockfd);
            epoll_ctl(epfd,EPOLL_CTL_ADD,clnt_sockfd,&ev);

        }else if(events[i].events&EPOLLIN){//可读事件
            char buf[READ_BUFFER];
            while(true){ //由于使用非阻塞IO，读取客户端buffer，一次读取buf大小数据，直到全部读取完毕

                bzero(&buf,sizeof(buf));
                ssize_t bytes_read=read(events[i].data.fd,buf,sizeof(buf));
                if(bytes_read>0){
                    printf("message from client fd %d: %s \n",events[i].data.fd,buf);
                    write(events[i].data.fd,buf,sizeof(buf));

                }else if(bytes_read==-1&&errno==EINTR){
                    printf("continue reading");
                    continue;

                }else if(bytes_read==-1&&((errno==EAGAIN||errno==EWOULDBLOCK))){//非阻塞IO，这个条件表示数据全部读取完毕
                    printf("finish reading once, errno: %d\n", errno);
                    break;
                }else if(bytes_read == 0){  //EOF，客户端断开连接
                    printf("EOF, client fd %d disconnected\n", events[i].data.fd);
                    close(events[i].data.fd);   //关闭socket会自动将文件描述符从epoll树上移除
                    break;
                }
            }

        }
        else{         //其他事件，之后的版本实现
            printf("something else happened\n");
        }
    }
}


close(sockfd);


return 0;
}

/*socket的结构
struct socket {
    // 1. 类型：TCP / UDP
    int type;           // SOCK_STREAM (TCP) / SOCK_DGRAM (UDP)

    // 2. 协议族：IPv4 / IPv6
    int family;         // AF_INET (IPv4)

    // 3. 绑定的 IP + 端口
    struct sockaddr_in local_addr;

    // 4. 对端（客户端）的 IP + 端口
    struct sockaddr_in peer_addr;

    // 5. 状态：LISTEN / ESTABLISHED / CLOSED
    int state;

    // 6. 接收缓冲区（收到的数据存在这里）
    char recv_buf[4096];

    // 7. 发送缓冲区（要发的数据存在这里）
    char send_buf[4096];

    // 8. 等待连接的队列（listen 用）
    struct list_head accept_queue;
};
*/