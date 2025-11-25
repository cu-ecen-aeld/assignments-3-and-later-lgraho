#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"

int server_socket = -1;
int client_socket = -1;
int write_fd = -1;
int read_fd = -1;
int run_as_daemon = 0;

void cleanup_and_exit(int signum) {
    syslog(LOG_INFO, "Caught signal, exiting");
    if (client_socket != -1) close(client_socket);
    if (server_socket != -1) close(server_socket);
    if (write_fd != -1) close(write_fd);
    if (read_fd != -1) close(read_fd);
    remove(FILE_PATH);
    closelog();
    exit(0);
}

void setup_signal_handlers() {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = cleanup_and_exit;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS); // Parent exits
    }

    // Child process continues
    if (setsid() == -1) {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (chdir("/") == -1) {
        syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[1024];
    ssize_t bytes_received, bytes_written;
    int yes = 1;

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        run_as_daemon = 1;
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    setup_signal_handlers();

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        return -1;
    }

    if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
    {
        syslog(LOG_ERR, "setsockopt: %s", strerror(errno));
        return -1;
    }

    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Socket bind failed: %s", strerror(errno));
        close(server_socket);
        return -1;
    }

    if (run_as_daemon) {
        daemonize();
    }

    // Listen for connections
    if (listen(server_socket, 10) == -1) {
        syslog(LOG_ERR, "Socket listen failed: %s", strerror(errno));
        close(server_socket);
        return -1;
    }

    while (1) {
        // Accept connection
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            syslog(LOG_ERR, "Socket accept failed: %s", strerror(errno));
            continue;
        }

        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

        // Open file for appending
        write_fd = open(FILE_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (write_fd == -1) {
            syslog(LOG_ERR, "File open failed: %s", strerror(errno));
            close(client_socket);
            continue;
        }

        // Open file for reading
        read_fd = open(FILE_PATH, O_RDONLY);
        if (read_fd == -1) {
            syslog(LOG_ERR, "File open failed: %s", strerror(errno));
            close(write_fd);
            close(client_socket);
            continue;
        }

        // Receive data
        while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
            char *newline = memchr(buffer, '\n', bytes_received);
            if (newline) {
                size_t packet_size = newline - buffer + 1;
                bytes_written = write(write_fd, buffer, packet_size);
                if (bytes_written != packet_size) {
                    syslog(LOG_ERR, "File write failed: %s", strerror(errno));
                    break;
                }

                // Send file content back to client
                while ((bytes_written = read(read_fd, buffer, sizeof(buffer))) > 0) {
                    if (send(client_socket, buffer, bytes_written, 0) == -1) {
                        syslog(LOG_ERR, "Socket send failed: %s", strerror(errno));
                        break;
                    }
                }
                if (bytes_written == -1) {
                    syslog(LOG_ERR, "File read failed: %s", strerror(errno));
                }
                break;
            } else {
                bytes_written = write(write_fd, buffer, bytes_received);
                if (bytes_written != bytes_received) {
                    syslog(LOG_ERR, "File write failed: %s", strerror(errno));
                    break;
                }
            }
        }

        if (bytes_received == -1) {
            syslog(LOG_ERR, "Socket receive failed: %s", strerror(errno));
        }

        close(write_fd);
        write_fd = -1;

        close(read_fd);
        read_fd = -1;

        syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
        close(client_socket);
        client_socket = -1;
    }

    cleanup_and_exit(0);
    return 0;
}
