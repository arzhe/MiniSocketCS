#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#include <sys/wait.h>


#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server:jbdhttpd/0.1.0\r\n"
#define STDIN 0
#define STDOUT 1
#define STDERR 2
#define SA struct sockaddr

void error_die(const char*);
int startup(u_int16_t*);
void accept_request(void*);
int get_line(int, char*, int);
void unimplemented(int);
void not_found(int);
void serve_file(int, const char*);
void headers(int, const char*);
void cat(int, FILE*);
void execute_cgi(int, const char*, const char*, const char*);
void bad_request(int);
void cannot_execute(int);

// 出错处理函数
void error_die(const char *sc)
{
    perror(sc); // stdio.h
    exit(1); // stdlib.h
}


int startup(u_int16_t *port)
{
    int httpd = 0;
    int on = 1;
    struct sockaddr_in httpd_addr;

    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if(httpd == -1)
        error_die("socket failed");
    memset(&httpd_addr, 0, sizeof(httpd_addr));
    httpd_addr.sin_family = AF_INET;
    httpd_addr.sin_port = htons(*port);
    httpd_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
        error_die("setsockopt failed");    
    if(bind(httpd, (SA*)&httpd_addr, sizeof(httpd_addr)) < 0)
        error_die("bind failed");
    if(*port == 0) // 如果动态的分配一个端口号
    {
        socklen_t httpd_addr_len = sizeof(httpd_addr);
        // 在以端口号0调用bind后（告知内核去选择本地端口号后
        // getsockname用于返回由内核赋予的本地端口号
        if(getsockname(httpd, (SA*)&httpd_addr, &httpd_addr_len) == -1) 
            error_die("getsockname failed");
        *port = ntohs(httpd_addr.sin_port);
    }
    if(listen(httpd, 5) < 0) 
        error_die("listen failed");
    return(httpd);
}

int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if(n > 0)
        {
            if(c == '\r')
            {   
                /*****************************************************/
                /* MSG_PEEK适用于recv和recvfrom，它允许我们查看在缓冲区
                 * 排队的数据，而且系统不在recv或recvfrom返回后丢弃这些数据。
                 * 也就是我们既想查看数据，又想数据仍然留在接收队列中以
                 * 供本进程其他部分稍后读取*/
                /*****************************************************/
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else 
            c = '\n';
    }
    buf[i] = '\0';

    return(i);
}

void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type:text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

void not_found(int client) 
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type:text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf),0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

void headers(int client, const char *filename)
{
    char buf[1024];
    // filename未曾使用，编译器编译时会警告报错
    // 加上（void）只是告诉编译器，该变量已经使用了
    (void)filename; 

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "Content-Type:text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

void cat(int client, FILE *resource) 
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while(!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

void serve_file(int client, const char *filename) 
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A'; buf[1] = '\0';
    while((numchars > 0) && strcmp("\n", buf))
        numchars = get_line(client, buf, sizeof(buf));
    
    resource = fopen(filename, "r");
    if(resource == NULL)
        not_found(client);
    else
    {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

void bad_request(int client) 
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type:text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request,");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, strlen(buf), 0);
}

void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type:text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{   
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A'; buf[1] = '\0';
    if(strcasecmp(method, "GET") == 0)
        while((numchars > 0) && strcmp("\n", buf))
            numchars = get_line(client, buf, sizeof(buf));
    else if(strcasecmp(method, "POST") == 0)
    {
        numchars = get_line(client, buf, sizeof(buf));
        while((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if(strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if(content_length == -1)
        {
            bad_request (client);
            return;
        }
    }
    else 
    {

    }

    if(pipe(cgi_output) < 0)
    {
        cannot_execute(client);
        return;
    }
    if(pipe(cgi_input) < 0)
    {
        cannot_execute(client);
        return;
    }

    if((pid = fork()) < 0)
    {
        cannot_execute(client);
        return;
    }
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    if(pid == 0)
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], STDOUT);
        dup2(cgi_input[0], STDIN);
        close(cgi_output[0]);
        close(cgi_input[1]);
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if(strcasecmp(method, "GET") == 0)
        {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else
        {
            sprintf(length_env, "CONTENT-LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path, NULL);
        exit(0);
    }
    else 
    {
        close(cgi_output[1]);
        close(cgi_input[0]);
        if(strcasecmp(method, "POST") == 0)
            for(i = 0; i < content_length; i++)
            {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        while(read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}

void accept_request(void *arg)
{
    int client = (intptr_t)arg;
    char buf[1024];
    size_t numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0; // 当服务端决定了这是个CGI程序时变为ture
    char *query_string = NULL;

    numchars = get_line(client, buf, sizeof(buf));
    i = 0; j = 0;
    while(!ISspace(buf[i]) && (i < sizeof(method) - 1)) 
    {
        method[i] = buf[i];
        i++;
    }
    j = i;
    method[i] = '\0';

    if(strcasecmp(method, "GET") && strcasecmp(method, "POST")) 
    {
        unimplemented(client);
        return;
    }

    if(strcasecmp(method, "POST") == 0)
        cgi = 1;

    i = 0;
    while(ISspace(buf[j]) &&  (j < numchars))
        j++;
    while(!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

    if(strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if(*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    sprintf(path, "htdocs%s", url);
    if(path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    if(stat(path, &st) == -1)
    {
        while((numchars > 0) && strcmp("\n", buf)) // 读取出留在缓冲区的数据
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else 
    {
        if((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
        if((st.st_mode & S_IXUSR) ||
           (st.st_mode & S_IXGRP) ||
           (st.st_mode & S_IXOTH))
            cgi = 1;
        if(!cgi)
            serve_file(client, path);
        else 
            execute_cgi(client, path, method, query_string);
    }
    close(client);
}

int main(void)
{
    int server_sock = -1;
    u_int16_t port = 4000; // 无符号的短整型端口号
    int client_sock = -1;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t newthread;

    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    while(1)
    {
        client_sock = accept(server_sock, (SA*)&client_addr, &client_addr_len);
        if(client_sock == -1)
            error_die("accept failed");
        // pthread_create是POSIX的默认库，但却不是Linux系统的默认库
        // 所以在编译时需要加-lpthread
        if(pthread_create(&newthread, NULL, (void*)accept_request, (void*)(intptr_t)client_sock) != 0)
            perror("pthread_create failed");
    }

    close(server_sock);

    return(0);
}
