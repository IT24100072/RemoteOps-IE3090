#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

#define TCP_PORT 9410
#define BUFFER_SIZE 4096
#define SID "2700"
#define AUTH_TOKEN "OPS-0072"
#define STORAGE_DIR "/home/tasa/remoteops/agentfiles/IT24100072"
#define MONITOR_INTERVAL 5

typedef struct {
    int udp_socket;
    struct sockaddr_in destination;
    volatile int running;
} monitor_context_t;

#define LOG_FILE "/home/tasa/remoteops/remoteops_072.log"
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void write_log(const char *event)
{
    FILE *fp;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];

    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    pthread_mutex_lock(&log_mutex);
    fp = fopen(LOG_FILE, "a");
    if (fp != NULL) {
        fprintf(fp, "[%s] %s SID:%s\n", timestamp, event, SID);
        fclose(fp);
    }
    pthread_mutex_unlock(&log_mutex);
}

/* ---------- Utility Functions ---------- */

void send_response(int client_fd, const char *message)
{
    send(client_fd, message, strlen(message), 0);
}

int receive_line(int fd, char *buffer, size_t size)
{
    size_t i = 0;
    char c;

    while (i < size - 1) {
        ssize_t n = recv(fd, &c, 1, 0);

        if (n <= 0) {
            return 0;
        }

        if (c == '\n') {
            break;
        }

        if (c != '\r') {
            buffer[i++] = c;
        }
    }

    buffer[i] = '\0';
    return 1;
}

int send_all(int fd, const void *buffer, size_t length)
{
    size_t total = 0;
    const char *ptr = buffer;

    while (total < length) {
        ssize_t n = send(fd, ptr + total, length - total, 0);

        if (n <= 0) {
            return -1;
        }

        total += n;
    }

    return 0;
}

int receive_all(int fd, void *buffer, size_t length)
{
    size_t total = 0;
    char *ptr = buffer;

    while (total < length) {
        ssize_t n = recv(fd, ptr + total, length - total, 0);

        if (n <= 0) {
            return -1;
        }

        total += n;
    }

    return 0;
}

/* ---------- System Information ---------- */

void get_sysinfo(char *output, size_t size)
{
    FILE *fp;
    double load = 0.0;
    double uptime = 0.0;
    long mem_total = 0;
    long mem_available = 0;
    char line[256];

    /* CPU load */
    fp = fopen("/proc/loadavg", "r");

    if (fp != NULL) {
        fscanf(fp, "%lf", &load);
        fclose(fp);
    }

    /* Uptime */
    fp = fopen("/proc/uptime", "r");

    if (fp != NULL) {
        fscanf(fp, "%lf", &uptime);
        fclose(fp);
    }

    /* Memory */
    fp = fopen("/proc/meminfo", "r");

    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            if (sscanf(line, "MemTotal: %ld kB", &mem_total) == 1) {
                continue;
            }

            if (sscanf(line, "MemAvailable: %ld kB", &mem_available) == 1) {
                continue;
            }
        }

        fclose(fp);
    }

    snprintf(
        output,
        size,
        "CPU:%.2f MEM_TOTAL:%ldKB MEM_AVAILABLE:%ldKB UPTIME:%.0fs SID:%s\n",
        load,
        mem_total,
        mem_available,
        uptime,
        SID
    );
}

/* ---------- LISTPROC ---------- */

void handle_listproc(int client_fd)
{
    FILE *fp;
    char line[512];

    fp = popen("ps -eo pid,comm,user --sort=pid", "r");

    if (fp == NULL) {
        send_response(
            client_fd,
            "ERR 500 PROCESS_LIST_FAILED SID:2700\n"
        );
        return;
    }

    send_response(
        client_fd,
        "OK PROCESS_LIST SID:2700\n"
    );

    while (fgets(line, sizeof(line), fp)) {
        send_all(client_fd, line, strlen(line));
    }

    pclose(fp);

    send_response(
        client_fd,
        "END_PROCESS_LIST SID:2700\n"
    );
}

/* ---------- EXEC ---------- */

