/* bsm_lookup.c - bsm_lookup */

#include <xinu.h>

// extern bsmtab[];
/*----------------------------------------------------------------------------
 *  bsm_lookup  -  
 *----------------------------------------------------------------------------
 */

void bsm_lookup(pid32 pid, uint32 * vaddr, int* store, int* offset) {
    int i, j;
    int32* tmp;
    for ( i = 0; i <= MAX_ID; i++)
    {
        tmp = *bsmtab[i].vpage;
        if (bsmtab[i].pid == pid)
        {
            j = ((uint32)*vaddr-(uint32)*tmp)/4096;
            if(j>0 && j<200) {
                *store = bsmtab[i].store;
                *offset = j;
                return;
            }
        }
        
    }
    return (void)SYSERR;
}