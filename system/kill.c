/* kill.c - kill */

#include <xinu.h>

/*------------------------------------------------------------------------
 *  kill  -  Kill a process and remove it from the system
 *------------------------------------------------------------------------
 */
syscall	kill(
	  pid32		pid		/* ID of process to kill	*/
	)
{
	intmask	mask;			/* Saved interrupt mask		*/
	struct	procent *prptr;		/* Ptr to process' table entry	*/
	int32	i,j;			/* Index into descriptors	*/
	uint32      *haddr;     /* Heap address */
    //uint32* frame;
	//uint32 pagenum;
	int pdindex, ptindex;
	int store;
	int offset;

	mask = disable();
	if (isbadpid(pid) || (pid == NULLPROC)
	    || ((prptr = &proctab[pid])->prstate) == PR_FREE) {
		restore(mask);
		return SYSERR;
	}
	//kprintf("KILL : %d\n", pid);
	if (--prcount <= 1) {		/* Last user process completes	*/
		xdone();
	}
	
	send(prptr->prparent, pid);
	for (i=0; i<3; i++) {
		close(prptr->prdesc[i]);
	}

    // Lab3 TODO. Free frames as a process gets killed.
	// frames holding process's pages be written back to the backing store and be freed
    for (int i=0;i<NFRAMES;i++) {
		if(ipttab[i].pid == pid) {
			haddr = (uint32 *)(ipttab[i].vpn * 0x1000);
			bsm_lookup(pid, haddr, &store, &offset);
			write_bs((char *)(0x00400000+i*4096), store, offset);
			vfreemem((char *)haddr, 0x1000);//pagenum * 0x1000);
			// release all frames
			ipttab[i].pid = -1;
			ipttab[i].vpn = 0;
			ipttab[i].refcnt = 0;
			uint32 * entry;
			entry = (uint32 *)(0x00400000 + i * 0x1000);
			hook_ptable_delete(*entry);
			for ( j = 0; j < 1024; j++)
			{
				*entry = NULL;
			}
			
		}
	}
    for(int i=0;i<=MAX_ID;i++) {
		if(bsmtab[i].pid == pid) {
			// return the backing store
			bsmtab[i].pid = -1;
			bsmtab[i].vpage = 0;
			bsmtab[i].npages = 0;
			deallocate_bs(i);
		}
	}
    
    // 
	
	freestk(prptr->prstkbase, prptr->prstklen);

	switch (prptr->prstate) {
	case PR_CURR:
		prptr->prstate = PR_FREE;	/* Suicide */
		resched();

	case PR_SLEEP:
	case PR_RECTIM:
		unsleep(pid);
		prptr->prstate = PR_FREE;
		break;

	case PR_WAIT:
		semtab[prptr->prsem].scount++;
		/* Fall through */

	case PR_READY:
		getitem(pid);		/* Remove from queue */
		/* Fall through */

	default:
		prptr->prstate = PR_FREE;
	}

	restore(mask);
	return OK;
}
