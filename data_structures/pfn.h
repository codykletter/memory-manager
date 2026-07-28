#pragma once

#include "pte.h"

#define PFN_FREE 0
#define PFN_ACTIVE 1
#define PFN_MODIFIED 2
#define PFN_STANDBY 3
#define PFN_ZERO 4

typedef struct {
    LIST_ENTRY entry;
    CRITICAL_SECTION pfn_lock;
    PPTE pte;
    ULONG_PTR disk_slot_index;
    int state;
} PFN, *PPFN;

void create_zeroed_pfn(PPFN ppfn);
ULONG_PTR calculate_page_number(PPFN ppfn, PPFN PFN_array_start);
void set_pfn_free(PPFN ppfn);
void set_pfn_active(PPFN ppfn);
void acquire_pfn_lock(PPFN ppfn);
void release_pfn_lock(PPFN ppfn);