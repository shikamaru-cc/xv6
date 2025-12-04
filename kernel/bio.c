// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 3
#define NSLOT (NBUF / NBUCKET)

struct bcache {
  struct spinlock lock;
  struct buf buf[NSLOT];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
};

struct bcache buckets[NBUCKET];

void
binit(void)
{
  struct buf *b;
  struct bcache *bc;

  for(int i = 0; i < NBUCKET; i++){
    bc = &buckets[i];
    initlock(&bc->lock, "bcache.bucket");
    bc->head.prev = &bc->head;
    bc->head.next = &bc->head;
    for(b = bc->buf; b < bc->buf+NSLOT; b++){
      b->next = bc->head.next;
      b->prev = &bc->head;
      initsleeplock(&b->lock, "buffer");
      bc->head.next->prev = b;
      bc->head.next = b;
    }
  }
}

static inline struct bcache*
bucket_get(uint blockno)
{
  return &buckets[blockno % NBUCKET];
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  struct bcache *bc = bucket_get(blockno);
  acquire(&bc->lock);

  // Is the block already cached?
  for(b = bc->head.next; b != &bc->head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bc->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bc->head.prev; b != &bc->head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bc->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  struct bcache *bc;

  if(!holdingsleep(&b->lock))
    panic("brelse");

  bc = bucket_get(b->blockno);

  releasesleep(&b->lock);

  acquire(&bc->lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bc->head.next;
    b->prev = &bc->head;
    bc->head.next->prev = b;
    bc->head.next = b;
  }

  release(&bc->lock);
}

void
bpin(struct buf *b) {
  struct bcache *bc = bucket_get(b->blockno);
  acquire(&bc->lock);
  b->refcnt++;
  release(&bc->lock);
}

void
bunpin(struct buf *b) {
  struct bcache *bc = bucket_get(b->blockno);
  acquire(&bc->lock);
  b->refcnt--;
  release(&bc->lock);
}
