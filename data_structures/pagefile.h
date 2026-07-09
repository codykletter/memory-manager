#pragma once

#include "../macros.h"
#include "pte.h"

#define NUMBER_OF_DISK_PAGES 4096

PVOID disk_base;
PBOOL disk_slot_in_use;

void create_paging_file(void);
int find_free_disk_slot(void);
