#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#include "monitor_ioctl.h"

#define STACK_SIZE (1024 * 1024)
#define CONTROL_PATH "/tmp/mini_runtime.sock"

typedef enum {
    CMD_SUPERVISOR = 0,
    CMD_START,
    CMD_RUN,
    CMD_PS,
    CMD_STOP,
    CMD_KILLALL
} command_kind_t;

typedef struct {
    command_kind_t kind;
    char container_id[32];
    char rootfs[256];
    char command[256];
    unsigned long soft_limit_bytes;
    unsigned long hard_limit_bytes;
} control_request_t;

typedef struct {
    char id[32];
    char rootfs[256];
    char command[256];
} child_config_t;

/* ================= CHILD ================= */

int child_func(void *arg)
{
    child_config_t *cfg = (child_config_t *)arg;

    printf("Child PID: %d\n", getpid());

    if (chroot(cfg->rootfs) < 0) {
        perror("chroot");
        return 1;
    }

    chdir("/");

    mount("proc", "/proc", "proc", 0, NULL);

    int log_fd = open("container.log", O_CREAT | O_WRONLY | O_APPEND, 0644);
    dup2(log_fd, STDOUT_FILENO);
    dup2(log_fd, STDERR_FILENO);
    close(log_fd);

    execl("/bin/sh", "sh", "-c", cfg->command, NULL);

    perror("exec failed");
    return 1;
}

/* ================= SUPERVISOR ================= */

static int run_supervisor()
{
    int server_fd, client_fd;
    struct sockaddr_un addr;

    unlink(CONTROL_PATH);

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, CONTROL_PATH);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    listen(server_fd, 5);
    printf("Supervisor running...\n");

    while (1) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        control_request_t req;
        read(client_fd, &req, sizeof(req));

        if (req.kind == CMD_START || req.kind == CMD_RUN) {

            printf("Launching container: %s\n", req.container_id);

            char *stack = malloc(STACK_SIZE);
            child_config_t *cfg = malloc(sizeof(child_config_t));

            strcpy(cfg->id, req.container_id);
            strcpy(cfg->rootfs, req.rootfs);
            strcpy(cfg->command, req.command);

            pid_t pid = clone(child_func,
                              stack + STACK_SIZE,
                              CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | SIGCHLD,
                              cfg);

            if (pid > 0) {
                printf("Started PID: %d\n", pid);

                /* Save metadata */
                FILE *f = fopen("containers.txt", "a");
                if (f) {
                    fprintf(f, "ID: %s PID: %d CMD: %s\n",
                            req.container_id, pid, req.command);
                    fclose(f);
                }

                /* Register with kernel module */
                int fd = open("/dev/container_monitor", O_RDWR);
                if (fd >= 0) {
                    struct monitor_request mreq;
                    memset(&mreq, 0, sizeof(mreq));

                    mreq.pid = pid;
                    mreq.soft_limit_bytes = req.soft_limit_bytes;
                    mreq.hard_limit_bytes = req.hard_limit_bytes;
                    strncpy(mreq.container_id,
                            req.container_id,
                            sizeof(mreq.container_id)-1);

                    ioctl(fd, MONITOR_REGISTER, &mreq);
                    close(fd);
                }
            }
        }

        close(client_fd);
    }
}

/* ================= CLIENT ================= */

static int send_request(control_request_t *req)
{
    int fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, CONTROL_PATH);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    write(fd, req, sizeof(*req));
    close(fd);

    return 0;
}

/* ================= COMMANDS ================= */

static int cmd_start(int argc, char *argv[])
{
    if (argc < 5) {
        printf("Usage: start <id> <rootfs> <cmd>\n");
        return 1;
    }

    control_request_t req;
    memset(&req, 0, sizeof(req));

    req.kind = CMD_START;
    strcpy(req.container_id, argv[2]);
    strcpy(req.rootfs, argv[3]);
    strcpy(req.command, argv[4]);

    req.soft_limit_bytes = 40UL << 20;
    req.hard_limit_bytes = 64UL << 20;

    return send_request(&req);
}

static int cmd_ps()
{
    FILE *f = fopen("containers.txt", "r");
    if (!f) {
        printf("No containers\n");
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), f))
        printf("%s", line);

    fclose(f);
    return 0;
}

static int cmd_stop(char *id)
{
    FILE *f = fopen("containers.txt", "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char cid[32];
        int pid;

        if (sscanf(line, "ID: %s PID: %d", cid, &pid) == 2) {
            if (strcmp(cid, id) == 0) {
                printf("Stopping %s\n", cid);
                kill(pid, SIGKILL);
            }
        }
    }

    fclose(f);
    return 0;
}

static int cmd_killall()
{
    FILE *f = fopen("containers.txt", "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int pid;
        if (sscanf(line, "ID: %*s PID: %d", &pid) == 1) {
            kill(pid, SIGKILL);
        }
    }

    fclose(f);
    printf("All containers killed\n");
    return 0;
}

/* ================= MAIN ================= */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: engine <command>\n");
        return 1;
    }

    if (strcmp(argv[1], "supervisor") == 0)
        return run_supervisor();

    if (strcmp(argv[1], "start") == 0)
        return cmd_start(argc, argv);

    if (strcmp(argv[1], "run") == 0)
        return cmd_start(argc, argv);

    if (strcmp(argv[1], "ps") == 0)
        return cmd_ps();

    if (strcmp(argv[1], "stop") == 0)
        return cmd_stop(argv[2]);

    if (strcmp(argv[1], "killall") == 0)
        return cmd_killall();

    printf("Unknown command\n");
    return 1;
}
