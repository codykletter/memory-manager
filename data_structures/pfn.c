#include "pfn.h"
#include "../macros.h"
void create_zeroed_pfn(PPFN ppfn) {
    ppfn->state = PFN_ZERO;
    ppfn->pte = 0;
    ppfn->entry.Flink = 0;
    ppfn->entry.Blink = 0;
}
ULONG_PTR calculate_page_number(PPFN ppfn, PPFN pfn_array_start) {
    return (ULONG_PTR) (ppfn - pfn_array_start);
}