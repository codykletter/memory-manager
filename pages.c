#include "pages.h"

#define ZERO_BATCH_TRIM_NUM 10
#define FREE_BATCH_TRIM_NUM 10
#define ACTIVE_EVENT_INDEX 0
#define EXIT_EVENT_INDEX 1

// TODO: LOCK HIERARCHY: 1. PTE 2. PFN 3. LIST LOCK (prevents soft fault trimming)

unsigned i;

// TODO: Make sure this is proper with multithreading and modified/standby!
void batch_zero_trim() {
    for (i = 0; i < ZERO_BATCH_TRIM_NUM; i++) {
        // Get oldest active page and trim to zero
        PPFN page_pfn = (PPFN) RemoveHeadList(&active_list_head);
        ULONG_PTR frame_number = calculate_page_number(page_pfn, pfn_array);
        trim_active_to_zero(page_pfn, frame_number);
        // Add page to zero list once done
        InsertTailList(&zero_list_head, (PLIST_ENTRY) page_pfn);
    }
}
// TODO: Make sure this is proper with multithreading!
void batch_free_trim() {
    for (i = 0; i < FREE_BATCH_TRIM_NUM; i++) {
        // Get oldest active page and trim to free
        PPFN page_pfn = (PPFN) RemoveHeadList(&active_list_head);
        ULONG_PTR frame_number = calculate_page_number(page_pfn, pfn_array);
        trim_active_to_free(page_pfn, frame_number);
        // Add page to free list once done
        InsertTailList(&free_list_head, (PLIST_ENTRY) page_pfn);
    }
}
// TODO: consider transition from active list to trimming through page table walks??
void trim_active_to_zero(PPFN page_pfn, ULONG_PTR frame_number) {
    // Get the VA of our victim page that will be written to disk
    PULONG_PTR old_va = get_VA_from_PTE(page_pfn->pte, VA_space_start);
    // Make the previous VA point to nothing
    if (!MapUserPhysicalPages(old_va, 1, NULL)) {
        DebugBreak();
    }
    write_page_to_disk(page_pfn, frame_number);
    // DIFFERENCE FROM TRIM TO FREE: zero out the page once it's been written
    memset(scratch_va_start, 0, PAGE_SIZE);
    // Make sure kernel VA points to nothing again (job has been done)
    if (!MapUserPhysicalPages(scratch_va_start, 1, NULL)) {
        DebugBreak();
    }
    page_pfn->state = PFN_ZERO;
}
// Note: this function does not set kernel VA start back to null, you must do that after calling function
// (This is so trim active to zero and trim active to free can both use it)
void write_page_to_disk(PPFN page_pfn, ULONG_PTR frame_number) {
    PPTE PTE_location = page_pfn->pte;
    // Wire the kernel VA to the active page so new VA can't access its contents
    if (!MapUserPhysicalPages(scratch_va_start, 1, &frame_number)) {
        DebugBreak();
    }
    // Find a free space on our disk for our old active page
    int disk_slot_index = find_free_disk_slot();
    if (disk_slot_index == -1) {
        DebugBreak();
    }
    // Update disk slot index in PFN metadata
    page_pfn->disk_slot_index = disk_slot_index;
    // Write page to disk (not using it anymore)
    PVOID disk_address = (PVOID) ((ULONG_PTR) disk_base + disk_slot_index * PAGE_SIZE);
    memcpy(disk_address, scratch_va_start, PAGE_SIZE);
    // Update PTE
    set_PTE_to_disk(PTE_location, disk_slot_index);
}
void trim_active_to_free(PPFN page_pfn, ULONG_PTR frame_number) {
    // Get the VA of our victim page that will be written to disk
    PULONG_PTR old_va = get_VA_from_PTE(page_pfn->pte, VA_space_start);
    // Make the previous VA point to nothing
    if (!MapUserPhysicalPages(old_va, 1, NULL)) {
        DebugBreak();
    }
    write_page_to_disk(page_pfn, frame_number);
    // Final step - make sure kernel VA points to nothing again (job has been done)
    if (!MapUserPhysicalPages(scratch_va_start, 1, NULL)) {
        DebugBreak();
    }
    page_pfn->state = PFN_FREE;
}
void retrieve_page_from_disk(PPTE PTE_location, ULONG_PTR frame_number) {
    ULONG_PTR disk_slot_index = PTE_location->disk_pte.disk_slot;
    PVOID disk_address = (PVOID) ((ULONG_PTR) disk_base + disk_slot_index * PAGE_SIZE);
    // Use kernel VA to fully copy data over to free page before giving to user
    if (MapUserPhysicalPages(scratch_va_start, 1, &frame_number) == FALSE) {
        DebugBreak();
    }
    // Copy data to free page and clear data from disk slot
    memcpy(scratch_va_start, disk_address, PAGE_SIZE);
    if (MapUserPhysicalPages(scratch_va_start, 1, NULL) == FALSE) {
        DebugBreak();
    }
    disk_slot_in_use[disk_slot_index] = FALSE;
}


void trim_pages_thread(void) {

    // Create our handles for the wait for multiple objects call in the loop.
    // We wait to trim or to exit.
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = initiate_trimming_event;
    events[EXIT_EVENT_INDEX] = system_exit_event;

    // If the exit flag has been set, then it's time to go!
    while (TRUE) {

        printf("in trimmer");

        if (WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, INFINITE)
            == EXIT_EVENT_INDEX) return;

        trim_active_to_modified();
    }
}

