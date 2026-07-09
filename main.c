#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "macros.h"
#include "data_structures/pfn.h"

//
// This define enables code that lets us create multiple virtual address
// mappings to a single physical page.  We only/need want this if/when we
// start using reference counts to avoid holding locks while performing
// pagefile I/Os - because otherwise disallowing this makes it easier to
// detect and fix unintended failures to unmap virtual addresses properly.
//

#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 0

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
#define NUMBER_OF_DISK_PAGES (VIRTUAL_ADDRESS_SIZE / PAGE_SIZE)

BOOL
GetPrivilege  (
    VOID
    )
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

PULONG_PTR CreateVirtualDisk()



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
VOID
st_state_machine_test (
    VOID
    )
{
    unsigned i;
    PULONG_PTR VA_space_start;
    PULONG_PTR arbitrary_va;
    unsigned random_number;
    BOOL allocated;
    BOOL page_faulted;
    BOOL privilege;
    BOOL obtained_pages;
    ULONG_PTR physical_page_count;
    PULONG_PTR physical_page_numbers;
    HANDLE physical_page_handle;
    ULONG_PTR virtual_address_size;
    ULONG_PTR virtual_address_size_in_unsigned_chunks;
    BOOL pages_full;
    ULONG_PTR highest_page_number;
    PPFN pfn_array;

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

    // Create list head for active list
    LIST_ENTRY active_list_head;
    LIST_ENTRY zero_list_head;

    // Create list head for zero list
    InitializeListHead(&zero_list_head);
    InitializeListHead(&active_list_head);

    // Populate PFN with physical pages
    for (i = 0; i < physical_page_count; i++) {
        LPVOID result = VirtualAlloc((LPVOID)(pfn_array + physical_page_numbers[i]),
                                     sizeof(PFN),
                                     MEM_COMMIT,
                                     PAGE_READWRITE);
        if (result == NULL) {
            DebugBreak();
        }
        create_zeroed_pfn(pfn_array + physical_page_numbers[i]);
        // Add physical pages to zero list
        InsertTailList(&zero_list_head, &(pfn_array[physical_page_numbers[i]].entry));
    }

    create_all_PTEs(NUMBER_OF_PTES);



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

    // Allocate kernel VA space ONE PAGE RESET FOR MULTI THREAD
    PULONG_PTR Kernel_VA_space_start = VirtualAlloc (NULL,
                        PAGE_SIZE,
                        MEM_RESERVE | MEM_PHYSICAL,
                        PAGE_READWRITE);

    if (VA_space_start == NULL) {

        printf ("full_virtual_memory_test : could not reserve memory %x\n",
                GetLastError ());

        return;
    }
    //
    // Now perform random accesses.
    //

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

        random_number %= virtual_address_size_in_unsigned_chunks;

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

        arbitrary_va = VA_space_start + random_number;

        __try {

            *arbitrary_va = (ULONG_PTR) arbitrary_va;

        } __except (EXCEPTION_EXECUTE_HANDLER) {

            page_faulted = TRUE;
        }

        if (page_faulted) {

            //
            // Connect the virtual address now - if that succeeds then
            // we'll be able to access it from now on.
            //
            // THIS IS JUST REUSING THE SAME PHYSICAL PAGE OVER AND OVER !
            //
            // IT NEEDS TO BE REPLACED WITH A TRUE MEMORY MANAGEMENT
            // STATE MACHINE !

            // Wire our final page table
            PPTE PTE_location = find_PTE_location(arbitrary_va, VA_space_start);
            ULONG_PTR frameNumber;
            PPFN pagePFN;

            // If we have zero pages available in our list
            if (!IsListEmpty(&zero_list_head)) {

                // Go to the zero list to get a zero page
                pagePFN = (PPFN) RemoveHeadList(&zero_list_head);

                // Calculate page number of that page (offset in the pfn array)
                frameNumber = calculate_page_number(pagePFN, pfn_array);
            }
            else {
            //     // If all pages are active
            //     printf("all pages in use!\n");

                // Remove tail from active list (oldest active page still in memory (first to be added))
                pagePFN = (PPFN) RemoveTailList(&active_list_head);

                // Access frame number from PTE
                PPTE oldPTE = pagePFN->pte;

                // Sever old VA connection
                frameNumber = oldPTE->frame_number;
                oldPTE->valid = 0;

                // TODO Eventually we want to make this write to disk instead of just zeroing out
                oldPTE->frame_number = 0;

                // Sever operating system connection between prior VA and physical page
                PULONG_PTR va = findVAFromPTE(oldPTE, VA_space_start);

                // TODO turn this into a function (we keep reusing the if (!Mapuser...)
                if (!MapUserPhysicalPages(va, 1, NULL)) {
                    DebugBreak();
                }

                // Assign connection to Kernel space
                // Map User takes VA to map, number of pages, and then the address (since we can pass multiple pages)
                // Map user is smart, knows you're pointing to a frame number / index not the actual frame
                if (!MapUserPhysicalPages(Kernel_VA_space_start, 1, &frameNumber)) {
                    DebugBreak();
                }

                // Zero out page
                memset(Kernel_VA_space_start, 0, PAGE_SIZE);

                // Sever Kernel Space Connection
                if (!MapUserPhysicalPages(Kernel_VA_space_start, 1, NULL)) {
                    DebugBreak();
                }
            }

            // Add zero page to our active page list
            InsertHeadList(&active_list_head, &pagePFN->entry);
            pagePFN->state = PFN_ACTIVE;

            // Set our frame number in our PTE
            set_PTE_to_valid(PTE_location, frameNumber);
            pagePFN->pte = PTE_location;

            if (!MapUserPhysicalPages (arbitrary_va, 1, &frameNumber)) {

                printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, *physical_page_numbers);

                return;
            }

            //
            // No exception handler needed now since we have connected
            // the virtual address above to one of our physical pages
            // so no subsequent fault can occur.
            //

            *arbitrary_va = (ULONG_PTR) arbitrary_va;

            //
            // Unmap the virtual address translation we installed above
            // now that we're done writing our value into it.
            //

            // This broke because we severed the OS connection to the physical page without updating our data structures
            // if (MapUserPhysicalPages (arbitrary_va, 1, NULL) == FALSE) {
            //
            //     printf ("full_virtual_memory_test : could not unmap VA %p\n", arbitrary_va);
            //
            //     return;
            // }
        }
    }

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