#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MAX_GRAPH_NODES 32
#define MAX_PATH_LENGTH (2 * MAX_GRAPH_NODES)

#define FIFO_NAME "/tmp/colony_fifo"

static volatile sig_atomic_t should_stop = 0;
static int self_read_fd = -1;

int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
}

void msleep(int ms)
{
    struct timespec tt;
    tt.tv_sec = ms / 1000;
    tt.tv_nsec = (ms % 1000) * 1000000;
    while (nanosleep(&tt, &tt) == -1)
    {
    }
}

void usage(char* argv[])
{
    printf("%s graph start dest\n", argv[0]);
    printf("  graph - path to file containing colony graph\n");
    printf("  start - starting node index\n");
    printf("  dest - destination node index\n");
    exit(EXIT_FAILURE);
}

void sigint_handler(int sig)
{
    (void)sig;
    should_stop = 1;

    /* Closing own read end unblocks read() in children. */
    if (self_read_fd != -1)
    {
        close(self_read_fd);
        self_read_fd = -1;
    }
}

void load_graph(const char* filename, int graph[MAX_GRAPH_NODES][MAX_GRAPH_NODES], int* vertices)
{
    FILE* file = fopen(filename, "r");
    if (!file)
        ERR("fopen");

    if (fscanf(file, "%d", vertices) != 1)
    {
        fclose(file);
        fprintf(stderr, "Invalid graph file format\n");
        exit(EXIT_FAILURE);
    }

    if (*vertices < 1 || *vertices > MAX_GRAPH_NODES)
    {
        fclose(file);
        fprintf(stderr, "Invalid number of vertices: %d\n", *vertices);
        exit(EXIT_FAILURE);
    }

    int from, to;
    while (fscanf(file, "%d %d", &from, &to) == 2)
    {
        if (from < 0 || from >= *vertices || to < 0 || to >= *vertices)
        {
            fclose(file);
            fprintf(stderr, "Invalid edge: %d -> %d\n", from, to);
            exit(EXIT_FAILURE);
        }
        graph[from][to] = 1;
    }

    fclose(file);
}

void validate_nodes(int start, int dest, int vertices)
{
    if (start < 0 || start >= vertices)
    {
        fprintf(stderr, "Invalid start node: %d\n", start);
        exit(EXIT_FAILURE);
    }

    if (dest < 0 || dest >= vertices)
    {
        fprintf(stderr, "Invalid destination node: %d\n", dest);
        exit(EXIT_FAILURE);
    }
}

void create_pipes(int pipes[MAX_GRAPH_NODES][2], int vertices)
{
    for (int i = 0; i < vertices; i++)
    {
        if (pipe(pipes[i]) == -1)
            ERR("pipe");
    }
}

void close_unused_child_fds(int id,
                            int vertices,
                            int graph[MAX_GRAPH_NODES][MAX_GRAPH_NODES],
                            int pipes[MAX_GRAPH_NODES][2])
{
    for (int i = 0; i < vertices; i++)
    {
        if (i != id)
            close(pipes[i][0]);

        if (!graph[id][i])
            close(pipes[i][1]);
    }

    if (!graph[id][id])
        close(pipes[id][1]);
}

void close_unused_parent_fds(int start, int vertices, int pipes[MAX_GRAPH_NODES][2])
{
    for (int i = 0; i < vertices; i++)
    {
        close(pipes[i][0]);

        if (i != start)
            close(pipes[i][1]);
    }
}

void close_child_write_ends(int id,
                            int vertices,
                            int graph[MAX_GRAPH_NODES][MAX_GRAPH_NODES],
                            int pipes[MAX_GRAPH_NODES][2])
{
    for (int i = 0; i < vertices; i++)
    {
        if (graph[id][i])
            close(pipes[i][1]);
    }

    if (graph[id][id])
        close(pipes[id][1]);
}

void listen_on_pipe(void)
{
    char buffer;

    while (!should_stop)
    {
        ssize_t result = read(self_read_fd, &buffer, 1);

        if (result > 0)
            continue;

        if (result == 0)
            break;

        if (errno == EINTR)
            continue;

        if (should_stop)
            break;

        ERR("read");
    }
}

void run_node_process(int id,
                      int vertices,
                      int graph[MAX_GRAPH_NODES][MAX_GRAPH_NODES],
                      int pipes[MAX_GRAPH_NODES][2])
{
    self_read_fd = pipes[id][0];

    if (set_handler(sigint_handler, SIGINT) == -1)
        ERR("sigaction");

    listen_on_pipe();

    if (self_read_fd != -1)
        close(self_read_fd);

    close_child_write_ends(id, vertices, graph, pipes);
    exit(EXIT_SUCCESS);
}

void spawn_children(int vertices,
                    int graph[MAX_GRAPH_NODES][MAX_GRAPH_NODES],
                    int pipes[MAX_GRAPH_NODES][2],
                    pid_t children[MAX_GRAPH_NODES])
{
    for (int i = 0; i < vertices; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
            ERR("fork");

        if (pid == 0)
        {
            close_unused_child_fds(i, vertices, graph, pipes);
            run_node_process(i, vertices, graph, pipes);
        }

        children[i] = pid;
    }
}

void stop_children(pid_t children[MAX_GRAPH_NODES], int vertices)
{
    for (int i = 0; i < vertices; i++)
    {
        if (kill(children[i], SIGINT) == -1 && errno != ESRCH)
            ERR("kill");
    }
}

void wait_for_children(pid_t children[MAX_GRAPH_NODES], int vertices)
{
    for (int i = 0; i < vertices; i++)
    {
        if (waitpid(children[i], NULL, 0) == -1)
            ERR("waitpid");
    }
}

void bootstrap_start_node(int start, int pipes[MAX_GRAPH_NODES][2])
{
    char token = 'S';

    if (write(pipes[start][1], &token, 1) == -1)
        ERR("write");
}

void wait_for_sigint(void)
{
    while (!should_stop)
        pause();
}

int main(int argc, char* argv[])
{
    if (argc != 4)
        usage(argv);

    int graph[MAX_GRAPH_NODES][MAX_GRAPH_NODES] = {0};
    int vertices = 0;
    int pipes[MAX_GRAPH_NODES][2];
    pid_t children[MAX_GRAPH_NODES];
    int start = atoi(argv[2]);
    int dest = atoi(argv[3]);

    load_graph(argv[1], graph, &vertices);
    validate_nodes(start, dest, vertices);

    if (set_handler(sigint_handler, SIGINT) == -1)
        ERR("sigaction");

    create_pipes(pipes, vertices);
    spawn_children(vertices, graph, pipes, children);
    close_unused_parent_fds(start, vertices, pipes);

    /* Start node is the only inbox the parent can still write to. */
    bootstrap_start_node(start, pipes);

    /* Stop the simulation with Ctrl+C. */
    wait_for_sigint();

    stop_children(children, vertices);
    close(pipes[start][1]);
    wait_for_children(children, vertices);

    return 0;
}
