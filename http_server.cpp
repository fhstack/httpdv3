#include "ThreadPool.h"
#include "http_conn.h"
    
#define MAX_FD 65535
#define MAX_EVENT_NUMBER 10000

extern int addfd(int epollfd,int fd,bool one_shoot);
extern int removefd(int epollfd,int fd);

void addsig(int sig,void(handler)(int),bool restart = true)
{
    struct sigaction sa;
    memset(&sa,0,sizeof(sa));
    sa.sa_handler = handler;
    if(restart)
    {
        sa.sa_flags |= SA_RESTART;
    }

    sigfillset(&sa.sa_mask);
    sigaction(sig,&sa,NULL);
}

void show_error(int connfd,const char* info)
{
    printf("%s",info);
    send(connfd,info,strlen(info),0);
    close(connfd);
}

int main(int argc, char* argv[])
{
    if(argc == 1 || argc >3)
    {
        printf("Usage: %s [port]\n",argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    addsig(SIGPIPE,SIG_IGN);

    //创建线程池
    ThreadPool tp(4);
    int ret = tp.initialize_threadpool();
    if(ret == -1)
    {
        cerr<<"failed to initialize threadpool!"<<endl;
        return 0;
    }
    http_conn* users = new http_conn[MAX_FD];
    assert(users);

    int listenfd = socket(AF_INET,SOCK_STREAM,0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    struct linger tmp = {1,0};
    setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));

    if(bind(listenfd,(struct sockaddr*)&addr,sizeof(addr)) != 0)
    {
        perror("bind");
        return 3;
    }

    if(listen(listenfd,5) != 0)
    {
        perror("listen");
        return 4;
    }

    struct epoll_event events[MAX_EVENT_NUMBER];
    int epollfd;
    if((epollfd = epoll_create(128)) == -1)
    {
        perror("epoll_create");
        return 5;
    }

    addfd(epollfd,listenfd,false);
    http_conn::m_epollfd = epollfd;

    for(;;)
    {
        int number = epoll_wait(epollfd,events,MAX_EVENT_NUMBER, 0 );
//        if(DEBUG)
//            printf("epoll_wait: %d events ready\n",number);
        if(number < 0 && errno != EINTR)
        {
            perror("epoll failure\n");
            break;
        }

        for(int i = 0;i < number;i++)
        {
            int sockfd = events[i].data.fd;

            //连接事件
            if(sockfd == listenfd)
            {
                if(DEBUG)
                    printf("deal link event\n");
                struct sockaddr_in client;
                socklen_t len = sizeof(client);
                int connfd = accept(sockfd,(struct sockaddr*)&client,&len);

                if(connfd < 0)
                {
                    printf("errno is :%d",errno);
                    continue;
                }

                if(http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal server busy");
                    continue;
                }
                
                if(DEBUG)
                    printf("get a new client ip: %s port: %d\n",inet_ntoa(client.sin_addr),ntohs(client.sin_port));
                
                users[connfd].init(connfd,client);
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //异常事件
                users[sockfd].close_conn();
            }
            else if(events[i].events & EPOLLIN)
            {
                if(DEBUG)
                    printf("deal read event\n");
                //根据读取的结果决定是否将任务将入到线程池
                if(users[sockfd].read())
                {
                    if(DEBUG)
                        printf("加入到线程池的任务队列中\n");

                    Task* t = new Task(users+sockfd);
                    tp.add_task(t);
                }
                else
                {
                    if(DEBUG)
                        printf("不加入到线程池，关闭连接\n");
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT)
            {
                if(DEBUG)
                    printf("deal write event\n");
                //根据写的结果，决定是否关闭连接
                if(!users[sockfd].write())
                {
                    if(DEBUG)
                        printf("关闭连接\n");
                    users[sockfd].close_conn();
                }
                else
                {
                    printf("长连接\n");
                }
            }
            else
            {
                perror("未知事件\n");
                return 5;
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete[] users;
    tp.destroy_threadpool();
    printf("http_server close!\n");
    return 0;
}
