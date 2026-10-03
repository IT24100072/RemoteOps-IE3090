#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define SERVER_IP "127.0.0.1"
#define TCP_PORT 9410
#define UDP_PORT 9411
#define BUFFER_SIZE 1024

#define AUTH_TOKEN "OPS-0072"

volatile int monitor_running = 0;
int udp_socket_fd = -1;
pthread_t monitor_thread;

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
        ssize_t received = recv(fd, &ch, 1, 0);

        if (received <= 0)
            return -1;

        buffer[index++] = ch;

        if (ch == '\n')
            break;
    }

    buffer[index] = '\0';
    return index;
}

void receive_exec_output(int sockfd)
{
    char buffer[BUFFER_SIZE];

    while (1)
    {
        if (receive_line(sockfd, buffer, sizeof(buffer)) < 0)
            return;

        if (strcmp(buffer, "END EXEC SID:2700\n") == 0)
            break;

        printf("%s", buffer);
    }
}

void receive_process_list(int sockfd)
{
    char buffer[BUFFER_SIZE];

    while (1)
    {
        if (receive_line(sockfd, buffer, sizeof(buffer)) < 0)
            return;

        if (strcmp(buffer, "END PROCS SID:2700\n") == 0)
            break;

        printf("%s", buffer);
    }
}

void *udp_monitor_thread(void *arg)
{
    (void)arg;

    char buffer[BUFFER_SIZE];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    printf("\n[UDP] Monitoring thread started on port %d\n", UDP_PORT);

    while (monitor_running)
    {
        struct timeval tv;

        tv.tv_sec = 1;
        tv.tv_usec = 0;

        setsockopt(
            udp_socket_fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &tv,
            sizeof(tv)
        );

        ssize_t received = recvfrom(
            udp_socket_fd,
            buffer,
            sizeof(buffer) - 1,
            0,
            (struct sockaddr *)&sender_addr,
            &sender_len
        );

        if (received > 0)
        {
            buffer[received] = '\0';

            printf("\n========== UDP MONITOR ==========\n");
            printf("%s", buffer);
            printf("=================================\n");
            printf("remoteops> ");
            fflush(stdout);
        }
    }

    printf("\n[UDP] Monitoring thread stopped.\n");

    return NULL;
}

int start_udp_monitor(void)
{
    struct sockaddr_in local_addr;

    if (monitor_running)
    {
        printf("UDP monitoring is already running.\n");
        return 0;
    }

    udp_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (udp_socket_fd < 0)
    {
        perror("UDP socket");
        return -1;
    }

    memset(&local_addr, 0, sizeof(local_addr));

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(UDP_PORT);

    if (bind(
            udp_socket_fd,
            (struct sockaddr *)&local_addr,
            sizeof(local_addr)) < 0)
    {
        perror("UDP bind");
        close(udp_socket_fd);
        udp_socket_fd = -1;
        return -1;
    }

    monitor_running = 1;

    if (pthread_create(
            &monitor_thread,
            NULL,
            udp_monitor_thread,
            NULL) != 0)
    {
        perror("pthread_create");
        monitor_running = 0;
        close(udp_socket_fd);
        udp_socket_fd = -1;
        return -1;
    }

    printf("[UDP] Listening on port %d\n", UDP_PORT);

    return 0;
}

void stop_udp_monitor(void)
{
    if (!monitor_running)
    {
        printf("UDP monitoring is not running.\n");
        return;
    }

    monitor_running = 0;

    pthread_join(monitor_thread, NULL);

    close(udp_socket_fd);
    udp_socket_fd = -1;

    printf("[UDP] Monitoring stopped.\n");
}

void execute_command(int sockfd, const char *command)
{
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    snprintf(
        request,
        sizeof(request),
        "EXEC %s\n",
        command
    );

    send_all(sockfd, request, strlen(request));

    if (receive_line(
            sockfd,
            response,
            sizeof(response)) < 0)
    {
        printf("Agent disconnected.\n");
        return;
    }

    printf("Agent: %s", response);

    if (strncmp(response, "ERR ", 4) == 0)
        return;

    receive_exec_output(sockfd);
}

void upload_file(int sockfd, const char *filename)
{
    FILE *fp;
    long file_size;
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];

    fp = fopen(filename, "rb");

    if (fp == NULL)
    {
        perror("fopen");
        return;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return;
    }

    file_size = ftell(fp);

    if (file_size < 0)
    {
        fclose(fp);
        return;
    }

    rewind(fp);

    snprintf(
        request,
        sizeof(request),
        "PUT %s %ld\n",
        filename,
        file_size
    );

    send_all(sockfd, request, strlen(request));

    long remaining = file_size;

    while (remaining > 0)
    {
        size_t chunk;

        if (remaining > BUFFER_SIZE)
            chunk = BUFFER_SIZE;
        else
            chunk = (size_t)remaining;

        size_t bytes_read = fread(buffer, 1, chunk, fp);

        if (bytes_read != chunk)
        {
            printf("File read error.\n");
            fclose(fp);
            return;
        }

        if (send_all(sockfd, buffer, bytes_read) < 0)
        {
            printf("File send failed.\n");
            fclose(fp);
            return;
        }

        remaining -= bytes_read;
    }

    fclose(fp);

    if (receive_line(
            sockfd,
            response,
            sizeof(response)) < 0)
    {
        printf("Agent response failed.\n");
        return;
    }

    printf("Agent: %s", response);
}

