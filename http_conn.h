#pragma once

#define DEBUG false
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <cstdarg>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>

class http_conn
{
public:

    //文件名的最大长度
    static const int FILENAME_LEN = 200;

    //读缓冲区的大小
    static const int READ_BUFFER_SIZE = 2048;

    //写缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;

    //HTTP请求方法
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATCH
    };

    //解析客户请求时，服务器所处状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    //服务器处理客户请求的结果
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDTEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    //行的读取状态
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn()
    {}
    ~http_conn()
    {}

public:
    //初始化新接受到的连接
    void init(int sockfd,const sockaddr_in& addr);

    //关闭连接
    void close_conn(bool real_close = true);

    //真正处理客户请求
    void process(void);

    //非阻塞读
    bool read(void);
    
    //非阻塞写
    bool write(void);
private:
    //初始化连接
    void init(void);


    //解析HTTP请求报文
    HTTP_CODE process_read(void);

    //以下该类函数被process_read调用,分析请求报文
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request(void);
    inline char* get_line(void) {  return m_read_buf + m_start_line;}
    LINE_STATUS parse_line(void);
    
    //填充应答
    bool process_write(HTTP_CODE ret);

    //下面该类函数被process_write调用，用来填充HTTP应答
    void unmap();
    bool add_response(const char* format,...);
    bool add_content(const char* content);
    bool add_status_line(int status,const char* title);
    bool add_content_length(int content_len);
    bool add_headers(int content_length);
    bool add_linger(void);
    bool add_blank_line(void);

    //这个一定不能少！
    bool add_content_type(void);
public:

    static int m_epollfd;

    //用户数量
    static int m_user_count;

private:
    int m_sockfd;
    sockaddr_in m_address;

    //读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];

    int m_checked_idx;
    
    //客户数据已经读到的最后一个字节的下一个位置
    int m_read_idx;


    //当前正在解析的行的起始位置
    int m_start_line;

    //写缓冲区
    char m_write_buf[ WRITE_BUFFER_SIZE ];

    //写缓冲区待发送的字节数
    int m_write_idx;

    CHECK_STATE m_check_state;

    //客户的请求方法

    METHOD m_method;

    //用户请求的实际路径
    char m_real_file[FILENAME_LEN];
    char* m_url;
    
    //HTTP版本号
    char* m_version;

    //主机名
    char* m_host;

    //HTTP 请求正文的长度
    int m_content_length;

    //HTTP请求是否要求保持连接
    bool m_linger;

    //客户请求的目标文件被mmap映射到虚拟地址空间
    char* m_file_address;

    //目标文件的状态：是否存在、是否为目录、是否可执行、文件大小等
    struct stat m_file_stat;

    struct iovec m_iv[2];
    int m_iv_count;
};



