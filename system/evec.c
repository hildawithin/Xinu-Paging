/* evec.c - initevec, doevec */

#include <xinu.h>
#include <stdio.h> 
#include <stdlib.h> 

#if XDEBUG
#define XDEBUG_KPRINTF(...) kprintf(__VA_ARGS__)
#else
#define XDEBUG_KPRINTF(...)
#endif
/* Interrupt Descriptor */

struct __attribute__ ((__packed__)) idt {
	unsigned short	igd_loffset;
	unsigned short	igd_segsel;
	unsigned int	igd_rsvd : 5;
	unsigned int	igd_mbz : 3;
	unsigned int	igd_type : 5;
	unsigned int	igd_dpl : 2;
	unsigned int	igd_present : 1;
	unsigned short	igd_hoffset;
};

/* Note:
 *  Global girmask is used as a mask for interrupts that don't have a
 *  handler set. disable() & restore() are OR-ed with it to get the
 *  mask actually used.
 */
uint16	girmask;

#define	IMR1	0x21		/* Interrupt Mask Register #1		*/
#define	IMR2	0xA1		/* Interrupt Mask Register #2		*/

#define	ICU1	0x20		/* I/O port address, 8259A #1		*/
#define	ICU2	0xA0		/* I/O port address, 8258A #2		*/

#define	OCR	ICU1		/* Operation Command Register		*/
#define	IMR	(ICU1+1)	/* Interrupt Mask Register		*/

#define	EOI	0x20		/* non-specific end of interrupt	*/

#define NID		48	/* Number of interrupt descriptors	*/
#define	IGDT_TRAPG	15	/* Trap Gate				*/

void	setirmask(void);	/* Set interrupt mask			*/
extern uint32 * globalclock;
extern	struct	idt idt[NID];	/* Interrupt descriptor table		*/
extern	long	defevec[];	/* Default exception vector		*/
extern	int	lidt();		/* Load the interrupt descriptor table	*/

/*------------------------------------------------------------------------
 * initevec  -  Initialize exception vectors to a default handler
 *------------------------------------------------------------------------
 */
int32	initevec()
{
	int	i;

	girmask = 0;	/* Until vectors initialized */

	for (i=0; i<NID; ++i) {
		set_evec(i, (long)defevec[i]);	
	}
    /* Set page fault handler */
    set_evec(14, (long)pfhandler);  /* Assign external handler interrupt to int 40 */

	/*
	 * "girmask" masks all (bus) interrupts with the default handler.
	 * initially, all, then cleared as handlers are set via set_evec()
	 */
	girmask = 0xfffb;	/* Leave bit 2 enabled for IC cascade */

	lidt();
	
	/* Initialize the 8259A interrupt controllers */
	
	/* Master device */
	outb(ICU1, 0x11);	/* ICW1: icw4 needed		*/
	outb(ICU1+1, 0x20);	/* ICW2: base ivec 32		*/
	outb(ICU1+1, 0x4);	/* ICW3: cascade on irq2	*/
	outb(ICU1+1, 0x1);	/* ICW4: buf. master, 808x mode */
	outb(ICU1, 0xb);	/* OCW3: set ISR on read	*/

	/* Slave device */
	outb(ICU2, 0x11);	/* ICW1: icw4 needed		*/
	outb(ICU2+1, 0x28);	/* ICW2: base ivec 40		*/
	outb(ICU2+1, 0x2);	/* ICW3: slave on irq2		*/
	outb(ICU2+1, 0xb);	/* ICW4: buf. slave, 808x mode	*/
	outb(ICU2, 0xb);	/* OCW3: set ISR on read	*/

	setirmask();
	
        return OK;
}

/*------------------------------------------------------------------------
 * set_evec  -  Set exception vector to point to an exception handler
 *------------------------------------------------------------------------
 */
int32	set_evec(uint32 xnum, uint32 handler)
{
	struct	idt	*pidt;

	pidt = &idt[xnum];
	pidt->igd_loffset = handler;
	pidt->igd_segsel = 0x8;		/* Kernel code segment */
	pidt->igd_mbz = 0;
	pidt->igd_type = IGDT_TRAPG;
	pidt->igd_dpl = 0;
	pidt->igd_present = 1;
	pidt->igd_hoffset = handler >> 16;

	if (xnum > 31 && xnum < 48) {
		/* Enable the interrupt in the global IR mask */
		xnum -= 32;
		girmask &= ~(1<<xnum);
		setirmask();	/* Pass it to the hardware */
	}
        return OK;
}

/*------------------------------------------------------------------------
 * setirmask  -  Set the interrupt mask in the controller
 *------------------------------------------------------------------------
 */
void	setirmask(void)
{
	if (girmask == 0) {	/* Skip until girmask initialized */
		return;
	}
	outb(IMR1, girmask&0xff);
	outb(IMR2, (girmask>>8)&0xff);
	return;
}

char *inames[] = {
	"divided by zero",
	"debug exception",
	"NMI interrupt",
	"breakpoint",
	"overflow",
	"bounds check failed",
	"invalid opcode",
	"coprocessor not available",
	"double fault",
	"coprocessor segment overrun",
	"invalid TSS",
	"segment not present",
	"stack fault",
	"general protection violation",
	"page fault",
	"coprocessor error"
};

