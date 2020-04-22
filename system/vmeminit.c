/* vmeminit.c - memory bounds and free list init */

#include <xinu.h>

/* Memory bounds */

void	*minvheap;		/* Start of heap			*/
void	*maxvheap;		/* Highest valid heap address		*/

/*------------------------------------------------------------------------
 * vmeminit - initialize memory bounds and the free virtual memory list
 *------------------------------------------------------------------------
 */
void	vmeminit(void) {

	struct	vmemblk	*vmemptr;	/* Ptr to memory block		*/

	/* Initialize the free list */
	vmemptr = &vmemlist;
	vmemptr->mnext = (struct vmemblk *)NULL;
	vmemptr->mlength = 0;

	/* Initialize the memory counters */
	/*    Heap starts at the end of Xinu image */
	minvheap = (void *)(4096*4096);
	maxvheap = minvheap;

	maxvheap = (void *)(4096*589824); //589824 3072+4096

	vmemptr->mlength = (uint32)maxvheap - (uint32)minvheap;
	vmemptr->mnext = (struct vmemblk *)minvheap;

	vmemptr = vmemptr->mnext;
	vmemptr->mnext = NULL;
	vmemptr->mlength = (uint32)maxvheap - (uint32)minvheap;
}
