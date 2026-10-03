#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>

#define PORT 9410
#define BUFFER_SIZE 1024
#define BACKLOG 10

#define AUTH_TOKEN "OPS-0072"
#define SID "SID:2700"

#define STORAGE_PATH "/home/tasa/remoteops/agentfiles/IT24100072"


/* =========================================
   Send all bytes
   ========================================= */
int send_all(int socket_fd,
             const void *data,
             size_t length)
{
    size_t total = 0;

    while (total < length)
    {
        ssize_t sent = send(socket_fd,
                            (const char *)data + total,
                            length - total,
                            0);

        if (sent <= 0)
        {
            return -1;
        }

        total += sent;
    }

    return 0;
}


/* =========================================
   Send one response line
   ========================================= */
void send_response(int client_fd,
                   const char *response)
{
    send_all(client_fd,
             response,
             strlen(response));
}


/* =========================================
   Receive one complete line
   ========================================= */
int receive_line(int client_fd,
                 char *buffer,
                 int size)
{
    int index = 0;
    char ch;

    while (index < size - 1)
    {
        ssize_t received = recv(client_fd,
                                &ch,
                                1,
                                0);

        if (received <= 0)
        {
            return -1;
        }

        buffer[index++] = ch;

        if (ch == '\n')
        {
            break;
        }
    }

    buffer[index] = '\0';

    return index;
}


/* =========================================
   Receive exactly N bytes
   ========================================= */
int receive_exact(int client_fd,
                  void *buffer,
                  size_t length)
{
    size_t total = 0;

    while (total < length)
    {
        ssize_t received = recv(
            client_fd,
            (char *)buffer + total,
            length - total,
            0
        );

        if (received <= 0)
        {
            return -1;
        }

        total += received;
    }

    return 0;
}


/* =========================================
   SYSINFO
   ========================================= */
void get_sysinfo(int client_fd)
{
    FILE *fp;
    char line[256];

    double uptime = 0.0;
    double load = 0.0;

    long mem_total = 0;
    long mem_available = 0;


    fp = fopen("/proc/loadavg", "r");

    if (fp != NULL)
    {
        fscanf(fp, "%lf", &load);
        fclose(fp);
    }


    fp = fopen("/proc/uptime", "r");

    if (fp != NULL)
    {
        fscanf(fp, "%lf", &uptime);
        fclose(fp);
    }


    fp = fopen("/proc/meminfo", "r");

    if (fp != NULL)
    {
        while (fgets(line,
                     sizeof(line),
                     fp) != NULL)
        {
            if (sscanf(line,
                       "MemTotal: %ld kB",
                       &mem_total) == 1)
            {
                continue;
            }

            if (sscanf(line,
                       "MemAvailable: %ld kB",
                       &mem_available) == 1)
            {
                continue;
            }
        }

        fclose(fp);
    }


    long mem_used =
        mem_total - mem_available;


    char response[BUFFER_SIZE];

    snprintf(
        response,
        sizeof(response),
        "OK SYSINFO CPU:%.2f MEM:%ld/%ldKB UPTIME:%.0fs %s\n",
        load,
        mem_used,
        mem_total,
        uptime,
        SID
    );

    send_response(client_fd,
                  response);
}


/* =========================================
   LISTPROC
   ========================================= */
void get_process_list(int client_fd)
{
    FILE *fp;
    char line[256];


    fp = popen(
        "ps -eo pid,comm --no-headers",
        "r"
    );


    if (fp == NULL)
    {
        send_response(
            client_fd,
            "ERR 500 PROCESS_LIST_FAILED SID:2700\n"
        );

        return;
    }


    send_response(
        client_fd,
        "OK PROCS SID:2700\n"
    );


    while (fgets(line,
                 sizeof(line),
                 fp) != NULL)
    {
        send_all(
            client_fd,
            line,
            strlen(line)
        );
    }


    pclose(fp);


    send_response(
        client_fd,
        "END PROCS SID:2700\n"
    );
}


/* =========================================
   EXEC
   ========================================= */
