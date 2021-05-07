#include "kernel/types.h"
#include "user/user.h"

typedef enum {
    false,
    true
} bool;

bool 
isPrime(int a)
{
    for (int i = 2; i <= a/2; i++) {
        if ((a % i) == 0) {
            return false;
        }
    }
    return true;
}

void
runFilter(int pRead[2], int pWrite[2], int feed)
{
    char num[5];
    char c;
    int n;

    int i = 0;
    while(read(pRead[0], &c, 1) != 0) {
        num[i++] = c;
        if(c != '\0') {
            continue;
        }
        n = atoi(num);
        if(n == feed) {
            fprintf(1, "prime %d\n", n);
        } else if(n % feed != 0) {
            write(pWrite[1], num, strlen(num)+1);
        }
        i = 0;
    }
}

int
main(int argc, char *argv[])
{
    int wg = 0;
    int pWrite[2], pRead[2], pMain[2];

    pipe(pWrite);
    // pMain is the write pipe for main routine
    memcpy(pMain, pWrite, 2*sizeof(int));

    for(int i = 2; i <= 35; i++) {
        if(isPrime(i)) {
            memcpy(pRead, pWrite, 2*sizeof(int));
            pipe(pWrite);
            if(fork() == 0) {
                // close main pipe
                if(pRead[0] != pMain[0] && pRead[1] != pMain[1]) {
                    close(pMain[0]);
                    close(pMain[1]);
                } 
                close(pRead[1]);
                close(pWrite[0]);

                runFilter(pRead, pWrite, i);

                close(pRead[0]);
                close(pWrite[1]);

                exit(0);
            } else {
                // close read pipe
                // reserve write pipe for next routine
                if(pRead[0] != pMain[0] && pRead[1] != pMain[1]) {
                    close(pRead[0]);
                    close(pRead[1]);
                }
                wg++;
            }
        }
    }

    char num[5];
    for(int i = 2; i <= 35; i++) {
        itoa(i, num);
        write(pMain[1], num, strlen(num)+1);
    }
    close(pMain[0]);
    close(pMain[1]);

    for(int i = 0; i < wg; i++) {
        wait(0);
    }
    exit(0);
}
