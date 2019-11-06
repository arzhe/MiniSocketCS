#include <stdio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int main()
{
    int clientfd;
    int len;
    struct sockaddr_in serv_addr;
    int result;
    char ch = 'A';

    clientfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(9734);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);
    len = sizeof(serv_addr);
    result = connect(clientfd, (struct sockaddr*)&serv_addr, len);

    if(result == -1) 
    {
        perror("oops:client1");
        exit(1);
    }
    write(clientfd, &ch, 1);
    read(clientfd, &ch, 1);
    printf("char from server = %c\n", ch);
    close(clientfd);
    exit(0);
}