void execute_command(int client_fd,
                     const char *command)
{
    FILE *fp;

    char line[512];
    char response[BUFFER_SIZE];

    const char *system_command = NULL;


    if (strcmp(command, "DATE") == 0)
    {
        system_command = "date";
    }

    else if (strcmp(command, "UPTIME") == 0)
    {
        system_command = "uptime";
    }

    else if (strcmp(command, "DISKFREE") == 0)
    {
        system_command = "df -h /";
    }

    else if (strcmp(command, "HOSTNAME") == 0)
    {
        system_command = "hostname";
    }

    else if (strcmp(command, "WHOAMI") == 0)
    {
        system_command = "whoami";
    }

    else
    {
        snprintf(
            response,
            sizeof(response),
            "ERR 002 COMMAND_NOT_ALLOWED %s\n",
            SID
        );

        send_response(
            client_fd,
            response
        );

        return;
    }


    fp = popen(system_command,
               "r");


    if (fp == NULL)
    {
        snprintf(
            response,
            sizeof(response),
            "ERR 500 EXEC_FAILED %s\n",
            SID
        );

        send_response(
            client_fd,
            response
        );

        return;
    }


    snprintf(
        response,
        sizeof(response),
        "OK EXEC_RESULT %s %s\n",
        command,
        SID
    );

    send_response(
        client_fd,
        response
    );


    while (fgets(line,
                 sizeof(line),
                 fp) != NULL)
    {
        send_all(
            client_fd,
            line,
            strlen(line)
        );
    }


    pclose(fp);


    send_response(
        client_fd,
        "END EXEC SID:2700\n"
    );
}


/* =========================================
   PUT
   Format:
   PUT <filename> <bytes>\n
   followed immediately by raw bytes
   ========================================= */
void handle_put(int client_fd,
                char *command)
{
    char filename[256];
    unsigned long long file_size;


    if (sscanf(command,
               "PUT %255s %llu",
               filename,
               &file_size) != 2)
    {
        send_response(
            client_fd,
            "ERR 400 INVALID_PUT SID:2700\n"
        );

        return;
    }


    /* Maximum file size: 10 MB */
    if (file_size > 10ULL * 1024ULL * 1024ULL)
    {
        send_response(
            client_fd,
            "ERR 004 FILE_TOO_LARGE SID:2700\n"
        );

        return;
    }


    /* Prevent path traversal */
    if (strstr(filename, "..") != NULL ||
        strchr(filename, '/') != NULL ||
        strchr(filename, '\\') != NULL)
    {
        send_response(
            client_fd,
            "ERR 400 INVALID_FILENAME SID:2700\n"
        );

        return;
    }


    char filepath[512];

    snprintf(
        filepath,
        sizeof(filepath),
        "%s/%s",
        STORAGE_PATH,
        filename
    );


    FILE *fp = fopen(filepath,
                     "wb");


    if (fp == NULL)
    {
        send_response(
            client_fd,
            "ERR 500 FILE_OPEN_FAILED SID:2700\n"
        );

        return;
    }


    char buffer[BUFFER_SIZE];

    unsigned long long remaining =
        file_size;


    while (remaining > 0)
    {
        size_t chunk_size;

        if (remaining > BUFFER_SIZE)
        {
            chunk_size = BUFFER_SIZE;
        }
        else
        {
            chunk_size = (size_t)remaining;
        }


        if (receive_exact(client_fd,
                          buffer,
                          chunk_size) < 0)
        {
            fclose(fp);

            remove(filepath);

            return;
        }


        if (fwrite(buffer,
                   1,
                   chunk_size,
                   fp) != chunk_size)
        {
            fclose(fp);

            remove(filepath);

            send_response(
                client_fd,
                "ERR 500 FILE_WRITE_FAILED SID:2700\n"
            );

            return;
        }


        remaining -= chunk_size;
    }


    fclose(fp);


    send_response(
        client_fd,
        "OK FILE_RECEIVED SID:2700\n"
    );


    printf(
        "PUT completed: %s (%llu bytes)\n",
        filename,
        file_size
    );
}


/* =========================================
   GET
   Format:
   GET <filename>\n
   Response:
   OK FILE_SEND <bytes> SID:2700\n
   followed immediately by raw bytes
   ========================================= */
