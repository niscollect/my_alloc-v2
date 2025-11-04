#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>


//**** ****///
#include <pthread.h>

pthread_mutex_t sbrk_lock = PTHREAD_MUTEX_INITIALIZER;

void* thread_safe_sbrk(size_t size) 
{
    void* ptr;
    pthread_mutex_lock(&sbrk_lock);
    ptr = sbrk(size);
    pthread_mutex_unlock(&sbrk_lock);
    return ptr;
}
//**** ****///


#define ALIGNMENT 8 // or 16 in some cases
#define ALIGN(size) ( ((size) + (ALIGNMENT - 1)) &~ (ALIGNMENT - 1) )


#define MIN_PAYLOAD 8  // often set to ALIGNMENT, so it can save atleast some user data


typedef struct meta_block
{
    size_t size;         // user-requested size
    size_t aligned_size; // actual block size allocated
    struct meta_block *next;
    int free;   // mark if it's free or not

} meta_block;



// #define META_SIZE sizeof(meta_block)
#define META_SIZE ALIGN(sizeof(meta_block))


// -------------------------------------------------------------------------- //

// segregated free list implementation starts
#define NUM_CLASSES 8
// static meta_block* free_lists[NUM_CLASSES];  // one head per size class
__thread meta_block* free_lists[NUM_CLASSES] = {NULL}; 

// returns which list to look at
int get_size_class(size_t size)
{
    int class = 0;
    size_t block_size = 16;

    while (size > block_size && class < NUM_CLASSES - 1) 
    {
        block_size <<= 1; //! next block size
        //* 16 → 32 → 64 → 128 → 256 → 512 → 1024
        class++;
    }
    return class;
}


// ------------------------------------------------------------------------ //


//* simple O(1) insertion at the head of the list.
void insert_into_free_list(meta_block* block, int class_idx)
{
    // Insert at head of free_lists[class_idx]
    block->next = free_lists[class_idx];
    free_lists[class_idx] = block;
    // maybe already done, but just making sure is good
    block->free = 1;  // mark as free
}

meta_block* find_free_in_class(int class_idx, size_t size)
{
    size_t aligned_size = ALIGN(size);

    meta_block* curr = free_lists[class_idx];
    meta_block* prev = NULL;

    while(curr)
    {
        // Check if this block is big enough
        if(curr->free && curr->aligned_size >= aligned_size)
        {
            // Found a suitable block - remove it from the list
            if(prev)
            {
                prev->next = curr->next;  // unlink from middle/end
            }
            else
            {
                free_lists[class_idx] = curr->next;  // unlink from head
            }
            
            curr->next = NULL;  // detach from list
            return curr;
        }
        
        prev = curr;
        curr = curr->next;
    }

    return NULL; // coudln't find any suitable block
}

void remove_from_free_list(meta_block* block, int class_idx)
{
    meta_block* curr = free_lists[class_idx];
    meta_block* prev = NULL;
    
    while(curr)
    {
        if(curr == block)
        {
            // Found it - remove from list
            if(prev)
            {
                prev->next = curr->next;  // unlink from middle/end
            }
            else
            {
                free_lists[class_idx] = curr->next;  // unlink from head
            }
            
            curr->next = NULL;
            return;
        }
        
        prev = curr;
        curr = curr->next;
    }
}



// to request for free block
meta_block* request_block(size_t size)
{

    size_t aligned_size = ALIGN(size);

    meta_block* block = NULL;
    
    block = thread_safe_sbrk(0); // current program break
    
    //* *//
    void* request = thread_safe_sbrk(aligned_size + META_SIZE); // gives the starting part
    //* *//

    // just for debugging help
    assert((void*)block == request);
    
    if(request == (void*)-1)  //* (void*)-1 is what sbrk returns on its failure, Nothing Special
    {
        return NULL;
    }

    block->size = size;
    block->aligned_size = aligned_size;
    block->next = NULL; // marks it as 'the last block'
    block->free = 0;

    return block;
}


