#include "pte.h"

#define BYTES_OF_PTE 8

extern PPTE page_table_start;
void create_all_PTEs(ULONG_PTR number_of_PTEs) {
    page_table_start = malloc(BYTES_OF_PTE * number_of_PTEs);
    if (page_table_start == NULL) {
        printf("couldn't reserve memory");
        DebugBreak();
    }
    memset(page_table_start, 0, BYTES_OF_PTE * number_of_PTEs);
}

void set_PTE_to_valid(PPTE pte, ULONG_PTR frame_number) {
    pte->frame_number = frame_number;
    pte->valid = PTE_VALID;
}

void set_PTE_to_invalid(PPTE pte) {
    pte->valid = PTE_INVALID;
}

PPTE find_PTE_location(PULONG_PTR arbitrary_va, PULONG_PTR VA_space_start) {
    ULONG_PTR pte_index = ((ULONG_PTR) arbitrary_va - (ULONG_PTR) VA_space_start) / PAGE_SIZE;
    return page_table_start + pte_index;
}