// Trims one page
void trim_active_to_modified() {
    // First acquire PTE, then active list lock
    acquire_PTE_lock();
    EnterCriticalSection(&active_list_lock);
    // If active list is empty, leave
    if (IsListEmpty(&active_list_head)) {
        LeaveCriticalSection(&active_list_lock);
        release_PTE_lock();
        return;
    }
    // Get oldest active page and remove from active list
    PPFN page_pfn = (PPFN) RemoveHeadList(&active_list_head);
    // Safe to unlock active list lock since one PTE lock for all PTEs protects us
    // Writer cannot access page since it's not yet on modified list, soft faulting cannot occur since no shared pages
    LeaveCriticalSection(&active_list_lock);


    PPTE PTE_location = page_pfn->pte;
    // Set valid bit to zero
    PTE_location->ram_pte.valid = PTE_INVALID;
    // Set transition bit to 1
    PTE_location->transition_pte.transition = PTE_IN_TRANSITION;
    PULONG_PTR old_va = get_VA_from_PTE(PTE_location, VA_space_start);
    // Turn off the actual valid bit in the hardware
    unmap_page(old_va);
    // Lock pfn
    EnterCriticalSection(&page_pfn->pfn_lock);
    // Lock modified list
    EnterCriticalSection(&modified_list_lock);
    // Add to modified list
    InsertTailList(&modified_list_head, (PLIST_ENTRY) page_pfn);
    page_pfn->state = PFN_MODIFIED;
    // Unlock
    LeaveCriticalSection(&modified_list_lock);
    LeaveCriticalSection(&page_pfn->pfn_lock);
    release_PTE_lock();
}

void unmap_page(PULONG_PTR old_va) {
    if (!MapUserPhysicalPages(old_va, 1, NULL)) {
        DebugBreak();
    }
}
void write_pages_thread(void) {

    // Create our handles for the wait for multiple objects call in the loop.
    // We wait to trim or to exit.
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = initiate_writing_event;
    events[EXIT_EVENT_INDEX] = system_exit_event;

    // If the exit flag has been set, then it's time to go!
    while (TRUE) {

        if (WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, INFINITE)
            == EXIT_EVENT_INDEX) return;

        trim_modified_to_standby();
    }
}
// Writes only one page to disk
void trim_modified_to_standby() {
    top:
    // Lock modified list
    EnterCriticalSection(&modified_list_lock);
    // If no pages to write to disk, leave
    if (IsListEmpty(&modified_list_head)) {
        LeaveCriticalSection(&modified_list_lock);
        return;
    }
    // Check to see if we will enter deadlock, exit early, otherwise make forward progress
    // Peek at modified list head
    PPFN page_pfn = (PPFN) modified_list_head.Flink;
    // Intentionally going in opposite direction of lock hierarchy, so must back up
    if (TryEnterCriticalSection(&page_pfn->pfn_lock) == FALSE) {
        // Be a good citizen and release modified list lock to prevent deadlock
        // with faulting thread
        LeaveCriticalSection(&modified_list_lock);
        goto top;
        return;
    }
    // Get oldest modified page and remove from modified list
    page_pfn = (PPFN) RemoveHeadList(&modified_list_head);

    // Todo: Could add a field here for disk write in progress, when soft fault
    // Occurs we have to check the value of this field and then if it is set,
    // The field has been rescued, and we have to free up the disk space
    // Need to add the other side change on soft fault thread, if write in progress
    // We're not on a list, and we need to modify the field to represent the change

    LeaveCriticalSection(&modified_list_lock);
    ULONG_PTR frame_number = calculate_page_number(page_pfn, pfn_array);
    // Write the page to disk
    write_page_to_disk(page_pfn, frame_number);
    // Make sure kernel VA points to nothing again (job has been done)
    if (!MapUserPhysicalPages(scratch_va_start, 1, NULL)) {
        DebugBreak();
    }
    EnterCriticalSection(&standby_list_lock);
    // Add to standby list
    InsertTailList(&standby_list_head, (PLIST_ENTRY) page_pfn);
    LeaveCriticalSection(&standby_list_lock);
    page_pfn->state = PFN_STANDBY;

    //Fix this by aquiring a free disk slot and releasing the
    LeaveCriticalSection(&page_pfn->pfn_lock);
}
void soft_fault_standby(PPFN page_pfn) {
    PPTE PTE_location = page_pfn->pte;
    // Get disk address of standby page
    ULONG_PTR disk_slot_index = PTE_location->disk_pte.disk_slot;
    // Now clear it and update metadata so data can be used
    free_disk_slot(disk_slot_index);
    // Lock standby list
    EnterCriticalSection(&standby_list_lock);
    // Remove page from modified list and set to active list
    RemoveEntryList((PLIST_ENTRY) page_pfn);
    LeaveCriticalSection(&standby_list_lock);
    // Now lock active list
    EnterCriticalSection(&active_list_lock);
    InsertTailList(&active_list_head, (PLIST_ENTRY) page_pfn);
    LeaveCriticalSection(&active_list_lock);
    page_pfn->state = PFN_ACTIVE;
    // Set valid bit of PTE back to 1
    page_pfn->pte->ram_pte.valid = PTE_VALID;

}
void soft_fault_modified(PPFN page_pfn) {
    // Lock modified list
    EnterCriticalSection(&modified_list_lock);
    // Remove page from modified list and set to active list
    RemoveEntryList((PLIST_ENTRY) page_pfn);
    LeaveCriticalSection(&modified_list_lock);
    // Now lock active list
    EnterCriticalSection(&active_list_lock);
    InsertTailList(&active_list_head, (PLIST_ENTRY) page_pfn);
    LeaveCriticalSection(&active_list_lock);
    page_pfn->state = PFN_ACTIVE;
    // Set valid bit of PTE back to 1
    page_pfn->pte->ram_pte.valid = PTE_VALID;
}