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

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem{
  struct spinlock lock;
  struct run *freelist;
} kmem;

/*为每个cpu维护一个freelist,每一个freelist都有独自的lock*/
struct kmem CPU_kmems[NCPU];

void
kinit()
{
  // initlock(&kmem.lock, "kmem");
  /*初始化NCPU个锁*/
  for(int i=0;i<NCPU;i++){
    initlock(&CPU_kmems[i].lock,"kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}



// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  /*获取当前运行的CPU号*/
  push_off();
  int now_CPU_id=cpuid();
  pop_off();
  
  /*头插法把被释放的块插进相应的freelist*/
  acquire(&CPU_kmems[now_CPU_id].lock);
  r->next = CPU_kmems[now_CPU_id].freelist;
  CPU_kmems[now_CPU_id].freelist = r; 
  release(&CPU_kmems[now_CPU_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  // struct run *r = kmem.freelist;

  /*获取当前运行的CPU号*/
  push_off();
  int now_CPU_id=cpuid();
  pop_off();

  acquire(&CPU_kmems[now_CPU_id].lock);
  struct run *r = CPU_kmems[now_CPU_id].freelist;
  if(r)/*有空内存直接用*/{
    CPU_kmems[now_CPU_id].freelist = r->next;
  }
  else/*偷内存*/
  {
    for (int i = 0; i < 8; i++)
    {
      if(i!=now_CPU_id){
        acquire(&CPU_kmems[i].lock);
        r=CPU_kmems[i].freelist;
        if(r){/*有空内存，进行窃取*/
          CPU_kmems[i].freelist=r->next;
          release(&CPU_kmems[i].lock);
          break;
        }
        release(&CPU_kmems[i].lock);
      }
      else
      {
        continue;
      }
    }
  }
  release(&CPU_kmems[now_CPU_id].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

uint64 free_mem(void){
  int len=0;
  struct run *run_pointer=kmem.freelist;;
  while(run_pointer){
    len++;
    run_pointer=run_pointer->next;
  }
  return len*PGSIZE;
}