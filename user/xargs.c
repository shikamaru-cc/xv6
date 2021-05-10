#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    char buf[512], *p1, *p2;
    char *args[MAXARG];

    argc = argc - 1;
    int argn = argc;
    for(int i = 0; i < argc; i++){
        args[i] = argv[i+1];
    }

    p1 = p2 = buf;
    int runFork = 0;
    while(read(0, p2, 1)){
        switch(*p2){
        case ' ':
        case '\n':
            if(*p2 == '\n'){
                runFork = 1;
            }
            *p2++ = 0;
            args[argn++] = p1;
            p1 = p2;
            if(runFork){
                if(fork() == 0){
                    exec(argv[1], args);
                } else {
                    wait(0);
                    argn = argc;
                    p1 = p2 = buf; 
                    runFork = 0;
                }
            }
            break;
        
        default:
            p2++;
            break;
        }
    }    
    exit(0);
}
