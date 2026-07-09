#include "pagefile.h"

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

int find_free_disk_slot(void) {
    for (int i = 0; i < NUMBER_OF_DISK_PAGES; i++) {
        if (disk_slot_in_use[i] == FALSE) {
            disk_slot_in_use[i] = TRUE;
            return i;
        }
    }
    return -1;
}
