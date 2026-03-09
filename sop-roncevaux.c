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

/* Stage 3: combat loop - runs in child until knight's HP drops to 0 or below,
 * or all enemies are dead (all enemy write pipes broken).
 * Uses a swap-based living-enemy array for O(1) dead-enemy removal.
 * read_fd    : own pipe read end (non-blocking after setup)
 * enemy_pipes: array of enemy pipes; only [j][1] (write ends) are open here
 * n_enemy    : number of enemies
 */
void fight(struct Knight* k, int read_fd,
           int (*enemy_pipes)[2], int n_enemy)
{
    srand(getpid());
    signal(SIGPIPE, SIG_IGN);

    // Set own read end non-blocking
    int flags = fcntl(read_fd, F_GETFL);
    if (flags == -1) ERR("fcntl");
    if (fcntl(read_fd, F_SETFL, flags | O_NONBLOCK) == -1) ERR("fcntl");

    // living[0..last] are indices of still-alive enemies in enemy_pipes
    int* living = malloc(n_enemy * sizeof(int));
    if (!living) ERR("malloc");
    for (int i = 0; i < n_enemy; i++) living[i] = i;
    int last = n_enemy - 1;

    while (k->hp > 0 && last >= 0) {
        // Step 1: drain all damage bytes currently in the pipe
        uint8_t dmg;
        ssize_t r;
        while ((r = read(read_fd, &dmg, 1)) == 1) {
            k->hp -= (int)dmg;
            if (k->hp <= 0) break;
        }
        if (r == -1 && errno != EAGAIN) ERR("read");
        if (k->hp <= 0) break;

        // Step 2: pick a random living enemy and deal a blow
        int pos = rand() % (last + 1);
        int enemy = living[pos];
        uint8_t S = (uint8_t)(rand() % (k->attack + 1));
        if (write(enemy_pipes[enemy][1], &S, 1) == -1) {
            if (errno == EPIPE) {
                // enemy is dead: close his fd, swap with last living, shrink pool
                close(enemy_pipes[enemy][1]);
                living[pos] = living[last];
                last--;
                continue;
            }
            ERR("write");
        }

        // Step 3: print message based on blow strength
        if (S == 0)
            printf("%s attacks his enemy, however he deflected\n", k->name);
        else if (S <= 5)
            printf("%s goes to strike, he hit right and well\n", k->name);
        else
            printf("%s strikes powerful blow, the shield he breaks and inflicts a big wound\n", k->name);

        // Step 4: wait t ms, t in [1, 10]
        msleep(1 + rand() % 10);
    }

    // Close remaining open enemy write ends and own read end
    for (int i = 0; i <= last; i++)
        close(enemy_pipes[living[i]][1]);
    close(read_fd);
    free(living);

    printf("%s dies glorious death\n", k->name);
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

            // Stage 3: fight until dead
            fight(&knights[i], my_pipes[i][0], enemy_pipes, n_enemy);
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

