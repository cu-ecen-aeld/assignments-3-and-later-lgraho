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
#include <pthread.h>
#include <stdbool.h>
#include <sys/queue.h>

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"

struct thread_data {
    int read_fd;
    int write_fd;
    int client_socket;
    struct sockaddr_in client_addr;
    pthread_mutex_t *file_mutex;
    bool completed;
};

struct thread_entry {
    struct thread_data *data;
    pthread_t thread_id;
    SLIST_ENTRY(thread_entry) entries;
};

SLIST_HEAD(thread_list_head, thread_entry);

int server_socket = -1;
int client_socket = -1;
int write_fd = -1;
int read_fd = -1;
bool run_as_daemon = false;
volatile bool exit_requested = false; // TODO: replace with semaphore?
struct thread_list_head tl_head = SLIST_HEAD_INITIALIZER(tl_head);

// Check for completed threads and clean them up
void cleanup_completed_threads()
{
    struct thread_entry *te_iter = SLIST_FIRST(&tl_head);

    while(te_iter != NULL)
    {
        // Get next entry before potential removal
        struct thread_entry *te_next = SLIST_NEXT(te_iter, entries);
        struct thread_data *td_ptr = te_iter->data;

        // Check if thread completed
        if(td_ptr->completed)
        {
            // Join thread
            pthread_join(te_iter->thread_id, NULL);
            free(td_ptr);

            // Remove from list and free entry
            SLIST_REMOVE(&tl_head, te_iter, thread_entry, entries);
            free(te_iter);
        }
        te_iter = te_next;
    }
}

void cleanup_and_exit(int signum) {
    syslog(LOG_INFO, "Caught signal, exiting");
    // Signal exit to threads
    exit_requested = true;
    // Wait for all threads to complete
    do {
        cleanup_completed_threads();
        usleep(100000); // Sleep for 100ms
    } while (!SLIST_EMPTY(&tl_head));
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

int write_to_file(int fd, char *buffer, size_t size)
{
    ssize_t bytes_written = write(fd, buffer, size);
    if (bytes_written != size) {
        syslog(LOG_ERR, "File write failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int reset_file_pointer(int fd)
{
    if (lseek(fd, 0, SEEK_SET) == -1) {
        syslog(LOG_ERR, "File seek failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int send_file_to_client(int fd, int client_socket)
{
    char buffer[1024];
    ssize_t bytes_read;

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        if (send(client_socket, buffer, bytes_read, 0) == -1) {
            syslog(LOG_ERR, "Socket send failed: %s", strerror(errno));
            return -1;
        }
    }

    if (bytes_read == -1) {
        syslog(LOG_ERR, "File read failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

int handle_received_data(struct thread_data *td, char *buffer, ssize_t bytes_received)
{
    char *newline = memchr(buffer, '\n', bytes_received);

    if (newline) {
        size_t packet_size = newline - buffer + 1;
        if (write_to_file(td->write_fd, buffer, packet_size) == -1) { return -1; }
        if (reset_file_pointer(td->read_fd) == -1) { return -1; }
        if (send_file_to_client(td->read_fd, td->client_socket) == -1) { return -1; }
    } else {
        if (write_to_file(td->write_fd, buffer, bytes_received) == -1) { return -1; }
    }

    return 0;
}

void* connection_handler(void *arg)
{
    struct thread_data *td = (struct thread_data *)arg;
    ssize_t bytes_received = 0;
    char buffer[1024];

    while (!exit_requested && (bytes_received = recv(td->client_socket, buffer, sizeof(buffer), 0)) > 0) {
        pthread_mutex_lock(td->file_mutex);

        if (handle_received_data(td, buffer, bytes_received) == -1) {
            pthread_mutex_unlock(td->file_mutex);
            break;
        }

        pthread_mutex_unlock(td->file_mutex);
    }

    if (bytes_received == -1) {
        syslog(LOG_ERR, "Socket receive failed: %s", strerror(errno));
    }

    close(td->client_socket);
    td->client_socket = -1;
    td->completed = true;
    return td;
}

void* timestamp_handler(void *arg)
{
    // Thread periodically appending timestamp to the output file
    // TODO
    return NULL;
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int yes = 1;
    pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        run_as_daemon = true;
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

    // Open file for appending
    write_fd = open(FILE_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (write_fd == -1) {
        syslog(LOG_ERR, "File open failed: %s", strerror(errno));
        close(server_socket);
        return -1;
    }

    // Open file for reading
    read_fd = open(FILE_PATH, O_RDONLY);
    if (read_fd == -1) {
        syslog(LOG_ERR, "File open failed: %s", strerror(errno));
        close(write_fd);
        close(server_socket);
        return -1;
    }

    // Init mutex
    pthread_mutex_init(&file_mutex, NULL);

    // Initialize thread list
    SLIST_INIT(&tl_head);

    while (1) {
        // Accept connection
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            syslog(LOG_ERR, "Socket accept failed: %s", strerror(errno));
            continue;
        }

        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

        // Allocate thread data
        struct thread_data *td_ptr = NULL;
        td_ptr = (struct thread_data*)malloc(sizeof(struct thread_data));
        if(NULL != td_ptr)
        {
            td_ptr->read_fd = read_fd;
            td_ptr->write_fd = write_fd;
            td_ptr->client_socket = client_socket;
            td_ptr->client_addr = client_addr;
            td_ptr->file_mutex = &file_mutex;
            td_ptr->completed = false;
        }
        else
        {
            syslog(LOG_ERR, "Memory allocation failed: %s", strerror(errno));
            close(client_socket);
            continue;
        }

        // Create new thread
        pthread_t new_thread;
        int rc = pthread_create(&new_thread, NULL, connection_handler, (void*)td_ptr);
        if(rc)
        {
            syslog(LOG_ERR, "Thread creation failed: %s", strerror(errno));
            free(td_ptr);
            close(client_socket);
            continue;
        }

        // Add new thread to list
        struct thread_entry *new_entry;
        new_entry = (struct thread_entry*)malloc(sizeof(struct thread_entry));
        if (new_entry == NULL) {
            syslog(LOG_ERR, "Memory allocation for thread entry failed: %s", strerror(errno));
            free(td_ptr);
            close(client_socket);
            continue;
        }
        new_entry->data = td_ptr;
        new_entry->thread_id = new_thread;
        SLIST_INSERT_HEAD(&tl_head, new_entry, entries);

        // Note: Since accept() is blocking, completed threads will only be freed after a new connection is accepted
        cleanup_completed_threads();
    }

    cleanup_and_exit(0);
    return 0;
}
