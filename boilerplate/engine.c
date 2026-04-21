#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/mount.h>

#define STACK_SIZE (1024 * 1024)
#define SOCKET_PATH "/tmp/mini_runtime.sock"

typedef struct {
    int kind;
    char id[32];
    char rootfs[256];
    char command[256];
} request_t;

typedef struct {
    char id[32];
    char rootfs[256];
    char command[256];
} child_config_t;

int child_func(void *arg)
{
    child_config_t *cfg = (child_config_t *)arg;

    printf("Child PID: %d\n", getpid());

    chroot(cfg->rootfs);
    chdir("/");

    mount("proc", "/proc", "proc", 0, NULL);

    setenv("PATH", "/bin:/usr/bin:/sbin:/usr/sbin", 1);

    execl("/bin/sh", "sh", "-c", cfg->command, NULL);

    perror("exec failed");
    return 1;
}

/* ------------------ SUPERVISOR ------------------ */

int run_supervisor()
{
    int server_fd, client_fd;
    struct sockaddr_un addr;

    unlink(SOCKET_PATH);

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    listen(server_fd, 5);
    printf("Supervisor running...\n");

    while (1) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        request_t req;
        read(client_fd, &req, sizeof(req));

        if (req.kind == 1) { // START

            printf("Launching container: %s\n", req.id);

            char *stack = malloc(STACK_SIZE);
            child_config_t *cfg = malloc(sizeof(child_config_t));

            strcpy(cfg->id, req.id);
            strcpy(cfg->rootfs, req.rootfs);
            strcpy(cfg->command, req.command);

            pid_t pid = clone(child_func,
                              stack + STACK_SIZE,
                              CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | SIGCHLD,
                              cfg);

            if (pid > 0) {
                printf("Started container PID: %d\n", pid);

                /* ✅ FIXED METADATA WRITING */
                FILE *f = fopen("containers.txt", "a");
                if (f) {
                    fprintf(f, "ID: %s | CMD: %s\n", req.id, req.command);
                    fclose(f);
                }
            }
        }

        close(client_fd);
    }

    return 0;
}

/* ------------------ CLIENT ------------------ */

int send_request(request_t *req)
{
    int fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    write(fd, req, sizeof(*req));
    close(fd);

    printf("Request sent\n");
    return 0;
}

/* ------------------ COMMANDS ------------------ */

int cmd_start(int argc, char *argv[])
{
    if (argc < 5) {
        printf("Usage: start <id> <rootfs> <cmd>\n");
        return 1;
    }

    request_t req;
    memset(&req, 0, sizeof(req));

    req.kind = 1;
    strcpy(req.id, argv[2]);
    strcpy(req.rootfs, argv[3]);
    strcpy(req.command, argv[4]);

    return send_request(&req);
}

int cmd_ps()
{
    FILE *f = fopen("containers.txt", "r");

    if (!f) {
        printf("No containers found\n");
        return 0;
    }

    char line[256];
    printf("Containers:\n");

    while (fgets(line, sizeof(line), f)) {
        printf("%s", line);
    }

    fclose(f);
    return 0;
}

/* ------------------ MAIN ------------------ */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage:\n");
        printf("  supervisor\n");
        printf("  start <id> <rootfs> <cmd>\n");
        printf("  ps\n");
        return 1;
    }

    if (strcmp(argv[1], "supervisor") == 0) {
        return run_supervisor();
    }

    if (strcmp(argv[1], "start") == 0) {
        return cmd_start(argc, argv);
    }

    if (strcmp(argv[1], "ps") == 0) {
        return cmd_ps();
    }

    printf("Unknown command\n");
    return 1;
}
