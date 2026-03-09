#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_KNIGHT_NAME_LENGHT 20

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

struct Knight{
    char name[MAX_KNIGHT_NAME_LENGHT];
    int hp;
    int attack;
};
    
int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
}

void msleep(int millisec)
{
    struct timespec tt;
    tt.tv_sec = millisec / 1000;
    tt.tv_nsec = (millisec % 1000) * 1000000;
    while (nanosleep(&tt, &tt) == -1)
    {
    }
}

int count_descriptors()
{
    int count = 0;
    DIR* dir;
    struct dirent* entry;
    struct stat stats;
    if ((dir = opendir("/proc/self/fd")) == NULL)
        ERR("opendir");
    char path[PATH_MAX];
    getcwd(path, PATH_MAX);
    chdir("/proc/self/fd");
    do
    {
        errno = 0;
        if ((entry = readdir(dir)) != NULL)
        {
            if (lstat(entry->d_name, &stats))
                ERR("lstat");
            if (!S_ISDIR(stats.st_mode))
                count++;
        }
    } while (entry != NULL);
    if (chdir(path))
        ERR("chdir");
    if (closedir(dir))
        ERR("closedir");
    return count - 1;  // one descriptor for open directory
}

int read_file(char* filename, char* side_name, char* knight_type)
{
    int num_of_knights;
    FILE *file;
    file = fopen(filename, "r");

    if (file == NULL) {
        printf("%s have not arrived on the battlefield\n", side_name);
        return 0;
    }

    fscanf(file, "%d", &num_of_knights);

    for (int i=0; i<num_of_knights; i++) {
        struct Knight k;
        fscanf(file, "%19s %d %d", k.name, &k.hp, &k.attack);
        printf("I am %s knight %s. I will serve my king with my %d HP and %d attack\n", knight_type, k.name, k.hp, k.attack);
    }

    fclose(file);
    return num_of_knights;

}

/* Stage 2: load knights from file into a dynamically allocated array.
 * On failure to open, prints the side_name error message and exits. */
struct Knight* load_knights(char* filename, char* side_name, int* count)
{
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        printf("%s have not arrived on the battlefield\n", side_name);
        exit(EXIT_FAILURE);
    }
    fscanf(file, "%d", count);
    struct Knight* knights = malloc(*count * sizeof(struct Knight));
    if (!knights) ERR("malloc");
    for (int i = 0; i < *count; i++) {
        knights[i] = (struct Knight){0};
        fscanf(file, "%19s %d %d", knights[i].name, &knights[i].hp, &knights[i].attack);
    }
    fclose(file);
    return knights;
}

/* Allocate and create n unnamed pipes. */
int (*create_pipes(int n))[2]
{
    int (*pipes)[2] = malloc(n * sizeof(int[2]));
    if (!pipes) ERR("malloc");
    for (int i = 0; i < n; i++)
        if (pipe(pipes[i])) ERR("pipe");
    return pipes;
}

/* Close all ends of n pipes. */
void close_pipes(int (*pipes)[2], int n)
{
    for (int i = 0; i < n; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

/* Wait for n child processes. */
void wait_children(pid_t* pids, int n)
{
    for (int i = 0; i < n; i++)
        waitpid(pids[i], NULL, 0);
}

/*
 * Fork one child per knight on my_side.
 * Each child keeps:  my_pipes[i][0]          (own read end)
 *                    enemy_pipes[j][1] for all j  (write ends to every enemy)
 * Everything else is closed before printing and exiting.
 * Appends spawned pids to pids[] and returns the count added.
 */
int spawn_knights(struct Knight* knights, int n,
                  int (*my_pipes)[2],
                  int (*enemy_pipes)[2], int n_enemy,
                  const char* knight_type,
                  pid_t* pids)
{
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) ERR("fork");
        if (pid == 0) {
            printf("Open descriptors before close: %d\n", count_descriptors());
            // Close all own-side pipes except my read end
            for (int k = 0; k < n; k++) {
                close(my_pipes[k][1]);          // never need own write end
                if (k != i) close(my_pipes[k][0]);
            }
            printf("Open descriptors after close my own: %d\n", count_descriptors());
            // Close all enemy read ends (keep enemy write ends)
            for (int j = 0; j < n_enemy; j++)
                close(enemy_pipes[j][0]);

            printf("I am %s knight %s. I will serve my king with my %d HP and %d attack\n",
                   knight_type, knights[i].name, knights[i].hp, knights[i].attack);
            printf("Open descriptors after close enemy: %d\n", count_descriptors());
            exit(EXIT_SUCCESS);
        }
        pids[i] = pid;
    }
    return n;
}

int main(int argc, char* argv[])
{
    srand(time(NULL));
    printf("Opened descriptors: %d\n", count_descriptors());

    // Stage 1 – now done by child processes:
    // read_file("saraceni.txt", "Saracens", "Spanish");
    // read_file("franci.txt", "Franks", "Frankish");

    int n_s, n_f;
    struct Knight* saracens = load_knights("saraceni.txt", "Saracens", &n_s);
    struct Knight* franks   = load_knights("franci.txt",   "Franks",   &n_f);

    int (*pipes_s)[2] = create_pipes(n_s);
    int (*pipes_f)[2] = create_pipes(n_f);

    pid_t* pids = malloc((n_s + n_f) * sizeof(pid_t));
    if (!pids) ERR("malloc");

    spawn_knights(saracens, n_s, pipes_s, pipes_f, n_f, "Spanish",  pids);
    spawn_knights(franks,   n_f, pipes_f, pipes_s, n_s, "Frankish", pids + n_s);

    close_pipes(pipes_s, n_s);
    close_pipes(pipes_f, n_f);

    wait_children(pids, n_s + n_f);

    free(pipes_s); free(pipes_f); free(pids); free(saracens); free(franks);
    printf("Opened descriptors after finish: %d\n", count_descriptors());
    return 0;
}

