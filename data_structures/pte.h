#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#pragma once

#define FRAME_NUMBER_BITS       40
#define STATE_BITS              2
#define PAGE_SIZE 4096

#define PTE_INVALID             0
#define PTE_VALID               1
#define PTE_IN_TRANSITION       0
#define PTE_ON_DISK             1

typedef struct {
    UINT64 valid : 1;
    UINT64 frame_number : FRAME_NUMBER_BITS;
    UINT64 reserved : (64 - 1 - FRAME_NUMBER_BITS);
} RAM_PTE, *PRAM_PTE;

typedef struct {
    UINT64 valid : 1;
    UINT64 status : 1;
    UINT64 disk_slot : FRAME_NUMBER_BITS;
    UINT64 reserved : (64 - STATE_BITS - FRAME_NUMBER_BITS);
} DISK_PTE, *PDISK_PTE;

typedef struct
{
    union
    {
        RAM_PTE ram_pte;
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
PULONG_PTR find_VA_from_PTE(PPTE pte, PULONG_PTR VA_space_start);
