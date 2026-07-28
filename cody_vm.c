#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "macros.h"
#include "data_structures/pfn.h"
#include "pages.h"
#include "data_structures/pagefile.h"

//
// This define enables code that lets us create multiple virtual address
// mappings to a single physical page.  We only/need want this if/when we
// start using reference counts to avoid holding locks while performing
// pagefile I/Os - because otherwise disallowing this makes it easier to
// detect and fix unintended failures to unmap virtual addresses properly.
//

#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 0
#define NUMBER_OF_THREADS       5
#pragma comment(lib, "advapi32.lib")

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
#pragma comment(lib, "onecore.lib")
#endif

#define MB(x)                       ((x) * 1024 * 1024)

//
// This is intentionally a power of two so we can use masking to stay
// within bounds.
//
#define VIRTUAL_ADDRESS_SIZE        MB(16)

#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS        (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))



//
// Deliberately use a physical page pool that is approximately 1% of the
// virtual address space !
//

#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)
#define NUMBER_OF_PTES                              (VIRTUAL_ADDRESS_SIZE / PAGE_SIZE)

ULONG_PTR virtual_address_size_in_unsigned_chunks;
unsigned i;
// TODO AI: create list head structs with a list entry and a lock and count to re-organize. Update list functions to update the counts
VOID initialize_globals(VOID) {
    // Initialize list heads
    InitializeListHead(&active_list_head);
    InitializeListHead(&zero_list_head);
    InitializeListHead(&free_list_head);
    InitializeListHead(&modified_list_head);
    InitializeListHead(&standby_list_head);

    // Initialize locks
    InitializeCriticalSection(&active_list_lock);
    InitializeCriticalSection(&zero_list_lock);
    InitializeCriticalSection(&free_list_lock);
    InitializeCriticalSection(&modified_list_lock);
    InitializeCriticalSection(&standby_list_lock);
    InitializeCriticalSection(&disk_lock);

    create_all_PTEs(NUMBER_OF_PTES);

    // Virtual memory for the kernel
    scratch_va_start = VirtualAlloc (NULL,
                      PAGE_SIZE,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

    if (scratch_va_start == NULL) {
        printf ("full_virtual_memory_test : could not reserve scratch memory %x\n",
                GetLastError ());
        DebugBreak();
    }
}

BOOL
GetPrivilege  (VOID)
{
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privilege [1];
    } Info;

    //
    // This is Windows-specific code to acquire a privilege.
    // Understanding each line of it is not so important for
    // our efforts.
    //

    HANDLE hProcess;
    HANDLE Token;
    BOOL Result;

    //
    // Open the token.
    //

    hProcess = GetCurrentProcess ();

    Result = OpenProcessToken (hProcess,
                               TOKEN_ADJUST_PRIVILEGES,
                               &Token);

    if (Result == FALSE) {
        printf ("Cannot open process token.\n");
        return FALSE;
    }

    //
    // Enable the privilege. 
    //

    Info.Count = 1;
    Info.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

    //
    // Get the LUID.
    //

    Result = LookupPrivilegeValue (NULL,
                                   SE_LOCK_MEMORY_NAME,
                                   &(Info.Privilege[0].Luid));

    if (Result == FALSE) {
        printf ("Cannot get privilege\n");
        return FALSE;
    }

    //
    // Adjust the privilege.
    //

    Result = AdjustTokenPrivileges (Token,
                                    FALSE,
                                    (PTOKEN_PRIVILEGES) &Info,
                                    0,
                                    NULL,
                                    NULL);

    //
    // Check the result.
    //

    if (Result == FALSE) {
        printf ("Cannot adjust token privileges %u\n", GetLastError ());
        return FALSE;
    } 

    if (GetLastError () != ERROR_SUCCESS) {
        printf ("Cannot enable the SE_LOCK_MEMORY_NAME privilege - check local policy\n");
        return FALSE;
    }

    CloseHandle (Token);

    return TRUE;
}



#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
HANDLE
CreateSharedMemorySection (
    VOID
    )
{
    HANDLE section;
    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Create an AWE section.  Later we deposit pages into it and/or
    // return them.
    //

    parameter.Type = MemSectionExtendedParameterUserPhysicalFlags;
    parameter.ULong = 0;

    section = CreateFileMapping2 (INVALID_HANDLE_VALUE,
                                  NULL,
                                  SECTION_MAP_READ | SECTION_MAP_WRITE,
                                  PAGE_READWRITE,
                                  SEC_RESERVE,
                                  0,
                                  NULL,
                                  &parameter,
                                  1);

    return section;
}

