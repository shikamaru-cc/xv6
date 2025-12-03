// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);
void kfree_i(void *pa, uint i);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct arena {
  struct spinlock lock;
  struct run *freelist;
} arenas[NCPU];

char arena_names[NCPU][128];

void
kinit()
{
  // initlock(&kmem.lock, "kmem");
  // freerange(end, (void*)PHYSTOP);

  for(int i = 0; i < NCPU; i++){
    // snprintf(arena_names[i], 128, "kmem %d", i);
    initlock(&arenas[i].lock, "kmem");
  }

  char *p;
  p = (char*)PGROUNDUP((uint64)end);
  for(; p + PGSIZE <= (char*)PHYSTOP; p += PGSIZE)
    kfree_i(p, ((uint64)p >> PGSHIFT) % NCPU);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

void
kfree_i(void *pa, uint i)
{
  struct run *r;
  struct arena *arena;

  if(i >= NCPU)
    panic("kfree_i: i >= NCPU");

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  arena = arenas + i;

  acquire(&arena->lock);
  r->next = arena->freelist;
  arena->freelist = r;
  release(&arena->lock);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  kfree_i(pa, cpuid());
}

void *
kalloc_i(int i)
{
  struct run *r;
  struct arena *arena = &arenas[i];

  acquire(&arena->lock);
  r = arena->freelist;
  if(r)
    arena->freelist = r->next;
  release(&arena->lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int i = cpuid();

  if((r = kalloc_i(i)))
    return r;

  // steal
  for(i = 0; i < NCPU; i++){
    if((r = kalloc_i(i)))
      break;
  }

  return r;
}
