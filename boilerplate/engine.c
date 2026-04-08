#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>

#define STACK_SIZE (1024 * 1024)
#define MAX_CONTAINERS 10
#define SOCKET_PATH "/tmp/runtime.sock"

static char stack[MAX_CONTAINERS][STACK_SIZE];

typedef struct {
    char id[32];
    pid_t pid;
    char state[16];
} container_t;

container_t containers[MAX_CONTAINERS];
int container_count = 0;

void cli_start(char *id, char *rootfs, char *cmd) {

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect failed");
        return;
    }

    char buffer[256];
    sprintf(buffer, "start %s %s %s", id, rootfs, cmd);

    write(sock, buffer, strlen(buffer));

    close(sock);
}

// ---------------- CONTAINER FUNCTION ----------------

int container_func(void *arg) {
    char **args = (char **)arg;

    char *cmd = args[0];
    char *rootfs = args[1];

    printf("Container starting...\n");

    if (chroot(rootfs) != 0) {
        perror("chroot failed");
        exit(1);
    }

    chdir("/");

    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        perror("mount failed");
    }

    char *new_args[] = {"/bin/sh", NULL};
execv("/bin/sh", new_args);

    perror("exec failed");
    return 1;
}

// ---------------- START CONTAINER ----------------
void start_container(char *id, char *rootfs, char *cmd) {

    int idx = container_count;

    char *args[] = {cmd, rootfs, NULL};

    pid_t pid = clone(container_func,
                      stack[idx] + STACK_SIZE,
                      CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD,
                      args);

    if (pid < 0) {
        perror("clone failed");
        exit(1);
    }

    strcpy(containers[idx].id, id);
    containers[idx].pid = pid;
    strcpy(containers[idx].state, "running");

    container_count++;

    printf("Started container %s (PID: %d)\n", id, pid);
}

// ---------------- REAP CHILDREN ----------------
void reap_children() {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < container_count; i++) {
            if (containers[i].pid == pid) {
                strcpy(containers[i].state, "stopped");
                printf("Container %s exited\n", containers[i].id);
            }
        }
    }
}

// ---------------- SUPERVISOR ----------------
void run_supervisor() {
    int server_fd, client_fd;
    struct sockaddr_un addr;

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        exit(1);
    }

    unlink(SOCKET_PATH);

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        exit(1);
    }

    listen(server_fd, 5);

    printf("Supervisor started...\n");

    // Start initial containers (Task 1 proof)
    start_container("alpha", "./rootfs-alpha", "/bin/sh");
    start_container("beta", "./rootfs-beta", "/bin/sh");

    while (1) {
        reap_children();

        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        char buffer[256] = {0};
        read(client_fd, buffer, sizeof(buffer));

        printf("Received command: %s\n", buffer);
	printf("Container count: %d\n", container_count);
        // -------- HANDLE ps --------
        if (strncmp(buffer, "ps", 2) == 0) {

	    char out[1024] = "";

    	strcat(out, "ID\tPID\tSTATE\n");

    	for (int i = 0; i < container_count; i++) {
        char line[128];
        sprintf(line, "%s\t%d\t%s\n",
                containers[i].id,
                containers[i].pid,
                containers[i].state);
        strcat(out, line);
    }

    write(client_fd, out, strlen(out));
}else if (strncmp(buffer, "start", 5) == 0) {

    char id[32], rootfs[64], cmd[64];

    sscanf(buffer, "start %s %s %s", id, rootfs, cmd);

    printf("Starting container from CLI: %s\n", id);

    start_container(id, rootfs, cmd);
}
        close(client_fd);
    }
}

// ---------------- CLI PS ----------------
void cli_ps() {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect failed (is supervisor running?)");
        return;
    }

    write(sock, "ps", 2);

    char buffer[512];
    int n = read(sock, buffer, sizeof(buffer) - 1);

    if (n > 0) {
        buffer[n] = '\0';
        printf("%s", buffer);
    }

    close(sock);
}

// ---------------- MAIN ----------------
int main(int argc, char *argv[]) {

    if (argc < 2) {
        printf("Usage:\n");
        printf("  ./engine supervisor\n");
        printf("  ./engine ps\n");
        return 1;
    }

    if (strcmp(argv[1], "supervisor") == 0) {
        run_supervisor();
    }

    else if (strcmp(argv[1], "ps") == 0) {
        cli_ps();
    }else if (strcmp(argv[1], "start") == 0) {

    if (argc < 5) {
        printf("Usage: ./engine start <id> <rootfs> <cmd>\n");
        return 1;
    }

    cli_start(argv[2], argv[3], argv[4]);
}

    else {
        printf("Unknown command\n");
    }

    return 0;
}
