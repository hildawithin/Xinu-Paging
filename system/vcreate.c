/* vcreate.c - vcreate */

#include <xinu.h>

local	int newpid();

#define	roundew(x)	( (x+3)& ~0x3)

/*----------------------------------------------------------------------------
 *  vcreate  -  Creates a new XINU process. The process's heap will be private
 *  and reside in virtual memory.
 *----------------------------------------------------------------------------
 */
pid32	vcreate(
	  void		*funcaddr,	/* Address of the function	*/
	  uint32	ssize,		/* Stack size in words		*/
		uint32	hsize,		/* Heap size in num of pages */
	  pri16		priority,	/* Process priority > 0		*/
	  char		*name,		/* Name (for debugging)		*/
	  uint32	nargs,		/* Number of args that follow */
	  ...
	)
{
	uint32		savsp, *pushsp;
	intmask 	mask;    	/* Interrupt mask		*/
	pid32		pid;		/* Stores new process id	*/
	struct	procent	*prptr;		/* Pointer to proc. table entry */
	// pd_t        pdentry;   /* Page table directory entry */
	pd_t        pdentry;   /* Page directory entry */
	bsd_t       bsid; /* backing store id */
	struct bsm         bsmentry;
	int		i, pdindex, ptindex, npagetables, pdoff, ptoff;
	int         round;
	uint32      realsize;
	// s * frameid;
	uint32		*a;		/* Points to list of args	*/
	uint32		*saddr;		/* Stack address		*/
	uint32      *haddr;     /* Heap address */
	pd_t* ptr;
	pt_t* tptr;

	mask = disable();
	if (ssize < MINSTK)
		ssize = MINSTK;
	ssize = (uint32) roundew(ssize);
	if (((saddr = (uint32 *)getstk(ssize)) ==
	    (uint32 *)SYSERR ) ||
	    (pid=newpid()) == SYSERR || priority < 1 || hsize > MAX_BS_ENTRIES*MAX_PAGES_PER_BS) {
		restore(mask);
		return SYSERR;
	}
    npagetables = 0;
    for(i = MIN_ID; i <= MIN_ID; i++) {
		if(bstab[i].isallocated == FALSE) { /* Found an unallocated store */
			npagetables += MAX_PAGES_PER_BS;
		}
	}
    // Lab3 TODO. set up page directory, allocate bs etc.
	// set up page directory 
	haddr = (uint32 *) vgetmem(hsize*4096);
	if(haddr==(uint32 *)SYSERR || npagetables<=hsize) {
		restore(mask);
		return SYSERR;
	}
    pdoff = (uint32)haddr >> 22;
	ptoff = (uint32)haddr >> 12 & 0x03ff; // should be 0
	pdindex = getfreeframe();
	pdtable[pid] = (pd_t *)(0x00400000 + pdindex * 0x1000);
	ptr = pdtable[pid];
	ipttab[pdindex].pid = getpid();
	ipttab[pdindex].vpn = (uint32)haddr/0x1000;
    
	pdentry.pd_pres = 1;
	pdentry.pd_write = 1;
	// set up page tables
	// suppose only one page would be enough to accommodate the page table
	// calculate how many page tables are needed
	npagetables = (hsize+ptoff)/0x400 + ((hsize+ptoff)%0x400>0);
	
	// tptr = tptr + ptoff;

    // allocate private heap
	int cnt = 0;
	ptr = ptr + pdoff;
	while(npagetables) {
		cnt++;
		ptindex = getfreeframe();
		tptr = (pt_t *)(0x00400000 + ptindex * 0x1000);
		ipttab[ptindex].pid = getpid();
		ipttab[ptindex].vpn = (uint32)haddr/0x1000 + cnt;//hsize;
		pdentry.pd_base = (0x00400000 + ptindex * 0x1000) >> 12;
		*ptr = pdentry;
		ptr++;
	    round = hsize/MAX_PAGES_PER_BS + (hsize%MAX_PAGES_PER_BS>0);
	    for (i = 0; i < round; i++)
	    {
		    // min
		    if(hsize<=MAX_PAGES_PER_BS) {
			    realsize = hsize;
		    } else {
			    realsize = MAX_PAGES_PER_BS;
		    }
		    bsid = allocate_bs(realsize);
		    bsmentry.pid = pid;
		    bsmentry.vpage = (uint32 *)(haddr+4096*MAX_PAGES_PER_BS*i);
		    bsmentry.npages = realsize;
		    bsmentry.store = bsid;
		    // store in backing store map
		    if(bsmtab[bsid].pid == -1) {
			    bsmtab[bsid] = bsmentry;
		    } else {
			    return (pid32)SYSERR;
		    }
		    hsize = hsize - MAX_PAGES_PER_BS;
	    }
		npagetables--;

	}
	// idpaging(tptr+ptoff, (unsigned int)haddr, 4096*hsize);
    prcount++;
	prptr = &proctab[pid];
	/* Initialize process table entry for new process */
	prptr->prstate = PR_SUSP;	/* Initial state is suspended	*/
	prptr->prprio = priority;
	prptr->prstkbase = (char *)saddr;
	prptr->prstklen = ssize;
	prptr->prname[PNMLEN-1] = NULLCH;
	for (i=0 ; i<PNMLEN-1 && (prptr->prname[i]=name[i])!=NULLCH; i++)
		;
	prptr->prsem = -1;
	prptr->prparent = (pid32)getpid();
	prptr->prhasmsg = FALSE;

	/* Set up stdin, stdout, and stderr descriptors for the shell	*/
	prptr->prdesc[0] = CONSOLE;
	prptr->prdesc[1] = CONSOLE;
	prptr->prdesc[2] = CONSOLE;

	/* Initialize stack as if the process was called		*/

	*saddr = STACKMAGIC;
	savsp = (uint32)saddr;

	/* Push arguments */
	a = (uint32 *)(&nargs + 1);	/* Start of args		*/
	a += nargs -1;			/* Last argument		*/
	for ( ; nargs > 0 ; nargs--)	/* Machine dependent; copy args	*/
		*--saddr = *a--;	/*   onto created process' stack*/
	*--saddr = (long)INITRET;	/* Push on return address	*/

	/* The following entries on the stack must match what ctxsw	*/
	/*   expects a saved process state to contain: ret address,	*/
	/*   ebp, interrupt mask, flags, registerss, and an old SP	*/

	*--saddr = (long)funcaddr;	/* Make the stack look like it's*/
					/*   half-way through a call to	*/
					/*   ctxsw that "returns" to the*/
					/*   new process		*/
	*--saddr = savsp;		/* This will be register ebp	*/
					/*   for process exit		*/
	savsp = (uint32) saddr;		/* Start of frame for ctxsw	*/
	*--saddr = 0x00000200;		/* New process runs with	*/
					/*   interrupts enabled		*/

	/* Basically, the following emulates an x86 "pushal" instruction*/

	*--saddr = 0;			/* %eax */
	*--saddr = 0;			/* %ecx */
	*--saddr = 0;			/* %edx */
	*--saddr = 0;			/* %ebx */
	*--saddr = 0;			/* %esp; value filled in below	*/
	pushsp = saddr;			/* Remember this location	*/
	*--saddr = savsp;		/* %ebp (while finishing ctxsw)	*/
	*--saddr = 0;			/* %esi */
	*--saddr = 0;			/* %edi */
	*pushsp = (unsigned long) (prptr->prstkptr = (char *)saddr);
	
	restore(mask);
	return pid;
}

