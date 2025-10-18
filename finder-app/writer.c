#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>

int main(int argc, char **argv)
{
    openlog(NULL, 0, LOG_USER);

    if (argc != 3)
    {
        syslog(LOG_ERR, "Invalid number of arguments: %d", argc);
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);

    int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        syslog(LOG_ERR, "Error opening file %s", argv[2]);
        return 1;
    }

    ssize_t written = write(fd, argv[2], strlen(argv[2]));

    if (written != strlen(argv[2]))
    {
        syslog(LOG_ERR, "Write failed");
        return 1;
    }

    close(fd);

    return 0;
}