#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int p1[2], p2[2];
    pipe(p1);
    pipe(p2);

    if (fork() == 0) {
        char c;
        close(p1[1]);
        close(p2[0]);
        if (read(p1[0], &c, 1) >= 0) {
            fprintf(1, "%d: received ping\n", getpid());
            write(p2[1], &c, 1);
        }
        close(p1[0]);
        close(p2[1]);
    } else {
        char c;
        close(p1[0]);
        close(p2[1]);
        write(p1[1], "a", 1);
        if (read(p2[0], &c, 1) >= 0) {
            if (c == 'a') {
                fprintf(1, "%d: received pong\n", getpid());
            } else {
                fprintf(1, "the byte received is not the same as what parent sends\n");
            }
        }
        close(p1[1]);
        close(p2[0]);
    }
    exit(0);
}