void split_block(meta_block* ptr, size_t requested_size)
{
    size_t aligned_requested_size = ALIGN(requested_size);

    meta_block* new_block = (meta_block*)( (char*)(ptr) + META_SIZE + aligned_requested_size );

    new_block->aligned_size = (ptr)->aligned_size - aligned_requested_size - META_SIZE;
    new_block->size = new_block->aligned_size;  // leftover block size
    new_block->free = 1;
    // new_block->next = (ptr)->next;
    new_block->next = NULL;


    // (ptr)->next = new_block;
    (ptr)->next = NULL; // got used, no need to track now
    (ptr)->aligned_size = aligned_requested_size;
    (ptr)->size = requested_size;
    (ptr)->free = 0;


    //* Insert leftover block into appropriate free list
    int class_idx = get_size_class(new_block->aligned_size);
    insert_into_free_list(new_block, class_idx);
}


void coalesce_forward(meta_block* block_ptr)
{
    // meta_block* next_block = block_ptr->next;
    // assumed physical adjaceny
    // but now we'll need to check for physical adjacency

    //* Calculate where the next block SHOULD be in memory
    meta_block* next_block = (meta_block*)((char*)block_ptr + META_SIZE + block_ptr->aligned_size);
    

    //* Check if next block exists, is free, and within heap bounds
    void* heap_end = thread_safe_sbrk(0);

    if ((void*)next_block >= heap_end)
    {
        return;  // no next block (we're at the end)
    }
    //* check if it's free
    if(!next_block->free)
        return;

    // if (next_block->free)
    // {
    //     // Remove next_block from its current free list
    //     int next_class = get_size_class(next_block->aligned_size);
    //     remove_from_free_list(next_block, next_class);
        
    //     // Merge sizes (+META_SIZE for the header we're absorbing)
    //     block_ptr->aligned_size += next_block->aligned_size + META_SIZE;
    //     block_ptr->size = block_ptr->aligned_size;  // update size too
    // }


    // Physical adjacency check is good but not in case of multi-threading coz that is not very prudent, as physically adjacent block can belong to some other thread
    //* check if next_block is in THIS thread's free list
    int next_class = get_size_class(next_block->aligned_size);
    meta_block* iter = free_lists[next_class];
    int found = 0;
    while (iter)
    {
        if (iter == next_block)
        {
            found = 1;
            break;
        }
        iter = iter->next;
    }

    if (!found)
        return; // next block belongs to another thread, skip coalescing
    
    //else
    // it is safe to remove from current thread free list and merge
    remove_from_free_list(next_block, next_class);
    block_ptr->aligned_size += next_block->aligned_size + META_SIZE;
    block_ptr->size = block_ptr->aligned_size; 

}



void* my_alloc(int size)
{

    //* align the requested size
    size_t aligned_size = ALIGN(size);

    meta_block* block = NULL;

    if(size < 0)
    {
        // Makes no sense
        return NULL;
    }
       
    
    //* get the appropriate size class
    int class_idx = get_size_class(aligned_size);


    //* Try to find a free block in the appropriate class
    block = find_free_in_class(class_idx, aligned_size);
    

    //* If not found in exact class, try larger classes (fallback)
    if(!block)
    {
        for(int i = class_idx + 1; i < NUM_CLASSES; i++)
        {
            block = find_free_in_class(i, aligned_size);
            if(block)
            {
                break;  // found in larger class
            }
        }
    }


    //* If still no block found, request new memory from OS
    if(!block)
    {
        block = request_block(size);
        if(!block)
        {
            return NULL;  // sbrk failed
        }
    }


    //* Mark block as allocated
    block->free = 0;


    //* Try to split if block is much larger than needed
    if(block->aligned_size >= aligned_size + META_SIZE + MIN_PAYLOAD)
    {
        split_block(block, size);
    }


    // ! Very Imp. line of code
    return (block + 1);
    //* This will go to the user, and so it must be internally aligned. Yes, it is aligned, by the `META_SIZE` macro
}


