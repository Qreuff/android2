#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);
    connect(sock, (struct sockaddr*)&server, sizeof(server));
    char* msg = "Hello World!";
    send(sock, msg, strlen(msg), 0);
    char buffer[100];
    recv(sock, buffer, sizeof(buffer), 0);
    printf("Server: %s\n", buffer);
    close(sock);
    return 0;
}