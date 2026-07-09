#include "pte.h"

#define BYTES_OF_PTE 8
// Globals
CRITICAL_SECTION pte_lock;

extern PPTE page_table_start;
void create_all_PTEs(ULONG_PTR number_of_PTEs) {
    InitializeCriticalSection(&pte_lock);
    page_table_start = malloc(BYTES_OF_PTE * number_of_PTEs);
    if (page_table_start == NULL) {
        printf("couldn't reserve memory");
        DebugBreak();
    }
    memset(page_table_start, 0, BYTES_OF_PTE * number_of_PTEs);
}

void acquire_PTE_lock() {
    EnterCriticalSection(&pte_lock);
}
void release_PTE_lock() {
    LeaveCriticalSection(&pte_lock);
}
void set_PTE_to_valid(PPTE pte, ULONG_PTR frame_number) {
    pte->ram_pte.frame_number = frame_number;
    pte->ram_pte.valid = PTE_VALID;
}

void set_PTE_to_invalid(PPTE pte) {
    pte->ram_pte.valid = PTE_INVALID;
}

void set_PTE_to_disk(PPTE pte, ULONG_PTR disk_slot) {
    pte->entire_field = 0;
    pte->disk_pte.disk_slot = disk_slot;
    pte->disk_pte.status = PTE_ON_DISK;
}

PPTE find_PTE_location(PULONG_PTR arbitrary_va, PULONG_PTR VA_space_start) {
    ULONG_PTR pte_index = ((ULONG_PTR) arbitrary_va - (ULONG_PTR) VA_space_start) / PAGE_SIZE;
    return page_table_start + pte_index;
}

PULONG_PTR find_VA_from_PTE(PPTE pte, PULONG_PTR VA_space_start) {
    ULONG_PTR pte_index = ((ULONG_PTR) pte - (ULONG_PTR) page_table_start) / BYTES_OF_PTE;
    return (PULONG_PTR) ((ULONG_PTR) VA_space_start + pte_index * PAGE_SIZE);
}