void download_file(int sockfd, const char *filename)
{
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];

    snprintf(
        request,
        sizeof(request),
        "GET %s\n",
        filename
    );

    send_all(sockfd, request, strlen(request));

    if (receive_line(
            sockfd,
            response,
            sizeof(response)) < 0)
    {
        printf("GET response failed.\n");
        return;
    }

    printf("Agent: %s", response);

    if (strncmp(response, "OK FILE_SEND", 12) != 0)
        return;

    long file_size;

    if (sscanf(
            response,
            "OK FILE_SEND %ld",
            &file_size) != 1)
    {
        printf("Invalid file size.\n");
        return;
    }

    FILE *fp = fopen(filename, "wb");

    if (fp == NULL)
    {
        perror("fopen");
        return;
    }

    long remaining = file_size;

    while (remaining > 0)
    {
        size_t chunk;

        if (remaining > BUFFER_SIZE)
            chunk = BUFFER_SIZE;
        else
            chunk = (size_t)remaining;

        ssize_t received = recv(
            sockfd,
            buffer,
            chunk,
            0
        );

        if (received <= 0)
        {
            printf("File receive failed.\n");
            fclose(fp);
            return;
        }

        fwrite(buffer, 1, received, fp);

        remaining -= received;
    }

    fclose(fp);

    printf(
        "GET completed successfully: %ld bytes received.\n",
        file_size
    );
}

int main(void)
{
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);

    if (inet_pton(
            AF_INET,
            SERVER_IP,
            &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sockfd);
        return 1;
    }

    if (connect(
            sockfd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        return 1;
    }

    printf("====================================\n");
    printf("       RemoteOps Controller\n");
    printf("====================================\n");
    printf("Server IP    : %s\n", SERVER_IP);
    printf("TCP Port     : %d\n", TCP_PORT);
    printf("UDP Port     : %d\n", UDP_PORT);
    printf("SID           : SID:2700\n");
    printf("====================================\n");

    send_all(
        sockfd,
        "AUTH OPS-0072\n",
        strlen("AUTH OPS-0072\n")
    );

    if (receive_line(
            sockfd,
            buffer,
            sizeof(buffer)) < 0)
    {
        printf("Authentication failed.\n");
        close(sockfd);
        return 1;
    }

    printf("Agent: %s", buffer);

    while (1)
    {
        char command[BUFFER_SIZE];

        printf("\nremoteops> ");
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL)
            break;

        command[strcspn(command, "\n")] = '\0';

        if (strcmp(command, "SYSINFO") == 0)
        {
            send_all(sockfd, "SYSINFO\n", 8);

            if (receive_line(
                    sockfd,
                    buffer,
                    sizeof(buffer)) < 0)
                break;

            printf("Agent: %s", buffer);
        }
        else if (strcmp(command, "LISTPROC") == 0)
        {
            send_all(sockfd, "LISTPROC\n", 9);

            if (receive_line(
                    sockfd,
                    buffer,
                    sizeof(buffer)) < 0)
                break;

            printf("Agent: %s", buffer);

            if (strncmp(buffer, "OK PROCS", 8) == 0)
                receive_process_list(sockfd);
        }
        else if (strncmp(command, "EXEC ", 5) == 0)
        {
            execute_command(sockfd, command + 5);
        }
        else if (strncmp(command, "PUT ", 4) == 0)
        {
            upload_file(sockfd, command + 4);
        }
        else if (strncmp(command, "GET ", 4) == 0)
        {
            download_file(sockfd, command + 4);
        }
        else if (strcmp(command, "MONITOR START") == 0)
        {
            char monitor_command[BUFFER_SIZE];

            if (start_udp_monitor() < 0)
                continue;

            snprintf(
                monitor_command,
                sizeof(monitor_command),
                "MONITOR START %d\n",
                UDP_PORT
            );

            send_all(
                sockfd,
                monitor_command,
                strlen(monitor_command)
            );

            if (receive_line(
                    sockfd,
                    buffer,
                    sizeof(buffer)) < 0)
            {
                printf("Agent response failed.\n");
                stop_udp_monitor();
                break;
            }

            printf("Agent: %s", buffer);

            if (strncmp(buffer, "OK MONITOR_STARTED", 19) != 0)
                stop_udp_monitor();
        }
        else if (strcmp(command, "MONITOR STOP") == 0)
        {
            send_all(
                sockfd,
                "MONITOR STOP\n",
                strlen("MONITOR STOP\n")
            );

            if (receive_line(
                    sockfd,
                    buffer,
                    sizeof(buffer)) < 0)
                break;

            printf("Agent: %s", buffer);

            stop_udp_monitor();
        }
        else if (strcmp(command, "QUIT") == 0)
        {
            send_all(
                sockfd,
                "QUIT\n",
                strlen("QUIT\n")
            );

            if (receive_line(
                    sockfd,
                    buffer,
                    sizeof(buffer)) >= 0)
            {
                printf("Agent: %s", buffer);
            }

            break;
        }
        else
        {
            printf("Commands:\n");
            printf("  SYSINFO\n");
            printf("  LISTPROC\n");
            printf("  EXEC DATE\n");
            printf("  EXEC UPTIME\n");
            printf("  EXEC DISKFREE\n");
            printf("  EXEC HOSTNAME\n");
            printf("  EXEC WHOAMI\n");
            printf("  PUT <filename>\n");
            printf("  GET <filename>\n");
            printf("  MONITOR START\n");
            printf("  MONITOR STOP\n");
            printf("  QUIT\n");
        }
    }

    if (monitor_running)
        stop_udp_monitor();

    close(sockfd);

    printf("Controller disconnected.\n");

    return 0;
}
