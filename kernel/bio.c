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

#define hash_buckets_num 13

struct {
  struct spinlock lock[hash_buckets_num];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;

  struct buf hash_buckets[hash_buckets_num];
  /*每个哈希桶序列一个linklist和一个lock*/
} bcache;

void
binit(void)
{
  struct buf *b;

  // initlock(&bcache.lock, "bcache");
  /*初始化哈希桶*/
  for (int i = 0; i < hash_buckets_num; i++)
  {
    /*队列前后均为自己*/
    bcache.hash_buckets[i].prev=&bcache.hash_buckets[i];
    bcache.hash_buckets[i].next=&bcache.hash_buckets[i];
    /*初始化🔒*/
    initlock(&bcache.lock[i],"bcache");
  }
  
  // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    int hash_code=(b->blockno)% 13;
    /*磁盘块号%13为对应的哈希桶号*/
    b->next = bcache.hash_buckets[hash_code].next;
    b->prev = &bcache.hash_buckets[hash_code];
    /*把b放进hash_buckets[hash_code]中*/
    initsleeplock(&b->lock, "buffer");
    bcache.hash_buckets[hash_code].next->prev = b;
    bcache.hash_buckets[hash_code].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int hash_code=(b->blockno)% 13;
  /*磁盘块号%13为对应的哈希桶号*/
  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.hash_buckets[hash_code].next; b != &bcache.hash_buckets[hash_code]; b = b->next)
  /*查找自己的哈希桶区有没有对应的扇区*/
  {
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
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
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


