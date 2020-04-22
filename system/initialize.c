/* initialize.c - nulluser, sysinit, sizmem */

/* Handle system initialization and become the null process */
#if XDEBUG
#define XDEBUG_KPRINTF(...) kprintf(__VA_ARGS__)
#else
#define XDEBUG_KPRINTF(...)
#endif

#include <xinu.h>
#include <string.h>
#include <lab3.h>

extern	void	start(void);	/* Start of Xinu code			*/
extern	void	*_end;		/* End of Xinu code			*/

/* Function prototypes */

extern	void main(void);	/* Main is the first process created	*/
extern	void xdone(void);	/* System "shutdown" procedure		*/
static	void sysinit(); 	/* Internal system initialization	*/
extern	void meminit(void);	/* Initializes the free memory list	*/
extern  void vmeminit(void); /* Initialize the free virtual memory list */

/* Lab3. initializes data structures and necessary set ups for paging */
static	void initialize_paging();
unsigned long tmp;
/* Declarations of major kernel variables */

struct	procent	proctab[NPROC];	/* Process table			*/
struct	sentry	semtab[NSEM];	/* Semaphore table			*/
pd_t pdtab[NPROC];
pd_t* pdtable[NPROC];
// pt_t ptab[NFRAMES];
struct ipt ipttab[NFRAMES];
struct bsm bsmtab[MAX_BS_ENTRIES];
struct	memblk	memlist;	/* List of free memory blocks		*/
struct  vmemblk vmemlist;    /* list of free virtual memory blocks */
struct fqueue* framelist; 
/* Lab3. frames metadata handling */
frame_md_t frame_md;

/* Active system status */

int	prcount;		/* Total number of live processes	*/
pid32	currpid;		/* ID of currently executing process	*/

bool8   PAGE_SERVER_STATUS;    /* Indicate the status of the page server */
sid32   bs_init_sem;

/*------------------------------------------------------------------------
 * nulluser - initialize the system and become the null process
 *
 * Note: execution begins here after the C run-time environment has been
 * established.  Interrupts are initially DISABLED, and must eventually
 * be enabled explicitly.  The code turns itself into the null process
 * after initialization.  Because it must always remain ready to execute,
 * the null process cannot execute code that might cause it to be
 * suspended, wait for a semaphore, put to sleep, or exit.  In
 * particular, the code must not perform I/O except for polled versions
 * such as kprintf.
 *------------------------------------------------------------------------
 */

void	nulluser()
{
	struct	memblk	*memptr;	/* Ptr to memory block		*/
	uint32	free_mem;		/* Total amount of free memory	*/
    struct	vmemblk	*vmemptr;	/* Ptr to virtual memory block		*/
	/* Initialize the system */

	sysinit();

  // Lab3
	initialize_paging();

	kprintf("\n\r%s\n\n\r", VERSION);

	/* Output Xinu memory layout */
	free_mem = 0;
	for (memptr = memlist.mnext; memptr != NULL;
						memptr = memptr->mnext) {
		free_mem += memptr->mlength;
	}
	kprintf("%10d bytes of free memory.  Free list:\n", free_mem);
	for (memptr=memlist.mnext; memptr!=NULL;memptr = memptr->mnext) {
	    kprintf("           [0x%08X to 0x%08X]\r\n",
		(uint32)memptr, ((uint32)memptr) + memptr->mlength - 1);
	}

    // for (vmemptr = vmemlist.mnext; vmemptr != NULL;
	// 					vmemptr = vmemptr->mnext) {
	// 	free_mem += memptr->mlength;
	// }
	// kprintf("%10d bytes of free memory.  Free list:\n", free_mem);
	for (vmemptr=vmemlist.mnext; vmemptr!=NULL;vmemptr = vmemptr->mnext) {
	    kprintf("           [0x%08X to 0x%08X]\r\n",
		(uint32) vmemptr, ((uint32) vmemptr) + vmemptr->mlength - 1);
	}

	kprintf("%10d bytes of Xinu code.\n",
		(uint32)&etext - (uint32)&text);
	kprintf("           [0x%08X to 0x%08X]\n",
		(uint32)&text, (uint32)&etext - 1);
	kprintf("%10d bytes of data.\n",
		(uint32)&ebss - (uint32)&data);
	kprintf("           [0x%08X to 0x%08X]\n\n",
		(uint32)&data, (uint32)&ebss - 1);

	/* Create the RDS process */

	rdstab[0].rd_comproc = create(rdsprocess, RD_STACK, RD_PRIO,
					"rdsproc", 1, &rdstab[0]);
	if(rdstab[0].rd_comproc == SYSERR) {
		panic("Cannot create remote disk process");
	}
	resume(rdstab[0].rd_comproc);

	/* Enable interrupts */

	enable();

	/* Create a process to execute function main() */

	resume (
	   create((void *)main, INITSTK, INITPRIO, "Main process", 0,
           NULL));

	/* Become the Null process (i.e., guarantee that the CPU has	*/
	/*  something to run when no other process is ready to execute)	*/

	while (TRUE) {
		;		/* Do nothing */
	}

}

