#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
} Knight;
    
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

int read_file(char* filename, char* side)
{
    int num_of_knights;
    FILE *file;
    file = fopen(filename, "r");

    if (file == NULL) {
        printf("%s have not arrived on the battlefield", side);
    };

    fscanf(file, "%d", &num_of_knights);
    printf("Number of knights %d\n", num_of_knights);

    for (int i=0; i<num_of_knights; i++) {
        struct Knight k;
        fscanf(file, "%s %d %d", k.name, &k.hp, &k.attack);
        printf("I am %s knight %s. I will serve my king with my %d HP and %d attack\n", side, k.name, k.hp, k.attack);
    }

    fclose(file);
    return num_of_knights;

}

int main(int argc, char* argv[])
{
    // srand(time(NULL));
    // printf("Opened descriptors: %d\n", count_descriptors());
    read_file("saraceni.txt", "Saracens");
    read_file("franci.txt", "Franci");
}