/*------------------------------------------------------------------------
 * pfhandler  -  Handle page faults
 *------------------------------------------------------------------------
 */
void	pfhandler (
		int32	inum,		/* Interrupt number	*/
		long	*savedsp	/* Saved stack pointer	*/
		)
{
	long	*regs;	/* Pointer to saved regs*/
	intmask mask;	/* Saved interrupt mask	*/
    pd_t *pd;
	pd_t pdentry;
	uint32* haddr;
	pt_t * pt;
	pt_t ptentry;
	uint32 vp;
	pid32 pid;
	pfcnt++;
	// struct bsm bsmentry;
	int p,q, ptindex, store, offset;
	/*void		*funcaddr;	 Address of the function	*/
	/* Sanity check on interrupt number */

	if( inum != 14 ) {
		return;
	}
    mask = disable();

	regs = savedsp;
	savedsp = regs + 7;
    haddr = (uint32*)*savedsp;
	/* 1. get the faulted address a
	2. vp = virtual page number 
	3. pd = current page directory pointer
	4. check if a is valid. if not print an error message and kill the process
	5. p be the upper 10 bits, q be the next 10 bits [21:12]
	6. pt = pth page table pointer. if not present, obtain a frame and initialize it
	7. bring in the faulted page: 1) backing store map -> store s + page offset o (with vp information)
	                              2) invert page table -> increments the reference count of the frame holding pt
								  3) obtain a free frame f
								  4) copy the page o of store s to f
								  5) update pt to mark the appropriate entry as present and set other fields. address points to frame f*/
	
	//check if haddr is valid.
    // if((uint32)*haddr >  (uint32)*maxvheap || (uint32)*haddr < (uint32)*minvheap) {
	// 	XDEBUG_KPRINTF("Virtual address not valid. \n\n");
	// 	restore(mask);
	// 	return SYSERR;
	// }
	pid = getpid();
    pd = pdtable[pid];
	vp = (uint32)haddr/0x1000; // virtual page number
    p = (uint32)haddr >> 22;
	q = (uint32)haddr >> 12 & 0x03ff;
	pd = pd + p;
	pdentry = *pd;
	pt = (pt_t*)(pdentry.pd_base << 12);
	pt = pt + q;
	ptentry = *pt;
    
	// check if pt is present
	if(ptentry.pt_pres != 1) {
		// bring in the faulted page
	    bsm_lookup(pid, haddr, &store, &offset);
        // increment the reference count of the frame holding pt
	    ptindex = getfreeframe();
		ipttab[ptindex].refcnt++;
	    read_bs((char*)ptindex, store, offset);
		ptentry.pt_write = 1;
		ptentry.pt_pres = 1;
		ptentry.pt_base = (unsigned long)(0x00400000+ptindex*4096);//(store*200+offset);
		*pt = ptentry;
	} 
	
    hook_pfault(pid, haddr, vp, ptindex);
	/* Acknowledge interrupt in local APIC */
    
	//lapic->eoi = 0;
	restore(mask);
}

/*------------------------------------------------------------------------
 * trap  -  print debugging info when a trap occurrs
 *------------------------------------------------------------------------
*/
void	trap (
	int	inum,	/* Interrupt number	*/
	long	*sp	/* Saved stack pointer	*/
	)
{
	intmask mask;	/* Saved interrupt mask	*/
	long	*regs;	/* Pointer to saved regs*/

	/* Disable interrupts */

	mask = disable();

	/* Get the location of saved registers */

	regs = sp;

	/* Print the trap message */

	kprintf("Xinu trap!\n");
	if (inum < 16) {
		kprintf("exception %d (%s) currpid %d (%s)\n", inum,
			inames[inum], currpid, proctab[currpid].prname);
	} else {
		kprintf("exception %d currpid %d (%s)\n", inum, currpid,
			proctab[currpid].prname);
	}

	/* Adjust stack pointer to get debugging information 	*/
	/* 8 registers and 1 %ebp pushed in Xint		*/

	sp = regs + 1 + 8;

	/* Print the debugging information related to interrupt	*/

	if (inum == 8 || (inum >= 10 && inum <= 14)) {
		kprintf("error code %08x (%u)\n", *sp, *sp);
		sp++;
	}
	kprintf("CS %X eip %X\n", *(sp + 1), *sp);
	kprintf("eflags %X\n", *(sp + 2));

	/* Dump the register values */

	sp = regs + 7;

	kprintf("register dump:\n");
	kprintf("eax %08X (%u)\n", *sp, *sp);
	sp--;
	kprintf("ecx %08X (%u)\n", *sp, *sp);
	sp--;
	kprintf("edx %08X (%u)\n", *sp, *sp);
	sp--;
	kprintf("ebx %08X (%u)\n", *sp, *sp);
	sp--;
	kprintf("esp %08X (%u)\n", *sp, *sp);
	sp--;
	kprintf("ebp %08X (%u)\n", *sp, *sp);
	sp--;
	kprintf("esi %08X (%u)\n", *sp, *sp);
	sp--;
	kprintf("edi %08X (%u)\n", *sp, *sp);
	sp--;

	panic("Trap processing complete...\n");
	restore(mask);
}
