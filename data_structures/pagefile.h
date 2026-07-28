#pragma once

#include "../macros.h"
#include "pte.h"

#define NUMBER_OF_DISK_PAGES 4096

PVOID disk_base;
PBOOL disk_slot_in_use;
CRITICAL_SECTION disk_lock;
void create_paging_file(void);
ULONG_PTR find_free_disk_slot(void);
void free_disk_slot(ULONG_PTR disk_slot_address);