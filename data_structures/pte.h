#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#pragma once

#define FRAME_NUMBER_BITS       40
#define STATE_BITS              2
#define PAGE_SIZE 4096

#define PTE_INVALID             0
#define PTE_VALID               1
#define PTE_IN_TRANSITION       1
#define PTE_ON_DISK             0

// Globals
CRITICAL_SECTION pte_lock;

typedef struct {
    UINT64 valid : 1;
    // TODO: This bit is unnecessary, can remove but then have to move over frame number when switching states
    // Will be 0 in ram format
    UINT64 transition : 1;
    UINT64 frame_number : FRAME_NUMBER_BITS;
    UINT64 reserved : (64 - 1 - FRAME_NUMBER_BITS);
} RAM_PTE, *PRAM_PTE;

typedef struct {
    // Valid must be 0
    UINT64 valid : 1;
    // Must be 1 in transition format
    UINT64 transition : 1;
    UINT64 frame_number : FRAME_NUMBER_BITS;
    UINT64 reserved : (64 - 1 - FRAME_NUMBER_BITS);
} TRANSITION_PTE, *PTRANSITION_PTE;

typedef struct {
    // Valid bit must be 0 and transition bit must be 0
    UINT64 valid : 1;
    // This bit tells us do I have a physical page associated with the PTE
    UINT64 transition : 1;
    UINT64 disk_slot : FRAME_NUMBER_BITS;
    UINT64 reserved : (64 - STATE_BITS - FRAME_NUMBER_BITS);
} DISK_PTE, *PDISK_PTE;

typedef struct
{
    union
    {
        RAM_PTE ram_pte;
        TRANSITION_PTE transition_pte;
        DISK_PTE disk_pte;
        ULONG64 entire_field;
    };
} PTE, *PPTE;

PPTE page_table_start;

void set_PTE_to_valid(PPTE pte, ULONG_PTR frame_number);
void set_PTE_to_invalid(PPTE pte);
void set_PTE_to_disk(PPTE pte, ULONG_PTR disk_slot);
void create_all_PTEs(ULONG_PTR number_of_PTEs);
PPTE find_PTE_location(PULONG_PTR arbitrary_va, PULONG_PTR VA_space_start);
PULONG_PTR get_VA_from_PTE(PPTE pte, PULONG_PTR VA_space_start);
void acquire_PTE_lock();
void release_PTE_lock();