#endif

VOID access_all_VAs(LPVOID lpParameter) {
    PULONG_PTR arbitrary_va;
    unsigned random_number;
    BOOL page_faulted;
    BOOL pages_full;
    BOOL redo_fault = FALSE;

    for (i = 0; i < MB (1); i += 1) {

        //
        // Randomly access different portions of the virtual address
        // space we obtained above.
        //
        // If we have never accessed the surrounding page size (4K)
        // portion, the operating system will receive a page fault
        // from the CPU and proceed to obtain a physical page and
        // install a PTE to map it - thus connecting the end-to-end
        // virtual address translation.  Then the operating system
        // will tell the CPU to repeat the instruction that accessed
        // the virtual address and this time, the CPU will see the
        // valid PTE and proceed to obtain the physical contents
        // (without faulting to the operating system again).
        //

        random_number = rand () * rand () * rand ();

        random_number %= VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS;

        //
        // Write the virtual address into each page.  If we need to
        // debug anything, we'll be able to see these in the pages.
        //

        page_faulted = FALSE;

        //
        // Ensure the write to the arbitrary virtual address doesn't
        // straddle a PAGE_SIZE boundary just to keep things simple for
        // now.
        //

        random_number &= ~0x7;

        if (redo_fault == FALSE) {
            arbitrary_va = VA_space_start + random_number;
        }
        // TODO: Add locks on this
        __try {
            *arbitrary_va = (ULONG_PTR) arbitrary_va;
            // Make sure we're removing most recently accessed, not most recently added
            // Move page to front of active list since we've recently accessed it
            PPTE PTE_location = find_PTE_location(arbitrary_va, VA_space_start);
            // Get frame number from PTE
            ULONG_PTR frame_number = PTE_location->ram_pte.frame_number;
            // Get PFN from frame number using sparse array and pointer arithmetic
            PPFN page_pfn = pfn_array + frame_number;
            // Get pointer to list entry of PFN
            PLIST_ENTRY data_entry = (PLIST_ENTRY) page_pfn;
            // Finally, bring page to start of active list
            RemoveEntryList(data_entry);
            InsertTailList(&active_list_head, data_entry);
        } __except (EXCEPTION_EXECUTE_HANDLER) {

            page_faulted = TRUE;
        }

        if (page_faulted) {
            // Get PTE of our faulting VA
            PPTE PTE_location = find_PTE_location(arbitrary_va, VA_space_start);
            PPFN page_pfn;
            ULONG_PTR frame_number;
            // Get PTE lock
            acquire_PTE_lock();
            // If another thread has already wired up the page, can exit early (work already done)
            if (PTE_location->ram_pte.valid == PTE_VALID) {
                release_PTE_lock();
                redo_fault = TRUE;
                continue;
            }
            // If it's in transition, then soft fault (page if modified or standby)
            if (PTE_location->transition_pte.transition == PTE_IN_TRANSITION) {
                // Lock PFN to see which list lock to grab
                EnterCriticalSection(&page_pfn->pfn_lock);
                // TODO: can solidify modified and standby fault into one function
                // Check whether page is on standby or modified list
                if (page_pfn->state == PFN_MODIFIED) {
                    // Soft fault and bring back page from modified list
                    soft_fault_modified(page_pfn);
                }
                // MUST BE ON STANDBY IF NOT ON
                else if (page_pfn->state == PFN_STANDBY){
                    soft_fault_standby(page_pfn);
                }
                // Something's wrong, must be modified or standby
                else {
                    DebugBreak();
                }
                LeaveCriticalSection(&page_pfn->pfn_lock);
            }
            // If it's not in transition, then it's either on disk or a first-time fault
            // Regardless, we need to get a page to associate with the PTE (pulling from free list if free pages available, otherwise standby)
            // If accessing old disk data
            if (PTE_location->disk_pte.transition == PTE_ON_DISK) {
                // TODO: something about free list lock
                // Prioritize getting a free page first if we have any
                // First make sure free list has pages before entering critical (expensive)
                // Right now don't do speculative looking
                // if (!IsListEmpty(&free_list_head)) {
                    EnterCriticalSection(&free_list_lock);
                    if (!IsListEmpty(&free_list_head)) {
                        page_pfn = (PPFN) RemoveHeadList(&free_list_head);
                        // Acquire page lock
                        acquire_pfn_lock(page_pfn);
                        frame_number = calculate_page_number(page_pfn, pfn_array);
                    }
                    // TODO AI: replace all zero lists with a free list pop and then zeroing if necsesary (not pulling from disk)
                    LeaveCriticalSection(&free_list_lock);
                    // TODO: FIX THIS
                // }
                // TODO: something about zero list lock
                // // Lock the zero list so other threads can't access
                // EnterCriticalSection(&zero_list_lock);
                // // Read the PTE and see if any other thread has been here
                // if (PTE_location->ram_pte.valid == 1) {
                //     // No work necessary, VA is already wired up
                //     LeaveCriticalSection(&zero_list_lock);
                //     continue;
                // }
                // Worst case, trim active page to free (no need to zero since we will re-write)
                else {
                    page_pfn = (PPFN) RemoveHeadList(&active_list_head);
                    frame_number = calculate_page_number(page_pfn, pfn_array);
                    trim_active_to_free(page_pfn, frame_number);
                }
                // Finally, retrieve old data from disk
                retrieve_page_from_disk(PTE_location, frame_number);
            }
            // If PTE has a frame number and not on disk, must be either on modified or standby (check for hits)
            else if (PTE_location->ram_pte.frame_number != 0) {
                frame_number = PTE_location->ram_pte.frame_number;
                // Get PFN and look at state metadata to determine if page is on modified or standby
                page_pfn = pfn_array + frame_number;
                if (page_pfn->state == PFN_STANDBY) {
                    soft_fault_standby(page_pfn);
                } // Else if check might not be necessary but good just in case
                else if (page_pfn->state == PFN_MODIFIED) {
                    soft_fault_modified(page_pfn);
                }
            }
            // New data, so we need a fully zeroed page!
            else {
                // TODO: lock stuff
                // If we have pages in our zero list, pop and retrieve
                if (!IsListEmpty(&zero_list_head)) {
                    page_pfn = (PPFN) RemoveHeadList(&zero_list_head);
                    frame_number = calculate_page_number(page_pfn, pfn_array);
                    // Release lock once zero page is accessed
                    // LeaveCriticalSection(&zero_list_lock);
                }
                // Otherwise, get oldest accessed page from our active list, write it to disk, and trim to zero
                else {
                    page_pfn = (PPFN) RemoveHeadList(&active_list_head);
                    frame_number = calculate_page_number(page_pfn, pfn_array);
                    trim_active_to_zero(page_pfn, frame_number);
                }
            }
            page_pfn->state = PFN_ACTIVE;
            InsertTailList(&active_list_head, &page_pfn->entry);
            // Wire up our new page and set it to valid
            set_PTE_to_valid(PTE_location, frame_number);
            page_pfn->pte = PTE_location;

            if (MapUserPhysicalPages (arbitrary_va, 1, &frame_number) == FALSE) {
                printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, frame_number);
                DebugBreak();
            }
            // Update redo fault flag so we write VA on next pass
            redo_fault = TRUE;
        }
        else {
            redo_fault = FALSE;
        }
    }
}
VOID
st_state_machine_test (
    VOID
    )
{
    ULONG_PTR highest_page_number;
    BOOL allocated;
    BOOL privilege;
    ULONG_PTR physical_page_count;
    PULONG_PTR physical_page_numbers;
    HANDLE physical_page_handle;
    ULONG_PTR virtual_address_size;
    // Initialize all globals
    initialize_globals();

    create_paging_file ();

    //
    // Allocate the physical pages that we will be managing.
    //
    // First acquire privilege to do this since physical page control
    // is typically something the operating system reserves the sole
    // right to do.
    //

    privilege = GetPrivilege ();

    if (privilege == FALSE) {
        printf ("full_virtual_memory_test : could not get privilege\n");
        return;
    }    

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    physical_page_handle = CreateSharedMemorySection ();

    if (physical_page_handle == NULL) {
        printf ("CreateFileMapping2 failed, error %#x\n", GetLastError ());
        return;
    }

#else

    physical_page_handle = GetCurrentProcess ();

#endif

    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;

    physical_page_numbers = malloc (physical_page_count * sizeof (ULONG_PTR));

    if (physical_page_numbers == NULL) {
        printf ("full_virtual_memory_test : could not allocate array to hold physical page numbers\n");
        return;
    }

    allocated = AllocateUserPhysicalPages (physical_page_handle,
                                           &physical_page_count,
                                           physical_page_numbers);

    if (allocated == FALSE) {
        printf ("full_virtual_memory_test : could not allocate physical pages\n");
        return;
    }
    if (physical_page_count != NUMBER_OF_PHYSICAL_PAGES) {

        printf ("full_virtual_memory_test : allocated only %llu pages out of %u pages requested\n",
                physical_page_count,
                NUMBER_OF_PHYSICAL_PAGES);
    }
    highest_page_number = 0;
    // Determine highest physical page number allocated to us
    for (i = 0; i < physical_page_count; i++) {
        ULONG_PTR current = physical_page_numbers[i];
        if (highest_page_number < current) {
            highest_page_number = current;
        }
    }

    // Create PFN array
    pfn_array = VirtualAlloc (NULL,
        highest_page_number * sizeof(PFN),
        MEM_RESERVE,
        PAGE_READWRITE);
    // Populate PFN with physical pages
    for (i = 0; i < physical_page_count; i++) {
        LPVOID result = VirtualAlloc((LPVOID)(pfn_array + physical_page_numbers[i]),
                                     sizeof(PFN),
                                     MEM_COMMIT,
                                     PAGE_READWRITE);
        if (result == NULL) {
            DebugBreak();
        }
        create_zeroed_pfn(pfn_array + (ULONG_PTR) physical_page_numbers[i]);
        // Add physical pages to zero list
        InsertTailList(&zero_list_head, &(pfn_array[physical_page_numbers[i]].entry));
    }

    //
    // Reserve a user address space region using the Windows kernel
    // AWE (address windowing extensions) APIs.
    //
    // This will let us connect physical pages of our choosing to
    // any given virtual address within our allocated region.
    //
    // We deliberately make this much larger than physical memory
    // to illustrate how we can manage the illusion.
    //

    virtual_address_size = 64 * physical_page_count * PAGE_SIZE;
    //
    // Round down to a PAGE_SIZE boundary.
    //

    virtual_address_size &= ~PAGE_SIZE;

    virtual_address_size_in_unsigned_chunks =
                        virtual_address_size / sizeof (ULONG_PTR);
    #if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Allocate a MEM_PHYSICAL region that is "connected" to the AWE section
    // created above.
    //

    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;

    VA_space_start = VirtualAlloc2 (NULL,
                       NULL,
                       virtual_address_size,
                       MEM_RESERVE | MEM_PHYSICAL,
                       PAGE_READWRITE,
                       &parameter,
                       1);

#else

    VA_space_start = VirtualAlloc (NULL,
                      virtual_address_size,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

#endif

    if (VA_space_start == NULL) {

        printf ("full_virtual_memory_test : could not reserve memory %x\n",
                GetLastError ());

        return;
    }

    // Create events
    initiate_trimming_event = CreateEvent(NULL, MANUAL_RESET, FALSE, NULL);
    system_exit_event = CreateEvent(NULL, MANUAL_RESET, FALSE, NULL);
    //
    // Now perform random accesses.
    //
    HANDLE faulting_thread_handles[NUMBER_OF_THREADS];
    for (i = 0; i < NUMBER_OF_THREADS; i += 1) {
        // Create thread, pass in access all virtual addresses
        faulting_thread_handles[i] = CreateThread(NULL,
                                        0,
                                        (LPTHREAD_START_ROUTINE)access_all_VAs,
                                        NULL,
                                        0,
                                        NULL);
    }
    // Create trimming thread
    CreateThread(NULL,
                                        0,
                                        (LPTHREAD_START_ROUTINE) trim_pages_thread,
                                        NULL,
                                        0,
                                        NULL);
    // Create writing thread
    CreateThread(NULL,
                                        0,
                                        (LPTHREAD_START_ROUTINE) write_pages_thread,
                                        NULL,
                                        0,
                                        NULL);



    for (i = 0; i < NUMBER_OF_THREADS; i += 1) {
        // Wait for all threads to die
        WaitForSingleObject(faulting_thread_handles[i], INFINITE);
    }

    SetEvent(system_exit_event);

    printf ("full_virtual_memory_test : finished accessing %u random virtual addresses\n", i);

    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //

    VirtualFree (VA_space_start, 0, MEM_RELEASE);

    return;
}
int
main (
    int argc,
    char** argv
    )
{
    st_state_machine_test();
}