void freee(void* ptr)
{
    if(!ptr)
    {
        return;
    }

    meta_block* block_ptr = (meta_block*)ptr - 1;

    assert(block_ptr->free == 0);

    block_ptr->free = 1;

    //* coalescing (forward)
    coalesce_forward(block_ptr);


    //* Insert into apt free list
    int class_idx = get_size_class(block_ptr->aligned_size);
    insert_into_free_list(block_ptr, class_idx);

}

void* call_oc(size_t nelem, size_t elsize)
{
    size_t size = nelem * elsize;

    // TODO: Check for overflow
    if (nelem != 0 && size / nelem != elsize) 
    {
        // overflow happened
        return NULL;
    }

    void* ptr = my_alloc(size);
    
    if(!ptr)
        return NULL;
    
    memset(ptr, 0, size);
    return ptr;
}

void* reall_oc(void* ptr, size_t size)
{

    size_t aligned_size_request = ALIGN(size);

    //*NOTE:  ptr = NULL  ------> realloc behaves as malloc
    if(!ptr)
    {
        return my_alloc(size);
    }

    meta_block* block_ptr = (meta_block*)ptr - 1;
    
    if(block_ptr->size >= size)
    {
        // already have enough space
        return ptr;
    }

    if(block_ptr->aligned_size >= aligned_size_request)
    {
        block_ptr->size = size; // update user-visible size
        return ptr;
    }

    // need to really realloc
    //* Malloc new space and free old space
    //* then just copy old data to the new space
    void* new_ptr;
    new_ptr = my_alloc(size);

    if(!new_ptr)
    {
        return NULL;
    }

    memcpy(new_ptr, ptr, block_ptr->size);
    freee(ptr);
    
    return new_ptr;
    
}


int main()
{
    printf("=== Testing Segregated Free List Allocator ===\n\n");

    // Test 1: Basic allocation
    printf("Test 1: Basic allocation\n");
    int* arr1 = (int*)my_alloc(10 * sizeof(int));
    char* str1 = (char*)my_alloc(50);
    printf("Allocated arr1: %p, str1: %p\n\n", arr1, str1);

    // Test 2: Free and reuse
    printf("Test 2: Free and reuse\n");
    freee(arr1);
    int* arr2 = (int*)my_alloc(10 * sizeof(int));  // Should reuse arr1's block
    printf("Freed arr1, allocated arr2: %p (should reuse)\n\n", arr2);

    // Test 3: Different size classes
    printf("Test 3: Different size classes\n");
    void* p16 = my_alloc(16);   // class 0
    void* p32 = my_alloc(32);   // class 1
    void* p64 = my_alloc(64);   // class 2
    void* p128 = my_alloc(128); // class 3
    printf("16B: %p, 32B: %p, 64B: %p, 128B: %p\n\n", p16, p32, p64, p128);

    // Test 4: Calloc (zero initialization)
    printf("Test 4: Calloc\n");
    int* zeros = (int*)call_oc(10, sizeof(int));
    printf("Calloc array, first element: %d (should be 0)\n\n", zeros[0]);

    // Test 5: Realloc
    printf("Test 5: Realloc\n");
    char* small = (char*)my_alloc(16);
    strcpy(small, "Hello");
    char* large = (char*)reall_oc(small, 128);
    printf("Realloc'd string: %s\n\n", large);

    // Test 6: Coalescing
    printf("Test 6: Coalescing\n");
    void* a = my_alloc(32);
    void* b = my_alloc(32);
    void* c = my_alloc(32);
    printf("Allocated 3 adjacent blocks: %p, %p, %p\n", a, b, c);
    freee(b);
    freee(a);  // Should coalesce with b
    printf("Freed a and b (should coalesce)\n\n");

    // Test 7: Heap growth
    printf("Test 7: Heap stats\n");
    void* heap_start = thread_safe_sbrk(0);
    void* big = my_alloc(2048);  // Force new sbrk
    void* heap_end = thread_safe_sbrk(0);
    printf("Heap grew from %p to %p (%ld bytes)\n", 
           heap_start, heap_end, (char*)heap_end - (char*)heap_start);

    printf("\n=== All tests completed ===\n");
    return 0;
}
