#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#pragma once

#define FRAME_NUMBER_BITS       40
#define STATE_BITS              2
#define PAGE_SIZE 4096

#define PTE_INVALID             0
#define PTE_VALID               1
// #define PTE_IN_TRANSITION       0
// #define PTE_ON_DISK             1

typedef struct {
    UINT64 valid : 1;                       // Valid bit -- 1 indicating PTE is valid
    UINT64 status : 1;                      // 1 bit to encode transition (0) or on disk (1)
    // UINT64 readwrite : 1;                   // Read/Write bit -- 0 for read privileges, 1 for write privileges
    // UINT64 dirty : 1;                       // Dirty bit -- 0 for unmodified, 1 for modified
    // UINT64 accessed : 1;                    // Accessed bit -- indicates if the page has been accessed to track frequency
    UINT64 frame_number : FRAME_NUMBER_BITS;// 40 bits to hold the frame number
    UINT64 reserved : (64 - STATE_BITS - FRAME_NUMBER_BITS); // Remaining bits reserved for later
} PTE, *PPTE;

PPTE page_table_start;
// /*
//  *  Moves an active PTE into the transition state.
//  */
// void set_PTE_to_transition(PPTE pte);

/*
 *  Moves an invalid PTE into the valid state.
 */
void set_PTE_to_valid(PPTE pte, ULONG_PTR frame_number);

/*
 *  Moves an valid PTE into invalid state.
 */
void set_PTE_to_invalid(PPTE pte);

void create_all_PTEs(ULONG_PTR number_of_PTEs);

PPTE find_PTE_location(PULONG_PTR arbitrary_va, PULONG_PTR VA_space_start);