void handle_get(int client_fd,
                char *command)
{
    char filename[256];


    if (sscanf(command,
               "GET %255s",
               filename) != 1)
    {
        send_response(
            client_fd,
            "ERR 400 INVALID_GET SID:2700\n"
        );

        return;
    }


    /* Prevent path traversal */
    if (strstr(filename, "..") != NULL ||
        strchr(filename, '/') != NULL ||
        strchr(filename, '\\') != NULL)
    {
        send_response(
            client_fd,
            "ERR 400 INVALID_FILENAME SID:2700\n"
        );

        return;
    }


    char filepath[512];

    snprintf(
        filepath,
        sizeof(filepath),
        "%s/%s",
        STORAGE_PATH,
        filename
    );


    FILE *fp = fopen(filepath,
                     "rb");


    if (fp == NULL)
    {
        send_response(
            client_fd,
            "ERR 005 FILE_NOT_FOUND SID:2700\n"
        );

        return;
    }


    /* Find exact file size */
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);

        send_response(
            client_fd,
            "ERR 500 FILE_READ_FAILED SID:2700\n"
        );

        return;
    }


    long file_size = ftell(fp);


    if (file_size < 0)
    {
        fclose(fp);

        send_response(
            client_fd,
            "ERR 500 FILE_READ_FAILED SID:2700\n"
        );

        return;
    }


    rewind(fp);


    char response[BUFFER_SIZE];


    snprintf(
        response,
        sizeof(response),
        "OK FILE_SEND %ld %s\n",
        file_size,
        SID
    );


    /* Send response header */
    if (send_all(client_fd,
                 response,
                 strlen(response)) < 0)
    {
        fclose(fp);
        return;
    }


    /* Send exact raw file bytes */
    char buffer[BUFFER_SIZE];

    size_t bytes_read;

    long total_sent = 0;


    while ((bytes_read = fread(buffer,
                               1,
                               sizeof(buffer),
                               fp)) > 0)
    {
        if (send_all(client_fd,
                     buffer,
                     bytes_read) < 0)
        {
            fclose(fp);
            return;
        }

        total_sent += bytes_read;
    }


    fclose(fp);


    printf(
        "GET completed: %s (%ld bytes)\n",
        filename,
        total_sent
    );
}


/* =========================================
   Handle one Controller
   ========================================= */
void *handle_client(void *arg)
{
    int client_fd = *(int *)arg;

    free(arg);


    printf(
        "Controller connected. Thread started.\n"
    );


    int authenticated = 0;

    char buffer[BUFFER_SIZE];


    while (1)
    {
        memset(buffer,
               0,
               sizeof(buffer));


        int result =
            receive_line(
                client_fd,
                buffer,
                sizeof(buffer)
            );


        if (result <= 0)
        {
            break;
        }


        printf(
            "Received: %s",
            buffer
        );


        /* =================================
           AUTH
           ================================= */
        if (strcmp(buffer,
                   "AUTH OPS-0072\n") == 0)
        {
            authenticated = 1;


            send_response(
                client_fd,
                "OK AUTHENTICATED SID:2700\n"
            );


            printf(
                "Authentication successful.\n"
            );
        }


        /* =================================
           SYSINFO
           ================================= */
        else if (strcmp(buffer,
                        "SYSINFO\n") == 0)
        {
            if (!authenticated)
            {
                send_response(
                    client_fd,
                    "ERR 401 UNAUTHENTICATED SID:2700\n"
                );
            }
            else
            {
                get_sysinfo(client_fd);

                printf(
                    "SYSINFO sent.\n"
                );
            }
        }


        /* =================================
           LISTPROC
           ================================= */
        else if (strcmp(buffer,
                        "LISTPROC\n") == 0)
        {
            if (!authenticated)
            {
                send_response(
                    client_fd,
                    "ERR 401 UNAUTHENTICATED SID:2700\n"
                );
            }
            else
            {
                get_process_list(client_fd);

                printf(
                    "LISTPROC sent.\n"
                );
            }
        }


        /* =================================
           EXEC
           ================================= */
        else if (strncmp(buffer,
                         "EXEC ",
                         5) == 0)
        {
            if (!authenticated)
            {
                send_response(
                    client_fd,
                    "ERR 401 UNAUTHENTICATED SID:2700\n"
                );
            }
            else
            {
                char command[100];


                strcpy(
                    command,
                    buffer + 5
                );


                command[
                    strcspn(command, "\n")
                ] = '\0';


                printf(
                    "EXEC command: %s\n",
                    command
                );


                execute_command(
                    client_fd,
                    command
                );
            }
        }


        /* =================================
           PUT
           ================================= */
        else if (strncmp(buffer,
                         "PUT ",
                         4) == 0)
        {
            if (!authenticated)
            {
                send_response(
                    client_fd,
                    "ERR 401 UNAUTHENTICATED SID:2700\n"
                );
            }
            else
            {
                handle_put(
                    client_fd,
                    buffer
                );
            }
        }


        /* =================================
           GET
           ================================= */
        else if (strncmp(buffer,
                         "GET ",
                         4) == 0)
        {
            if (!authenticated)
            {
                send_response(
                    client_fd,
                    "ERR 401 UNAUTHENTICATED SID:2700\n"
                );
            }
            else
            {
                handle_get(
                    client_fd,
                    buffer
                );
            }
        }


        /* =================================
           UNKNOWN COMMAND
           ================================= */
        else
        {
            send_response(
                client_fd,
                "ERR 400 UNKNOWN_COMMAND SID:2700\n"
            );


            printf(
                "Unknown command.\n"
            );
        }
    }


    close(client_fd);


    printf(
        "Controller disconnected. "
        "Thread finished.\n"
    );


    return NULL;
}


