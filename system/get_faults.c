/* get_faults.c - get_faults */
#include <xinu.h>

uint32 pfcnt = 0;
/*---------------------------------------------------------------------------
 *  get_faults  -  Return the number of times the page fault handler has been
 *  called.
 *---------------------------------------------------------------------------
 */
uint32 get_faults() {
  	/* LAB3 TODO */
    return pfcnt;	
}
