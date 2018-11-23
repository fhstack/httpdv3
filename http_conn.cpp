#include <iostream>
#include "http_conn.h"
const char* HOME_PAGE = "index.html";
const char* ok_200_title = "OK";
  
const char* error_400_title = "Bad Request";
const char* error_400_form = "Yoour request has bad syntax or is interently impossible to satisfy.\n";

const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";

const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";

const char* error_500_title = "Internal error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

const char* doc_root = "./wwwroot";

int setNonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    if(fcntl(fd, F_SETFL, new_option) != 0)
        printf("fcntl 失败\n");
    return old_option;
}

void addfd(int epollfd,int fd,bool one_shoot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

    if(one_shoot)
    {
        event.events |= EPOLLONESHOT;
    }

    if(epoll_ctl( epollfd,EPOLL_CTL_ADD,fd, &event ) !=0 && DEBUG)
        printf("epoll_ctl ADD 失败\n");
    setNonblocking(fd);
}

void modfd(int epollfd, int fd ,int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    if(epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event ) != 0 && DEBUG)
        printf("epoll_ctl mod 失败\n");
}

void removefd(int epollfd,int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL,fd,NULL);
    close(fd);
}
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool real_close)
{
    if(real_close && (m_sockfd != -1))
    {
        removefd( m_epollfd, m_sockfd );
        m_sockfd = -1;

        --m_user_count; //客户总数减一
    }
}

void http_conn::init(int sockfd,const sockaddr_in& addr)
{
    m_sockfd = sockfd;
    m_address = addr;

    int opt = 1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    addfd(m_epollfd,m_sockfd,true);
    ++m_user_count;

    init();
}
void http_conn::init(void)
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = NULL;
    m_version = NULL;
    m_content_length = 0;
    m_host = NULL;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    memset(m_read_buf,0,sizeof(m_read_buf));
    memset(m_write_buf,0,sizeof(m_write_buf));
    memset(m_real_file,0,sizeof(m_real_file));

}

http_conn::LINE_STATUS http_conn::parse_line(void)
{
    if(DEBUG)
        printf("开始解析一行\n");
    char tmp;
    for(;m_checked_idx < m_read_idx;++m_checked_idx)
    {
        tmp = m_read_buf[ m_checked_idx ];

        if(tmp == '\r')
        {
            if( ( m_checked_idx + 1 ) == m_read_idx)
                return LINE_OPEN;
            else if( m_read_buf[ m_checked_idx + 1 ] == '\n')
            {
                m_read_buf[ m_checked_idx++ ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                if(DEBUG)
                    printf("成功读取一行\n");
                return LINE_OK;
            }
        }
        else if(tmp == '\n')
        {
            if(DEBUG)
                printf("成功读取一行\n");
            m_read_buf[m_checked_idx++ ] = '\0';
            return LINE_OK;
        }
    }

    return LINE_OPEN;
}

//一直读取客户数据，知道无数据可读或对方关闭连接
bool http_conn::read(void)
{
    if( m_read_idx >= READ_BUFFER_SIZE )
    {
        return false;
    }

    int bytes_read = 0;

    while(1)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);

        if(bytes_read == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;  //读完了
            }
            else
                return false;
        }
        else if(bytes_read == 0)
        {
            //客户端关闭连接
            return false;
        }

        m_read_idx += bytes_read;
    }
    return true;
}

