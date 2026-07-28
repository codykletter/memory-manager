#include "pagefile.h"
unsigned i;
CRITICAL_SECTION disk_lock;
void create_paging_file(void) {
    disk_base = malloc(NUMBER_OF_DISK_PAGES * PAGE_SIZE);
    if (disk_base == NULL) {
        printf("couldn't reserve memory");
        DebugBreak();
    }
    disk_slot_in_use = malloc(NUMBER_OF_DISK_PAGES * sizeof(BOOL));
    if (disk_slot_in_use == NULL) {
        printf("couldn't reserve memory");
        DebugBreak();
    }
    memset(disk_slot_in_use, 0, NUMBER_OF_DISK_PAGES * sizeof(BOOL));
}

ULONG_PTR find_free_disk_slot(void) {
    EnterCriticalSection(&disk_lock);
    for (i = 0; i < NUMBER_OF_DISK_PAGES; i++) {
        if (disk_slot_in_use[i] == FALSE) {
            disk_slot_in_use[i] = TRUE;
            LeaveCriticalSection(&disk_lock);
            return i;
        }
    }
    LeaveCriticalSection(&disk_lock);
    return -1;
}
void free_disk_slot(ULONG_PTR disk_slot_address) {
    EnterCriticalSection(&disk_lock);
    if (disk_slot_in_use[i] == TRUE) {
        disk_slot_in_use[i] = FALSE;
    }
    else {
        DebugBreak();
    }
    LeaveCriticalSection(&disk_lock);
}