void handle_exec(int client_fd, const char *input)
{
    const char *command = input + 5;
    char response[BUFFER_SIZE];
    FILE *fp;

    while (*command == ' ') {
        command++;
    }

    if (
        strcmp(command, "DATE") != 0 &&
        strcmp(command, "UPTIME") != 0 &&
        strcmp(command, "DISKFREE") != 0 &&
        strcmp(command, "HOSTNAME") != 0 &&
        strcmp(command, "WHOAMI") != 0
    ) {
        snprintf(
            response,
            sizeof(response),
            "ERR 002 COMMAND_NOT_ALLOWED SID:%s\n",
            SID
        );

        send_response(client_fd, response);
        return;
    }

    if (strcmp(command, "DATE") == 0) {
        fp = popen("date", "r");
    }
    else if (strcmp(command, "UPTIME") == 0) {
        fp = popen("uptime", "r");
    }
    else if (strcmp(command, "DISKFREE") == 0) {
        fp = popen("df -h /", "r");
    }
    else if (strcmp(command, "HOSTNAME") == 0) {
        fp = popen("hostname", "r");
    }
    else {
        fp = popen("whoami", "r");
    }

    if (fp == NULL) {
        snprintf(
            response,
            sizeof(response),
            "ERR 500 EXEC_FAILED SID:%s\n",
            SID
        );

        send_response(client_fd, response);
        return;
    }

    send_response(
        client_fd,
        "OK EXEC_RESULT SID:2700\n"
    );

    while (fgets(response, sizeof(response), fp)) {
        send_all(client_fd, response, strlen(response));
    }

    pclose(fp);

    send_response(
        client_fd,
        "END_EXEC SID:2700\n"
    );
}

/* ---------- PUT ---------- */

void handle_put(int client_fd, const char *input)
{
    char filename[256];
    long file_size;

    if (sscanf(input, "PUT %255s %ld", filename, &file_size) != 2) {
        send_response(
            client_fd,
            "ERR 400 INVALID_PUT SID:2700\n"
        );
        return;
    }

    if (file_size < 0) {
        send_response(
            client_fd,
            "ERR 400 INVALID_FILE_SIZE SID:2700\n"
        );
        return;
    }

    char path[512];

    snprintf(
        path,
        sizeof(path),
        "%s/%s",
        STORAGE_DIR,
        filename
    );

    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        send_response(
            client_fd,
            "ERR 500 FILE_OPEN_FAILED SID:2700\n"
        );
        return;
    }

    send_response(
        client_fd,
        "OK READY_FOR_DATA SID:2700\n"
    );

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    char buffer[BUFFER_SIZE];
    long remaining = file_size;

    while (remaining > 0) {
        size_t chunk =
            remaining > BUFFER_SIZE ?
            BUFFER_SIZE :
            (size_t)remaining;

        if (receive_all(client_fd, buffer, chunk) != 0) {
            fclose(fp);
            return;
        }

        fwrite(buffer, 1, chunk, fp);
        remaining -= chunk;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
    double throughput = elapsed > 0.0 ? (double)file_size / elapsed : 0.0;
    char transfer_log[BUFFER_SIZE];
    snprintf(transfer_log, sizeof(transfer_log), "FILE_TRANSFER PUT BYTES:%ld RATE:%.2f_Bps", file_size, throughput);
    write_log(transfer_log);
    fclose(fp);

    send_response(
        client_fd,
        "OK FILE_RECEIVED SID:2700\n"
    );
}

/* ---------- GET ---------- */