//解析HTTP请求行，获得请求方法，url，版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
     /* The strpbrk() function locates the first occurrence in the string s of any of the bytes in the string accept.*/
    m_url = strpbrk(text," \t");

    if(m_url == NULL)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    char* method = text;

    if(strcasecmp(method,"GET") == 0)
    {
        m_method = GET;
    }
    else if(strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        m_check_state = CHECK_STATE_CONTENT;    //若为POST 则需要解析内容
    }
    else
    {
        //其他请求，我们暂时不处理
        return BAD_REQUEST;
    }
    if(DEBUG)
        printf("method解析成功\n");
    //GET /wwwroot/index.html HTTP/1.1
    //保证跳过中间的\t
    m_url += strspn(m_url," \t");
    m_version = strpbrk(m_url," \t");

    if(m_version == NULL)
    {
        return BAD_REQUEST;
    }

    *m_version++ = '\0';
    m_version += strspn(m_version," \t");
    
    if(strcasecmp(m_version,"HTTP/1.1") != 0)
    {
        //目前只支持1.1
        return BAD_REQUEST;
    }

    if( strncasecmp(m_url,"http://",7) == 0 )
    {
        m_url += 7;
        /* The strchr() function returns a pointer to the first occurrence of the character c in the string s. */
        m_url = strchr(m_url,'/');
        if(DEBUG)
            printf("m_url is %s\n",m_url);
    }

    if(!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    if(DEBUG)
        printf("url 解析成功\n");
    m_check_state = CHECK_STATE_HEADER;//下一步解析头部
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    //遇到空行表示头部字段解析完毕
    if(text[0] == '\0')
    {
        if(DEBUG)
            printf("报头解析完成\n");
        //如果请求报文中正文段有内容
        //则需要继续读取正文部分
        if(m_content_length  != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        //否则我们已经把请求报文完整的读完
        return GET_REQUEST;
    }
    //处理Connection 字段
    else if( strncasecmp(text, "Connection:",11) == 0)
    {
        text += 11;
        //注意！！
        //是“空格\t"
        text += strspn(text," \t");
        //长连接开启
        if( strcasecmp( text, "keep-alive" ) == 0)
        {
            m_linger = true;
        }
    }
    //处理Content-Length字段
    else if(strncasecmp(text, "Content-Length:",15) == 0)
    {
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
        m_check_state = CHECK_STATE_CONTENT;
    }
    //处理host头部
    else if(strncasecmp(text, "Host:",5) == 0)
    {
        text += 5;
        text += strspn(text," \t");
        m_host = text;
    }
    else
    {
        if(DEBUG)
            printf("Unknow header: %s \n",text);
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    if(m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    else
        return BAD_REQUEST;
}

//状态机处理
http_conn::HTTP_CODE http_conn::process_read(void)
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    if(DEBUG)
        printf("segment:\n%s",m_read_buf);
    while(((m_check_state == CHECK_STATE_CONTENT) &&(line_status == LINE_OK)) ||\
              ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_checked_idx;
        if(DEBUG)
            printf("get 1 http line:%s\n",text);
        
        switch(m_check_state)
        {
            //解析请求行
            case (CHECK_STATE_REQUESTLINE):
            {
                if(DEBUG)
                    printf("解析请求行\n");
                ret = parse_request_line(text);
                if(DEBUG)
                {
                    printf("请求行解析完成\n");
                    printf("m_method:%d\nm_url:%s\nm_version:%s\n",m_method,m_url,m_version);
                }
                if(ret == BAD_REQUEST)
                {
                    if(DEBUG)
                        printf("请求行解析结果:BAD_REQUEST\n");
                    return BAD_REQUEST;
                }
                break;
            }
            //解析头部，获取Connettion和Content-Length字段
            case (CHECK_STATE_HEADER):
            {
                if(DEBUG)
                    printf("解析头部\n");
                ret = parse_headers(text);
                if(DEBUG)
                    printf("头部解析完成\n");
                if(ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST)
                {
                    if(DEBUG)
                        printf("正式开始处理请求！\n");
                    //该状态说明报文已被解析完
                    return do_request();
                }
                
                line_status = LINE_OPEN;
                break;
            }
            //解析正文内容
            case (CHECK_STATE_CONTENT):
            {
                if(DEBUG)
                    printf("解析正文\n");
                ret = parse_content(text);
                if(DEBUG)
                    printf("正文解析完成\n");
                if(ret == GET_REQUEST)
                {
                    if(DEBUG)
                        printf("正式开始处理请求\n");
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                if(DEBUG)
                    printf("case m_check_state default value\n");
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

//对正确无误的HTTP请求做出相关处理
//如果目标文件存在，对用户可读、且不是目录，则使用mmap将目标文件
//映射到虚拟地址空间的m_file_address处
http_conn::HTTP_CODE http_conn::do_request(void)
{
    strcpy(m_real_file,doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);
    if(DEBUG)
        printf("m_rear_file is :%s\n",m_real_file);

    if(stat(m_real_file,&m_file_stat) < 0)
    {
        if(DEBUG)
            printf("该文件不存在\n");
        return NO_REQUEST;
    }

    if( !(m_file_stat.st_mode & S_IROTH) )
    {
        //不可读
        if(DEBUG)
            printf("该文件不可读\n");
        return FORBIDDTEN_REQUEST;
    }

    //我们设定为如果是目录则给他index.html
    if(S_ISDIR(m_file_stat.st_mode))
    {
        if(DEBUG)
            printf("请求的网页是目录：%s\n",m_real_file);
        strcat(m_real_file,HOME_PAGE);
        if(DEBUG)    
            printf("转换为：%s\n",m_real_file);
        stat(m_real_file,&m_file_stat);
    }

    //对cgi进行处理时，就不用mmap了

    int fd = open(m_real_file,O_RDONLY);
    if((m_file_address = (char*)mmap(NULL, m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd, 0)) != (void*)-1 && DEBUG)
        printf("mmap 完成\n");
    close(fd);
    return FILE_REQUEST;
}

//解除内存映射
void http_conn::unmap(void)
{
    if(m_file_address)
    {
        if(munmap(m_file_address,m_file_stat.st_size) != 0 && DEBUG)
            printf("munmap failed!\n");
        m_file_address = NULL;
    }
}

//填写HTTP相应报文
bool http_conn::write(void)
{
    int tmp = 0;
    int bytes_have_send = 0;

    //m_write_idx 写缓冲区待发送的字节数
    int bytes_to_send = m_write_idx;
    if(bytes_to_send == 0)
    {
        //没有需要发送的内容
        //说明是读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(1)
    {
        if(DEBUG)
            printf("%s\n",m_write_buf);
        tmp = writev(m_sockfd, m_iv,m_iv_count);

        if(DEBUG)
            printf("tmp is %d",tmp);

        if(tmp <= -1)
        {
            //等待下一轮写事件
            if(errno == EAGAIN)
            {
                modfd(m_epollfd,m_sockfd,EPOLLOUT);
                return true;
            }

            unmap();
            return false;
        }

        bytes_to_send -= tmp;
        bytes_have_send += tmp;

        if(bytes_to_send <= bytes_have_send)
        {
            unmap();
            if(m_linger)
            {
                init();
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return true;
            }
            else
            {
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return false;
            }
        }
    }
}

//往读缓冲区写入待发送数据
bool http_conn::add_response(const char* format,...)
{
    if(m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }

    va_list arg_list;
    va_start(arg_list,format);
    int len = vsnprintf(m_write_buf + m_write_idx,\
                         WRITE_BUFFER_SIZE-1-m_write_idx,\
                         format,arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        return true;
    }

    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status,const char* title)
{
    if(DEBUG)
        printf("填写请求行\n");
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}

bool http_conn::add_headers(int content_len)
{
    return add_content_type()&&add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_type(void)
{
    if(DEBUG)
        printf("填写正文类型\n");
    return add_response("Content-type: text/html;charser=ISO-8859-1");
}
bool http_conn::add_content_length(int content_len)
{
    if(DEBUG)
        printf("填写正文长度\n");
    return add_response("Content-Length: %d\r\n",content_len);
}

bool http_conn::add_linger(void)
{
    if(DEBUG)
        printf("填写连接模式\n");
    return add_response("Connection: %s\r\n",(m_linger == true)? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    if(DEBUG)
        printf("填写空行\n");
    return add_response("\r\n");
}

//暂时还木有用它
//想用也非常方便啦
bool http_conn::add_content(const char* content)
{
    if(DEBUG)
        printf("填写正文\n");
    return add_response("%s",content);
}

//给客户返回的结果处理
bool http_conn::process_write(HTTP_CODE ret)
{
    switch(ret)
    {
        //内部错误
        case INTERNAL_ERROR:
        {
            add_status_line(500,error_500_title);
            add_headers( strlen(error_500_form) );

            if(!add_content( error_500_form ))
            {
                return false;
            }
            break;
        }
        //错误的请求
        case BAD_REQUEST:
        {
            add_status_line(400,error_400_title);
            add_headers(strlen(error_400_form));

            if(!add_content(error_400_form))
            {
                return false;
            }
            break;
        }

        //客户请求的其资源不存在
        case NO_RESOURCE:
        {
            add_status_line(404,error_404_title);
            add_headers(strlen(error_404_form));

            if(!add_content(error_404_form))
            {
                return false;
            }

            break;
        }
        //服务器拒绝客户访问
        case FORBIDDTEN_REQUEST:
        {
            add_status_line(403,error_403_title);
            add_headers( strlen(error_403_form));
            if(!add_content(error_403_form))
            {
                return false;
            }
            break;
        }

        case FILE_REQUEST:
        {
            if(DEBUG)
                printf("FILE_REQUEST\n");
            add_status_line(200,ok_200_title);

            if(m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else
            {
                const char* ok_string = "<html><body>welcome a better web server </body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string))
                {
                    return false;
                }
                return true;
            }
        }

        default:
            if(DEBUG)
                printf("HTTP_CODE: %d",ret);
            return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

void http_conn::process(void)
{
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST)
    {
        if(DEBUG)
            printf("NO_REQUEST\n");
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return;
    }
    
    if(DEBUG)
        printf("请求报文读取完成,准备填写响应报文\n");

    bool write_ret = process_write(read_ret);
    
    if(!write_ret)
    {
        if(DEBUG)
            printf("错误，关闭该连接\n");
        close_conn();
    }
    
    modfd(m_epollfd,m_sockfd,EPOLLOUT);
    if(DEBUG)
        printf("将事件设置成 EPOLLOUT,执行结束\n");
}
