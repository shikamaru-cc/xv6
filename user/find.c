#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char*
filename(char *path)
{
    static char buf[DIRSIZ+1];
    char *p;

    // Find first character after last slash.
    for(p=path+strlen(path); p >= path && *p != '/'; p--)
      ;
    p++;

    // Return blank-padded name.
    if(strlen(p) >= DIRSIZ)
      return p;
    strcpy(buf, p);    
    return buf;
}

void
strDirentName(char str[DIRSIZ+1], char name[DIRSIZ])
{
    memcpy(str, name, DIRSIZ);
    char *p;
    for(p = str+DIRSIZ-1; str <= p && *p == ' '; p--)
        ;
    p++;
    *p = 0;
    return;
}

void
find(char *path, char *target)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: can not open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch(st.type){
    case T_FILE: 
        if(strcmp(filename(path), target) == 0)
            printf("%s\n", path);
        close(fd);
        return;

    case T_DIR:
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
            printf("ls: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf+strlen(buf);
        *p++ = '/';
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            if(de.inum == 0)
                continue;
            if((strcmp(de.name, ".") == 0) || (strcmp(de.name, "..") == 0))
                continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            find(buf, target);
        } 
    }

    close(fd);
    return;
}

int
main(int argc, char *argv[])
{
    if(argc < 2) {
        fprintf(2, "usage: find [path] filename\n");
        exit(0);
    }
    if(argc == 2){
        find(".", argv[1]);
    } else {
        find(argv[1], argv[2]);
    }
    exit(0);
}
