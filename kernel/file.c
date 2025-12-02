//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "fcntl.h"
#include "memlayout.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

uint64
filemmap(struct file *f, uint64 len, int prot, int flags)
{
  int i;
  uint64 a;
  struct proc *p = myproc();

  if(prot & PROT_WRITE && flags & MAP_SHARED && !f->writable)
    return -1;

  for(i = 0; i < NMAP; i++){
    if(p->mm[i].va == 0)
      goto found;
  }
  return -1;

found:
  a = i > 0 ? p->mm[i-1].va : TRAPFRAME;
  a = PGROUNDDOWN(a-len);

  p->mm[i].va = a;
  p->mm[i].len = len;
  p->mm[i].prot = prot;
  p->mm[i].flags = flags;
  p->mm[i].off = 0;
  p->mm[i].f = f;

  filedup(f);

  return a;
}

int
filemmapa(uint64 va)
{
  struct proc *p;
  struct file *f;
  int i;
  char *mem;
  uint64 a, off;

  a = PGROUNDDOWN(va);
  p = myproc();

  if(a >= TRAPFRAME){
    printf("filemmapa: a >= TRAPFRAME\n");
    return -1;
  }

  // find map record
  for(i = 0; i < NMAP; i++){
    if(p->mm[i].va == 0)
      return -1;
    if(a >= p->mm[i].va && a < p->mm[i].va + p->mm[i].len)
      goto found;
  }
  return -1;

found:
  mem = kalloc();
  if(mem == 0)
    return -1;

  memset(mem, 0, PGSIZE);

  if(mappages(p->pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|PTE_W) != 0){
    kfree(mem);
    return -1;
  }

  off = a - p->mm[i].va + p->mm[i].off;
  f = p->mm[i].f;
  ilock(f->ip);
  readi(f->ip, 1, a, (uint)off, PGSIZE);
  iunlock(f->ip);

  return 0;
}

int
fileunmap(uint64 va, uint64 len)
{
  int i;
  uint n;
  uint64 a, nexta, off;
  pte_t *pte;
  struct file *f;

  struct proc *p = myproc();
  for(i = 0; i < NMAP; i++){
    if(p->mm[i].va == va)
      goto found;
  }
  return -1;

found:
  f = p->mm[i].f;

  ilock(f->ip);
  nexta = PGROUNDUP(va + min(len, p->mm[i].len));
  iunlock(f->ip);

  for(a = va; a < nexta; a += PGSIZE){
    pte = walk(p->pagetable, a, 0);
    if(!pte || !(*pte & PTE_V))
      continue;

    if(p->mm[i].flags & MAP_SHARED){
      begin_op();
      ilock(f->ip);
      off = a - va + p->mm[i].off;
      n = min(PGSIZE, f->ip->size - off);
      writei(f->ip, 1, a, off, n);
      iunlock(f->ip);
      end_op();
    }
    uvmunmap(p->pagetable, a, 1, 1);
  }

  if(nexta >= PGROUNDUP(va + p->mm[i].len)){
    for(; i < NMAP-1; i++){
      p->mm[i] = p->mm[i+1];
    }
    p->mm[NMAP-1].va = 0;
    fileclose(f);
  } else {
    p->mm[i].va = nexta;
    p->mm[i].len -= nexta - va;
    p->mm[i].off += nexta - va;
  }

  return 0;
}
