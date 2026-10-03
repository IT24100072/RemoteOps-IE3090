#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9410
#define BUFFER_SIZE 1024


/* Receive one line from Agent */
int receive_line(int sock_fd, char *buffer, int size)
{
    int i = 0;
    char ch;

    while (i < size - 1)
    {
        int n = recv(sock_fd, &ch, 1, 0);

        if (n <= 0)
        {
            return -1;
        }

        buffer[i++] = ch;

        if (ch == '\n')
        {
            break;
        }
    }

    buffer[i] = '\0';

    return i;
}


/* Receive one normal response line */
void receive_response(int sock_fd)
{
    char buffer[BUFFER_SIZE];

    memset(buffer, 0, sizeof(buffer));

    if (receive_line(sock_fd,
                     buffer,
                     sizeof(buffer)) > 0)
    {
        printf("Agent response: %s", buffer);
    }
}


/* Receive EXEC output */
void receive_exec_output(int sock_fd)
{
    char buffer[BUFFER_SIZE];

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        if (receive_line(sock_fd,
                         buffer,
                         sizeof(buffer)) <= 0)
        {
            break;
        }

        if (strcmp(buffer,
                   "END EXEC SID:2700\n") == 0)
        {
            break;
        }

        printf("%s", buffer);
    }
}


/* Receive LISTPROC output */
void receive_process_list(int sock_fd)
{
    char buffer[BUFFER_SIZE];

    printf("\n========== PROCESS LIST ==========\n");

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        if (receive_line(sock_fd,
                         buffer,
                         sizeof(buffer)) <= 0)
        {
            break;
        }

        if (strcmp(buffer,
                   "END LISTPROC SID:2700\n") == 0)
        {
            break;
        }

        printf("%s", buffer);
    }

    printf("======== END PROCESS LIST ========\n");
}


/* Send EXEC command and receive result */
void run_exec(int sock_fd, const char *command)
{
    char message[BUFFER_SIZE];

    snprintf(message,
             sizeof(message),
             "EXEC %s\n",
             command);

    send(sock_fd,
         message,
         strlen(message),
         0);

    printf("\nSent: %s", message);

    receive_response(sock_fd);

    receive_exec_output(sock_fd);
}


int main()
{
    int sock_fd;

    struct sockaddr_in server_addr;


    /* Create TCP socket */
    sock_fd = socket(AF_INET,
                     SOCK_STREAM,
                     0);

    if (sock_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }


    /* Clear server address */
    memset(&server_addr,
           0,
           sizeof(server_addr));


    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);


    /* Convert IP address */
    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }


    /* Connect to Agent */
    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }


    printf("====================================\n");
    printf("       RemoteOps Controller\n");
    printf("====================================\n");
    printf("Server : %s:%d\n",
           SERVER_IP,
           PORT);
    printf("====================================\n");


    /* =========================
       AUTH
       ========================= */

    const char *auth = "AUTH OPS-0072\n";

    send(sock_fd,
         auth,
         strlen(auth),
         0);

    printf("Sent: AUTH OPS-0072\n");

    receive_response(sock_fd);


    /* =========================
       SYSINFO
       ========================= */

    const char *sysinfo = "SYSINFO\n";

    send(sock_fd,
         sysinfo,
         strlen(sysinfo),
         0);

    printf("Sent: SYSINFO\n");

    receive_response(sock_fd);


    /* =========================
       LISTPROC
       ========================= */

    const char *listproc = "LISTPROC\n";

    send(sock_fd,
         listproc,
         strlen(listproc),
         0);

    printf("Sent: LISTPROC\n");

    receive_process_list(sock_fd);


    /* =========================
       EXEC DATE
       ========================= */

    run_exec(sock_fd, "DATE");


    /* =========================
       EXEC UPTIME
       ========================= */

    run_exec(sock_fd, "UPTIME");


    /* =========================
       EXEC DISKFREE
       ========================= */

    run_exec(sock_fd, "DISKFREE");


    /* =========================
       EXEC HOSTNAME
       ========================= */

    run_exec(sock_fd, "HOSTNAME");


    /* =========================
       EXEC WHOAMI
       ========================= */

    run_exec(sock_fd, "WHOAMI");


    /* Close connection */
    close(sock_fd);

    printf("\nDisconnected from Agent.\n");

    return 0;
}
