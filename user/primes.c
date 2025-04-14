/*
    p = get a number from left neighbor
    print p
    loop:
        n = get a number from left neighbor
        if (p does not divide n)
            send n to right neighbor
*/

#include "kernel/types.h"
#include "user/user.h"

static int pleft = 0;
static int pright = 0;

void do_primes() __attribute__((noreturn));

void do_primes() {
    int p, n;

    read(pleft, &p, sizeof(int));
    fprintf(1, "prime %d\n", p);

    while (read(pleft, &n, sizeof(int)) > 0) {
        if (n % p == 0) {
            continue;
        }

        // spawn right
        if (!pright) {
            int pipefd[2];
            if (pipe(pipefd) < 0) {
                fprintf(2, "failed to pipe\n");
                exit(1);
            }

            int pid = fork();
            if (pid < 0) {
                fprintf(1, "failed to fork\n");
                exit(1);
            }

            if (pid == 0) {
                close(pipefd[1]);
                close(pleft);
                pleft = pipefd[0];
                do_primes();
            } else {
                close(pipefd[0]);
                pright = pipefd[1];
            }
        }

        write(pright, &n, sizeof(int));
    }

    close(pleft);

    if (pright) {
        close(pright);
        wait(0);
    }

    exit(0);
}

int main(int argc, char *argv[]) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        fprintf(2, "failed to pipe\n");
        exit(1);
    }

    int pid = fork();
    if (pid < 0) {
        fprintf(2, "failed to fork\n");
        exit(1);
    }

    if (pid == 0) {
        close(pipefd[1]);
        pleft = pipefd[0];
        do_primes();
    } else {
        close(pipefd[0]);
        pright = pipefd[1];
    }

    for (int i = 2; i <= 280; i++)
        write(pright, &i, sizeof(int));

    close(pright);
    wait(0);
    exit(0);
}
