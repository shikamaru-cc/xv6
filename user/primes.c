#include "kernel/types.h"
#include "user/user.h"

typedef enum {
    false,
    true
} bool;

bool 
isPrime(int a)
{
    for (int i = 2; i < a/2; i++) {
        if ((a % i) == 0) {
            return false;
        }
    }
    return true;
}

#define BUF_SIZE 10
typedef struct BufIO
{
    char buf[BUF_SIZE];
    int unread;
    int fd;
} *BufIO;

BufIO
newBufIO(int fd)
{
    BufIO bufio = malloc(sizeof(BufIO));
    memset(bufio->buf, '\0', BUF_SIZE);
    bufio->unread = 0;
    bufio->fd = fd;
    return bufio;
}

bool
readNum(BufIO bufio, char *dest)
{
    char num[100];
    int nCursor = 0;
    int bCursor = BUF_SIZE - bufio->unread;

    while(1) {
        while(bufio->unread > 0) {
            num[nCursor] = bufio->buf[bCursor];
            bufio->unread--;
            if(num[nCursor] == '\0') {
                strcpy(dest, num);
                return true;
            }
            nCursor++;
            bCursor++;
        }
        int n = read(bufio->fd, bufio->buf, BUF_SIZE);
        if(n <= 0) {
            return false;
        }
        bufio->unread = n;
        bCursor = 0;
    }
}

void
runFilter(int pRead[2], int pWrite[2], int feed)
{
    char num[5];
    int n;
    BufIO bufio;

    close(pRead[1]);
    close(pWrite[0]);

    bufio = newBufIO(pRead[0]);
    while(readNum(bufio, num)) {
        n = atoi(num);
        if(feed == n) {
            fprintf(1, "prime %d\n", feed);
            continue;
        }
        if(n % feed != 0) {
            write(pWrite[1], num, strlen(num)+1);
        }
    }

    free(bufio);

    close(pRead[0]);
    close(pWrite[1]);
}

int
main(int argc, char *argv[])
{
    int wg = 0;
    int pWrite[2], pRead[2], pMain[2];

    pipe(pWrite);
    fprintf(1, "%d %d\n", pWrite[0], pWrite[1]);
    // pMain is the write pipe for main routine
    memcpy(pMain, pWrite, 2);

    for(int i = 2; i <= 35; i++) {
        if(isPrime(i)) {
            memcpy(pRead, pWrite, 2);
            pipe(pWrite);
            if(fork() == 0) {
                // close main pipe
                if(pRead[0] != pMain[0] && pRead[1] != pMain[1]) {
                    close(pMain[0]);
                    close(pMain[1]);
                }
                runFilter(pRead, pWrite, i);
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

    fprintf(1, "%d\n", pMain[1]);
    for(int i = 0; i <= 35; i++) {
        fprintf(pMain[1], "%d\0", i);
    }
    close(pMain[0]);
    close(pMain[1]);

    for(int i = 0; i < wg; i++) {
        wait(0);
    }
    exit(0);
}