void handle_get(int client_fd, const char *input)
{
    char filename[256];

    if (sscanf(input, "GET %255s", filename) != 1) {
        send_response(
            client_fd,
            "ERR 400 INVALID_GET SID:2700\n"
        );
        return;
    }

    char path[512];

    snprintf(
        path,
        sizeof(path),
        "%s/%s",
        STORAGE_DIR,
        filename
    );

    FILE *fp = fopen(path, "rb");

    if (fp == NULL) {
        send_response(
            client_fd,
            "ERR 404 FILE_NOT_FOUND SID:2700\n"
        );
        return;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    char response[BUFFER_SIZE];

    snprintf(
        response,
        sizeof(response),
        "OK FILE_SEND %ld SID:%s\n",
        file_size,
        SID
    );

    send_response(client_fd, response);

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    char buffer[BUFFER_SIZE];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (send_all(client_fd, buffer, bytes_read) != 0) {
            fclose(fp);
            return;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
    double throughput = elapsed > 0.0 ? (double)file_size / elapsed : 0.0;
    char transfer_log[BUFFER_SIZE];
    snprintf(transfer_log, sizeof(transfer_log), "FILE_TRANSFER GET BYTES:%ld RATE:%.2f_Bps", file_size, throughput);
    write_log(transfer_log);

    fclose(fp);
}

/* ---------- UDP MONITOR ---------- */

void *monitor_sender(void *arg)
{
    monitor_context_t *ctx = (monitor_context_t *)arg;

    char stats[BUFFER_SIZE];

    while (ctx->running) {

        get_sysinfo(stats, sizeof(stats));

        sendto(
            ctx->udp_socket,
            stats,
            strlen(stats),
            0,
            (struct sockaddr *)&ctx->destination,
            sizeof(ctx->destination)
        );

        for (int i = 0; i < MONITOR_INTERVAL; i++) {

            if (!ctx->running) {
                break;
            }

            sleep(1);
        }
    }

    return NULL;
}

int start_monitor(
    int client_fd,
    const char *command,
    monitor_context_t **monitor,
    pthread_t *monitor_thread
)
{
    int udp_port;

    if (sscanf(command, "MONITOR START %d", &udp_port) != 1) {
        send_response(
            client_fd,
            "ERR 400 INVALID_MONITOR_PORT SID:2700\n"
        );
        return -1;
    }

    if (udp_port < 1 || udp_port > 65535) {
        send_response(
            client_fd,
            "ERR 400 INVALID_MONITOR_PORT SID:2700\n"
        );
        return -1;
    }

    if (*monitor != NULL && (*monitor)->running) {
        send_response(
            client_fd,
            "ERR 409 MONITOR_ALREADY_RUNNING SID:2700\n"
        );
        return -1;
    }

    struct sockaddr_in peer_address;
    socklen_t peer_length = sizeof(peer_address);

    if (
        getpeername(
            client_fd,
            (struct sockaddr *)&peer_address,
            &peer_length
        ) < 0
    ) {
        send_response(
            client_fd,
            "ERR 500 PEER_ADDRESS_FAILED SID:2700\n"
        );
        return -1;
    }

    int udp_socket = socket(
        AF_INET,
        SOCK_DGRAM,
        0
    );

    if (udp_socket < 0) {
        send_response(
            client_fd,
            "ERR 500 UDP_SOCKET_FAILED SID:2700\n"
        );
        return -1;
    }

    monitor_context_t *ctx =
        malloc(sizeof(monitor_context_t));

    if (ctx == NULL) {
        close(udp_socket);

        send_response(
            client_fd,
            "ERR 500 MEMORY_FAILED SID:2700\n"
        );

        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));

    ctx->udp_socket = udp_socket;
    ctx->running = 1;

    ctx->destination.sin_family = AF_INET;
    ctx->destination.sin_port = htons(udp_port);
    ctx->destination.sin_addr = peer_address.sin_addr;

    *monitor = ctx;

    if (
        pthread_create(
            monitor_thread,
            NULL,
            monitor_sender,
            ctx
        ) != 0
    ) {
        close(udp_socket);
        free(ctx);
        *monitor = NULL;

        send_response(
            client_fd,
            "ERR 500 MONITOR_THREAD_FAILED SID:2700\n"
        );

        return -1;
    }

    send_response(
        client_fd,
        "OK MONITOR_STARTED SID:2700\n"
    );

    return 0;
}

void stop_monitor(
    int client_fd,
    monitor_context_t **monitor,
    pthread_t *monitor_thread
)
{
    if (*monitor == NULL) {
        send_response(
            client_fd,
            "ERR 409 MONITOR_NOT_RUNNING SID:2700\n"
        );
        return;
    }

    (*monitor)->running = 0;

    pthread_join(*monitor_thread, NULL);

    close((*monitor)->udp_socket);

    free(*monitor);

    *monitor = NULL;

    send_response(
        client_fd,
        "OK MONITOR_STOPPED SID:2700\n"
    );
}

void cleanup_monitor(
    monitor_context_t **monitor,
    pthread_t *monitor_thread
)
{
    if (*monitor == NULL) {
        return;
    }

    (*monitor)->running = 0;

    pthread_join(*monitor_thread, NULL);

    close((*monitor)->udp_socket);

    free(*monitor);

    *monitor = NULL;
}

/* ---------- Client Thread ---------- */

void *handle_client(void *arg)
{
    int client_fd = *(int *)arg;

    free(arg);

    printf(
        "Controller connected. Thread started.\n"
    );

    write_log("CONTROLLER_CONNECTED");
    int authenticated = 0;

    monitor_context_t *monitor = NULL;
    pthread_t monitor_thread;

    char buffer[BUFFER_SIZE];

    while (1) {

        if (!receive_line(
                client_fd,
                buffer,
                sizeof(buffer)
            )) {
            break;
        }

        printf(
            "Command received: %s\n",
            buffer
        );

        { char log_event[BUFFER_SIZE]; snprintf(log_event, sizeof(log_event), "COMMAND %s", buffer); write_log(log_event); }
        /* AUTH */
        if (strncmp(buffer, "AUTH ", 5) == 0) {

            char token[256];

            if (sscanf(
                    buffer + 5,
                    "%255s",
                    token
                ) == 1 &&
                strcmp(token, AUTH_TOKEN) == 0) {

                authenticated = 1;

                send_response(
                    client_fd,
                    "OK AUTHENTICATED SID:2700\n"
                );
            }
            else {
                send_response(
                    client_fd,
                    "ERR 001 AUTH_FAILED SID:2700\n"
                );
            }

            continue;
        }

        /* AUTH required */
        if (!authenticated) {

            send_response(
                client_fd,
                "ERR 401 UNAUTHENTICATED SID:2700\n"
            );

            continue;
        }

        /* SYSINFO */
        if (strcmp(buffer, "SYSINFO") == 0) {

            char stats[BUFFER_SIZE];

            get_sysinfo(
                stats,
                sizeof(stats)
            );

            char response[BUFFER_SIZE];

            snprintf(
                response,
                sizeof(response),
                "OK SYSINFO SID:2700 %s",
                stats
            );

            send_response(
                client_fd,
                response
            );
        }

        /* LISTPROC */
        else if (strcmp(buffer, "LISTPROC") == 0) {

            handle_listproc(client_fd);
        }

        /* EXEC */
        else if (strncmp(buffer, "EXEC ", 5) == 0) {

            handle_exec(
                client_fd,
                buffer
            );
        }

        /* PUT */
        else if (strncmp(buffer, "PUT ", 4) == 0) {

            write_log("FILE_TRANSFER PUT");
            handle_put(
                client_fd,
                buffer
            );
        }

        /* GET */
        else if (strncmp(buffer, "GET ", 4) == 0) {

            write_log("FILE_TRANSFER GET");
            handle_get(
                client_fd,
                buffer
            );
        }

        /* MONITOR START */
        else if (strncmp(
                     buffer,
                     "MONITOR START ",
                     14
                 ) == 0) {

            start_monitor(
                client_fd,
                buffer,
                &monitor,
                &monitor_thread
            );
        }

        /* MONITOR STOP */
        else if (
            strcmp(buffer, "MONITOR STOP") == 0
        ) {

            stop_monitor(
                client_fd,
                &monitor,
                &monitor_thread
            );
        }

        /* QUIT */
        else if (
            strcmp(buffer, "QUIT") == 0
        ) {

            cleanup_monitor(
                &monitor,
                &monitor_thread
            );

            send_response(
                client_fd,
                "OK BYE SID:2700\n"
            );

            break;
        }

        /* Unknown */
        else {

            send_response(
                client_fd,
                "ERR 400 UNKNOWN_COMMAND SID:2700\n"
            );
        }
    }

    cleanup_monitor(
        &monitor,
        &monitor_thread
    );

    close(client_fd);

    printf(
        "Controller disconnected. Thread ended.\n"
    );

    return NULL;
}

/* ---------- Main ---------- */

int main(void)
{
    mkdir(STORAGE_DIR, 0755);

    int server_fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int option = 1;

    setsockopt(
        server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &option,
        sizeof(option)
    );

    struct sockaddr_in server_address;

    memset(
        &server_address,
        0,
        sizeof(server_address)
    );

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(TCP_PORT);

    if (
        bind(
            server_fd,
            (struct sockaddr *)&server_address,
            sizeof(server_address)
        ) < 0
    ) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("\n");
    printf("====================================\n");
    printf("       RemoteOps Agent\n");
    printf("====================================\n");
    printf("Registration : IT24100072\n");
    printf("Port         : 9410\n");
    printf("SID          : SID:2700\n");
    printf("Concurrency  : pthread per Controller\n");
    printf("Storage      : %s\n", STORAGE_DIR);
    printf("Monitor      : UDP system statistics\n");
    printf("Status       : Listening...\n");
    printf("====================================\n");

    while (1) {

        struct sockaddr_in client_address;
        socklen_t client_length =
            sizeof(client_address);

        int client_fd = accept(
            server_fd,
            (struct sockaddr *)&client_address,
            &client_length
        );

        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        printf(
            "New Controller connection accepted.\n"
        );

        int *client_ptr =
            malloc(sizeof(int));

        if (client_ptr == NULL) {
            close(client_fd);
            continue;
        }

        *client_ptr = client_fd;

        pthread_t thread;

        if (
            pthread_create(
                &thread,
                NULL,
                handle_client,
                client_ptr
            ) != 0
        ) {
            perror("pthread_create");
            close(client_fd);
            free(client_ptr);
            continue;
        }

        pthread_detach(thread);
    }

    close(server_fd);

    return 0;
}