/* =========================================
   MAIN
   ========================================= */
int main()
{
    int server_fd;

    struct sockaddr_in server_addr;


    /* Create storage directory */
    if (mkdir(STORAGE_PATH, 0755) < 0 &&
        errno != EEXIST)
    {
        perror("mkdir");
        exit(EXIT_FAILURE);
    }


    /* Create TCP socket */
    server_fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );


    if (server_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }


    int opt = 1;


    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)) < 0)
    {
        perror("setsockopt");

        close(server_fd);

        exit(EXIT_FAILURE);
    }


    memset(
        &server_addr,
        0,
        sizeof(server_addr)
    );


    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(PORT);


    if (bind(
            server_fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) < 0)
    {
        perror("bind");

        close(server_fd);

        exit(EXIT_FAILURE);
    }


    if (listen(
            server_fd,
            BACKLOG) < 0)
    {
        perror("listen");

        close(server_fd);

        exit(EXIT_FAILURE);
    }


    printf(
        "====================================\n"
    );

    printf(
        "       RemoteOps Agent\n"
    );

    printf(
        "====================================\n"
    );

    printf(
        "Registration : IT24100072\n"
    );

    printf(
        "Port         : %d\n",
        PORT
    );

    printf(
        "SID          : %s\n",
        SID
    );

    printf(
        "Concurrency  : pthread per Controller\n"
    );

    printf(
        "Storage      : %s\n",
        STORAGE_PATH
    );

    printf(
        "Status       : Listening...\n"
    );

    printf(
        "====================================\n"
    );


    while (1)
    {
        struct sockaddr_in client_addr;

        socklen_t client_len =
            sizeof(client_addr);


        int client_fd =
            accept(
                server_fd,
                (struct sockaddr *)&client_addr,
                &client_len
            );


        if (client_fd < 0)
        {
            perror("accept");

            continue;
        }


        int *client_socket =
            malloc(sizeof(int));


        if (client_socket == NULL)
        {
            perror("malloc");

            close(client_fd);

            continue;
        }


        *client_socket =
            client_fd;


        pthread_t thread_id;


        int result =
            pthread_create(
                &thread_id,
                NULL,
                handle_client,
                client_socket
            );


        if (result != 0)
        {
            fprintf(
                stderr,
                "pthread_create failed\n"
            );

            close(client_fd);

            free(client_socket);

            continue;
        }


        pthread_detach(thread_id);


        printf(
            "New Controller thread created.\n"
        );
    }


    close(server_fd);

    return 0;
}
