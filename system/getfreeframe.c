/* getfreeframe.c - getfreeframe, enqueuef, dequeuef */

#include <xinu.h>
#include <stdio.h>
#include <stdlib.h>

/*------------------------------------------------------------------------
 *  getfreeframe  -  Get free frame
 *------------------------------------------------------------------------
 */
uint32 getfreeframe() {
	uint32 * addr;
	int p, q, frameid;
	pid32 pid;
    pd_t *pd;
	pt_t *pt;
	uint32 tmp;
	struct fnode* fn;
	pt_t        ptentry;
	pd_t        pdentry;
	/* Obtain a free frame 
	        1. search the invert page table for an empty frame. stop if one exists
			2. else: pick a page to replace
			    search invert page table to get virtual page number of the page vp
				a = vp*4096 the first virtual address on page vp
				p = a[31:22], q = [21:12]
				pid, pd page directory pointer, pt pth page table pointer,
				mark the approriate entry of pt as not present
				if the page being removed belongs to the current process, invalidate the TLB entry for page vp using invlpg
				invert page table, decrement the reference count of the frame occupied by pt, reference count 0 -> mark as "not present"
				if dirty bit for page vp was set, 1) backing store map -> store s and offset o
				                                  2) write the page back to the backing store*/
    uint32 i;
	for(i=0;i<NFRAMES;i++) {
		if(ipttab[i].pid == -1) {
			if(i<6) {
				hook_ptable_create(i);
				return i;
			}
			hook_ptable_create(i);
            enqueuef(framelist, i);//myglobalclock
			return i;
		}
	}
	// Paging out necessary frames
	pid = getpid();
	if(currpolicy == FIFO) {
        fn = dequeuef(framelist);
		tmp = fn->key;
	} else if (currpolicy == GCA) {
        fn = dequeuef(framelist);
		tmp = fn->key;
	} else {
		kprintf("Page replacement policy not set. \n\n");
	}
    uint32 vpn = ipttab[tmp].vpn;
    addr = (uint32*)(vpn*4096);
    p = (uint32)addr >> 22;
	q = (uint32)addr >> 12 & 0x03ff; 
	pid = ipttab[tmp].pid;
	pd = pdtable[pid];
	pd = pd + p;
	pdentry = *pd;

	pt = (pt_t*)(pdentry.pd_base << 12);
	frameid = (pdentry.pd_base << 12)/4096;
	pt = pt + q;
	ptentry = *pt;
	ptentry.pt_pres = 0;
	ipttab[frameid].refcnt--;
	if(ipttab[frameid].refcnt == 0) {
		pdentry.pd_pres = 0;
		*pd = pdentry;
	}
	// dirty bit for page vp is set
	if(ptentry.pt_dirty==1) {
		int store = -1;
		int offset = -1;
		bsm_lookup(pid, addr, &store, &offset);
		if(store == -1 || offset == -1) {
			kprintf("Unable to find the backing store and offset with virtual address %d \n", *addr);
			kill(pid);
			return SYSERR;
		}
		write_bs((char *)(0x00400000+tmp*4096), store, offset);
        
	}
	hook_pswap_out(pid, vpn,  frameid);
	// if(pid==getpid()) {
	// 	asm("4(%esp),%eax");
	// 	asm("invlpg (%eax)");
	// }
	// Decrement the reference count of the frame occupied by pt
    

	return SYSERR;
}

struct fnode* newNode(uint32 k) 
{ 
    struct fnode* temp = (struct fnode*)getmem(sizeof(struct fnode)); 
    temp->key = k; 
    temp->next = NULL; 
    return temp; 
} 

struct fqueue* createQueue() 
{ 
    struct fqueue* q = (struct fqueue*)getmem(sizeof(struct fqueue)); 
    q->front = q->rear = NULL; 
    return q; 
} 

/*
 * enqueuef
 */

void enqueuef(struct fqueue* q, uint32 k) 
{ 
    // Create a new LL node 
    struct fnode* temp = newNode(k); 
  
    // If queue is empty, then new node is front and rear both 
    if (q->rear == NULL) { 
        q->front = q->rear = temp; 
        return; 
    } 
  
    // Add the new node at the end of queue and change rear 
    q->rear->next = temp; 
    q->rear = temp; 
} 

/*
 * dequeuef
 */

struct fnode* dequeuef(struct fqueue* q) 
{ 
    // If queue is empty, return NULL. 
    if (q->front == NULL) 
        return NULL; 
  
    // Store previous front and move front one node ahead 
    struct fnode* temp = q->front; 
    freemem((char*)temp, sizeof(struct fnode)); 
  
    q->front = q->front->next; 
  
    // If front becomes NULL, then change rear also as NULL 
    if (q->front == NULL) 
        q->rear = NULL; 
    return temp; 
} 