/*------------------------------------------------------------------------
 *
 * sysinit  -  Initialize all Xinu data structures and devices
 *
 *------------------------------------------------------------------------
 */
static	void	sysinit()
{
	int32	i;
	struct	procent	*prptr;		/* Ptr to process table entry	*/
	struct	sentry	*semptr;	/* Ptr to semaphore table entry	*/

	/* Platform Specific Initialization */

	platinit();

	/* Initialize the interrupt vectors */

	initevec();

	/* Initialize free memory list */

	meminit();

	/* Initialize free virtual memory list */

	vmeminit();

	/* Initialize system variables */

	/* Count the Null process as the first process in the system */

	prcount = 1;

	/* Scheduling is not currently blocked */

	Defer.ndefers = 0;

	/* Initialize process table entries free */

	for (i = 0; i < NPROC; i++) {
		prptr = &proctab[i];
		prptr->prstate = PR_FREE;
		prptr->prname[0] = NULLCH;
		prptr->prstkbase = NULL;
		prptr->prprio = 0;
	}

	/* Initialize the Null process entry */

	prptr = &proctab[NULLPROC];
	prptr->prstate = PR_CURR;
	prptr->prprio = 0;
	strncpy(prptr->prname, "prnull", 7);
	prptr->prstkbase = getstk(NULLSTK);
	prptr->prstklen = NULLSTK;
	prptr->prstkptr = 0;
	currpid = NULLPROC;

	/* Initialize semaphores */

	for (i = 0; i < NSEM; i++) {
		semptr = &semtab[i];
		semptr->sstate = S_FREE;
		semptr->scount = 0;
		semptr->squeue = newqueue();
	}

	/* Initialize buffer pools */

	bufinit();

	/* Create a ready list for processes */

	readylist = newqueue();

	/* Create a frame list */

	framelist = createQueue();

	/* Initialize the real time clock */

	clkinit();

	for (i = 0; i < NDEVS; i++) {
		init(i);
	}

	PAGE_SERVER_STATUS = PAGE_SERVER_INACTIVE;
	bs_init_sem = semcreate(1);

	return;
}

