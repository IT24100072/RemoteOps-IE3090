#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9410
#define BUFFER_SIZE 1024

int send_all(int fd, const void *data, size_t len)
{
    size_t total = 0;

    while (total < len)
    {
        ssize_t sent = send(fd, (const char *)data + total, len - total, 0);

        if (sent <= 0)
            return -1;

        total += sent;
    }

    return 0;
}

int receive_line(int fd, char *buffer, int size)
{
    int index = 0;
    char ch;

    while (index < size - 1)
    {
        ssize_t n = recv(fd, &ch, 1, 0);

        if (n <= 0)
            return -1;

        buffer[index++] = ch;

        if (ch == '\n')
            break;
    }

    buffer[index] = '\0';
    return index;
}

int receive_exact(int fd, void *buffer, size_t length)
{
    size_t total = 0;

    while (total < length)
    {
        ssize_t received = recv(
            fd,
            (char *)buffer + total,
            length - total,
            0
        );

        if (received <= 0)
            return -1;

        total += received;
    }

    return 0;
}

int main()
{
    int sockfd;
    struct sockaddr_in server_addr;
    char response[BUFFER_SIZE];
    FILE *fp;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    inet_pton(
        AF_INET,
        SERVER_IP,
        &server_addr.sin_addr
    );

    if (connect(
            sockfd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        return 1;
    }

    printf("Connected to Agent.\n");

    send_all(
        sockfd,
        "AUTH OPS-0072\n",
        strlen("AUTH OPS-0072\n")
    );

    if (receive_line(
            sockfd,
            response,
            sizeof(response)) < 0)
    {
        printf("AUTH response failed.\n");
        close(sockfd);
        return 1;
    }

    printf("Agent: %s", response);

    send_all(
        sockfd,
        "GET test_upload.txt\n",
        strlen("GET test_upload.txt\n")
    );

    if (receive_line(
            sockfd,
            response,
            sizeof(response)) < 0)
    {
        printf("GET response failed.\n");
        close(sockfd);
        return 1;
    }

    printf("Agent: %s", response);

    if (strncmp(response, "OK FILE_SEND", 12) != 0)
    {
        printf("GET failed.\n");
        close(sockfd);
        return 1;
    }

    long file_size;

    if (sscanf(
            response,
            "OK FILE_SEND %ld",
            &file_size) != 1)
    {
        printf("Invalid file size.\n");
        close(sockfd);
        return 1;
    }

    printf("Expected bytes: %ld\n", file_size);

    fp = fopen("downloaded_test.txt", "wb");

    if (fp == NULL)
    {
        perror("fopen");
        close(sockfd);
        return 1;
    }

    char buffer[BUFFER_SIZE];

    long remaining = file_size;
    long total_received = 0;

    while (remaining > 0)
    {
        size_t chunk_size;

        if (remaining > BUFFER_SIZE)
            chunk_size = BUFFER_SIZE;
        else
            chunk_size = (size_t)remaining;

        if (receive_exact(
                sockfd,
                buffer,
                chunk_size) < 0)
        {
            printf("File receive failed.\n");
            fclose(fp);
            close(sockfd);
            return 1;
        }

        fwrite(buffer, 1, chunk_size, fp);

        remaining -= chunk_size;
        total_received += chunk_size;
    }

    fclose(fp);

    printf("Raw bytes received: %ld\n", total_received);

    close(sockfd);

    printf("GET completed successfully.\n");

    return 0;
}