/*------------------------------------------------------------------------
 *  newpid  -  Obtain a new (free) process ID
 *------------------------------------------------------------------------
 */
local	pid32	newpid(void)
{
	uint32	i;			/* Iterate through all processes*/
	static	pid32 nextpid = 1;	/* Position in table to try or	*/
					/*   one beyond end of table	*/

	/* Check all NPROC slots */

	for (i = 0; i < NPROC; i++) {
		nextpid %= NPROC;	/* Wrap around to beginning */
		if (proctab[nextpid].prstate == PR_FREE) {
			return nextpid++;
		} else {
			nextpid++;
		}
	}
	return (pid32) SYSERR;
}

    // } else {
	// 	ptindex = getfreeframe();
	// 	pdentry.pd_base = 0x00400000 + ptindex * 0x1000;
	    // *ptr = pdentry;
		// ptr++;
		// //tmp = (uint32 *)(0x00400000 + ptindex * 0x1000);
		// ipttab[ptindex].pid = getpid();
		// ipttab[ptindex].vpn = (uint32)haddr/0x1000+i;
		// ptentry.pt_pres = 1;
	    // ptentry.pt_write = 1;

		// ptentry.pt_base = (unsigned int)(bsid);
	    //allocate backup stores, private heap
		// round = 0x400/MAX_PAGES_PER_BS + 1;
		// for(j=0;j<round;j++) {
		// 	if(hsize<=MAX_PAGES_PER_BS) {
		// 	    realsize = hsize;
		//     } else {
		// 	    realsize = MAX_PAGES_PER_BS;
		//     }
		// }
		// *tptr = ptentry;
        // tptr++;