#pragma once

#define MANUAL_RESET TRUE
#define AUTO_RESET FALSE
#include "data_structures/pfn.h"
#include "data_structures/pagefile.h"
#include "macros.h"
// Globals
PULONG_PTR VA_space_start;
CRITICAL_SECTION zero_list_lock;
CRITICAL_SECTION active_list_lock;
CRITICAL_SECTION free_list_lock;
CRITICAL_SECTION modified_list_lock;
CRITICAL_SECTION standby_list_lock;
LIST_ENTRY active_list_head;
LIST_ENTRY zero_list_head;
LIST_ENTRY free_list_head;
LIST_ENTRY modified_list_head;
LIST_ENTRY standby_list_head;
PULONG_PTR scratch_va_start;
HANDLE initiate_trimming_event;
HANDLE initiate_writing_event;
HANDLE system_exit_event;

PPFN pfn_array;
void initialize_pte_globals(PULONG_PTR scratch_va_start, PULONG_PTR VA_space_start, PPFN pfn_array,
    LIST_ENTRY active_list_head, LIST_ENTRY free_list_head, LIST_ENTRY zero_list_head, LIST_ENTRY modified_list_head,
    LIST_ENTRY standby_list_head);

void batch_zero_trim();

void batch_free_trim();

void trim_active_to_free(PPFN page_pfn, ULONG_PTR frame_number);

void trim_active_to_zero(PPFN page_pfn, ULONG_PTR frame_number);
void write_page_to_disk(PPFN page_pfn, ULONG_PTR frame_number);

void retrieve_page_from_disk(PPTE PTE_location, ULONG_PTR frame_number);

void trim_active_to_modified();
void trim_pages_thread();
void trim_modified_to_standby();
void soft_fault_standby(PPFN page_pfn);
void soft_fault_modified(PPFN page_pfn);
void unmap_page(PULONG_PTR old_va);
void write_pages_thread(void);
