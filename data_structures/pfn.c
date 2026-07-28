#include "pfn.h"
#include "../macros.h"
void create_zeroed_pfn(PPFN ppfn) {
    ppfn->state = PFN_ZERO;
    ppfn->pte = 0;
    InitializeCriticalSection(&ppfn->pfn_lock);
    ppfn->entry.Flink = 0;
    ppfn->entry.Blink = 0;
}
ULONG_PTR calculate_page_number(PPFN ppfn, PPFN pfn_array_start) {
    return (ULONG_PTR) (ppfn - pfn_array_start);
}
void acquire_pfn_lock(PPFN ppfn) {
    EnterCriticalSection(&ppfn->pfn_lock);
}
void release_pfn_lock(PPFN ppfn) {
    LeaveCriticalSection(&ppfn->pfn_lock);
}