static void initialize_paging()
{
	pd_t        pdentry;   /* Page table directory entry */
	pt_t        ptentry;   /* Page table entry */
	int i;
	unsigned int pdindex, ptindex;
	unsigned long tm;
	pd_t* ptr;
	pt_t* tptr;
	kprintf("Initializing...\n");
	// int i;
	// void * addr = 0x0;
	/* LAB3 TODO */
    /* Initialize data structures: pdtab,  */
	for(i=0; i<NPROC; i++) {
		pdtable[i] = NULL;
	}
    // for(i=0; i<NPROC; i++) {
	// 	pdentry = pdtab[i];
	// 	pdentry.pd_pres = 0;
	// 	pdentry.pd_write = 0;
	// 	pdentry.pd_base = 0;
	// }
	for(i=0;i<NFRAMES;i++) {
		// ptentry = ptab[i];
		// ptentry.pt_pres = 0;
	    // ptentry.pt_write = 0;
	    // ptentry.pt_base = 0;
		ipttab[i].pid = -1;
		ipttab[i].vpn = 0;
		ipttab[i].refcnt = 0;
	}
	for(i=0;i<MAX_BS_ENTRIES;i++) {
		bsmtab[i].pid = -1;
		bsmtab[i].store = i;
		bsmtab[i].vpage = 0;
		bsmtab[i].npages = 0;
	}
	
	/* Allocate and initialize a page directory for the null process */
	pdindex = getfreeframe();
	pdtable[0] = (pd_t *)(0x00400000 + pdindex * 0x1000);
	ptr = pdtable[0];
	pdentry.pd_pres = 1;
	pdentry.pd_write = 1;
    ipttab[pdindex].pid = 0;
	ipttab[pdindex].vpn = pdindex;
	/* 5 Globale Page Tables */
	// 0-255
	ptindex = getfreeframe();
	pdentry.pd_base = (0x00400000 + ptindex * 0x1000) >> 12;
	*ptr = pdentry;
	tptr = (pt_t *)(0x00400000 + ptindex * 0x1000);
	ipttab[ptindex].pid = 0;
	ipttab[ptindex].vpn = 0;
	ptentry.pt_pres = 1;
	ptentry.pt_write = 1;
	//ptentry.pt_base = (unsigned int)0x0;
	*tptr = ptentry;
	idpaging((uint32*)tptr, 0x0+0x1000, 4096*255);

	// 256-325
	ptindex = getfreeframe();
	pdentry.pd_base = (0x00400000 + ptindex * 0x1000) >> 12;
	ptr++;
	*ptr = pdentry;
	tptr = (pt_t*)(0x00400000 + ptindex * 0x1000);
	ipttab[ptindex].pid = 0;
	ipttab[ptindex].vpn = 256;
	ptentry.pt_pres = 1;
	ptentry.pt_write = 1;
	// ptentry.pt_base = (unsigned int)0x00100000;
	*tptr = ptentry;
	idpaging((uint32*)tptr, 0x00100000+0x1000, 4096*69);

    // 326-1023
	ptindex = getfreeframe();
	pdentry.pd_base = (0x00400000 + ptindex * 0x1000) >> 12;
	ptr++;
	*ptr = pdentry;
	tptr = (pt_t *) (0x00400000 + ptindex * 0x1000);
	ipttab[ptindex].pid = 0;
	ipttab[ptindex].vpn = 326;
	ptentry.pt_pres = 1;
	ptentry.pt_write = 1;
	// ptentry.pt_base = (unsigned int)0x00146000;
	*tptr = ptentry;
	idpaging((uint32*)tptr, 0x00146000+0x1000, 4096*697);

    // 1024-4095
    ptindex = getfreeframe();
	pdentry.pd_base = (0x00400000 + ptindex * 0x1000) >> 12;
	ptr++;
	*ptr = pdentry;
	tptr = (pt_t*) (0x00400000 + ptindex * 0x1000);
	ipttab[ptindex].pid = 0;
	ipttab[ptindex].vpn = 1024;
	ptentry.pt_pres = 1;
	ptentry.pt_write = 1;
	// ptentry.pt_base = (unsigned int)0x00400000;
	*tptr = ptentry;
	idpaging((uint32*)tptr, 0x00400000+0x1000, 4096*3071);

	// 1024-4095
    ptindex = getfreeframe();
	pdentry.pd_base = (0x00400000 + ptindex * 0x1000) >> 12;
	ptr = ptr+(0x90000000>>22);
	*ptr = pdentry;
	tptr = (pt_t*) (0x00400000 + ptindex * 0x1000);
	ipttab[ptindex].pid = 0;
	ipttab[ptindex].vpn = 1024;
	ptentry.pt_pres = 1;
	ptentry.pt_write = 1;
	// ptentry.pt_base = (unsigned int)0x90000000;
	*tptr = ptentry;
	idpaging((uint32*)tptr, 0x90000000+0x1000, 4096*1023);
	/* set PDBR register ; install the page fault interrupt service routine; enable paging */
	pdentry = *pdtable[0];
	tm = (unsigned long)(pdentry.pd_base << 12);
	tmp = tm;
	loadPageDirectory(tmp);
	kprintf("Enable paging ... \n");
	enablePaging();
	// enablePaging();
	kprintf("Paging enabled. \n");
	return;
}

void idpaging(uint32 *first_pte, unsigned int from, int size) {
    from = from & 0xfffff000; // discard bits we don't want 1mb 3 0s
    for(; size>0; from += 4096, size -= 4096, first_pte++){
       *first_pte = from | 1;     // mark page present. 1
    }
}

int32	stop(char *s)
{
	kprintf("%s\n", s);
	kprintf("looping... press reset\n");
	while(1)
		/* Empty */;
}

int32	delay(int n)
{
	DELAY(n);
	return OK;
}
