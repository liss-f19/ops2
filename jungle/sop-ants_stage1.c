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

// signal handler
int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
}


// sleep
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

// read graph from txt
// graph[u][v] == 1 means there is an edge otherwise 0
void load_graph(const char* filename, int graph[MAX_GRAPH_NODES][MAX_GRAPH_NODES], int* vertices)
{
    FILE* file = fopen(filename, "r");
    if (!file)
        ERR("fopen");

    // read num of vertices
    if (fscanf(file, "%d", vertices) != 1)
    {
        fclose(file);
        fprintf(stderr, "Invalid graph file format\n");
        exit(EXIT_FAILURE);
    }

    // validate vertex count
    if (*vertices < 1 || *vertices > MAX_GRAPH_NODES)
    {
        fclose(file);
        fprintf(stderr, "Invalid number of vertices: %d\n", *vertices);
        exit(EXIT_FAILURE);
    }


    // read all edges until EOF
    int from, to;
    while (fscanf(file, "%d %d", &from, &to) == 2)
    {
        if (from < 0 || from >= *vertices || to < 0 || to >= *vertices)
        {
            fclose(file);
            fprintf(stderr, "Invalid edge: %d -> %d\n", from, to);
            exit(EXIT_FAILURE);
        }
        // store edge from -> to
        graph[from][to] = 1;
    }

    fclose(file);
}

// print one vertex and all of its outgoing neighbors
// format {ID}: {NEIGHBORS_LIST}
void print_neighbors(int id, int vertices, int graph[MAX_GRAPH_NODES][MAX_GRAPH_NODES])
{
    printf("%d:", id);
    for (int neighbor = 0; neighbor < vertices; neighbor++)
    {
        if (graph[id][neighbor])
            printf(" %d", neighbor);
    }
    printf("\n");
    fflush(stdout);
}

int main(int argc, char* argv[])
{
    if (argc != 4)
        usage(argv);

        // create and init with zzeroes the matrix
    int graph[MAX_GRAPH_NODES][MAX_GRAPH_NODES] = {0};
    int vertices = 0;

    // load from file
    load_graph(argv[1], graph, &vertices);

    // for storing children pids, so the main process can wait for children
    pid_t children[MAX_GRAPH_NODES];

    // create 1 child per vertex
    for (int i = 0; i < vertices; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
            ERR("fork");

        if (pid == 0) //it is child
        {
            print_neighbors(i, vertices, graph);
            exit(EXIT_SUCCESS);
        }
        
        // parent stores children pids
        children[i] = pid;
    }

    // use children pids for waiting
    for (int i = 0; i < vertices; i++)
        waitpid(children[i], NULL, 0);

    return 0;
}
