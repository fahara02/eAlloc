#pragma once

#include "builtins.h"
#include <assert.h>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#define dsa_min(a, b) ((a) < (b) ? (a) : (b))
#define dsa_max(a, b) ((a) > (b) ? (a) : (b))
#define dsa_insist(x)  \
    {                  \
        dsa_assert(x); \
        if(!(x))       \
        {              \
            status--;  \
        }              \
    }

#if !defined(dsa_assert)
    #define dsa_assert assert
#endif
#define _dsa_glue2(x, y) x##y
#define _dsa_glue(x, y) _dsa_glue2(x, y)
#define dsa_static_assert(exp) typedef char _dsa_glue(static_assert, __LINE__)[(exp) ? 1 : -1]

dsa_static_assert(sizeof(int) * CHAR_BIT == 32);
dsa_static_assert(sizeof(size_t) * CHAR_BIT >= 32);
dsa_static_assert(sizeof(size_t) * CHAR_BIT <= 64);

namespace dsa
{

template <size_t SLI = 5> // NO LINT
class TLSF
{
   public:
    /**
     * @brief Header for memory blocks in the TLSF allocator.
     *
     * This structure stores meta-information about a memory block used by the TLSF (Two-Level
     * Segregated Fit) allocator. It includes a pointer to the previous physical block, the block's
     * size and allocation flags, a 32-bit tag for ownership (task/thread ID), and pointers to the
     * next and previous free blocks in the free list.
     */
    struct BlockHeader
    {
        /* Points to the previous physical block. */
        struct BlockHeader* prev_phys_block;
        /* The size of this block, including the block header. */
        size_t size_and_flags;
#if defined(EALLOC_ENABLE_OWNERSHIP_TAG) && EALLOC_ENABLE_OWNERSHIP_TAG
        /* 32-bit tag for ownership (task/thread ID). */
        uint32_t owner_tag;
#endif
        /* Next and previous free blocks. */
        struct BlockHeader* next_free;
        struct BlockHeader* prev_free;
    };

   private:
    static constexpr size_t SL_INDEX_LOG2 = SLI;
    static constexpr size_t ALIGN_SIZE_LOG2 = 2;
    static constexpr size_t BLOCK_ALIGNMENT = (1 << ALIGN_SIZE_LOG2);
    static constexpr size_t FL_INDEX_MAX = 31;
    static constexpr size_t SLI_COUNT = (1 << SL_INDEX_LOG2);
    static constexpr size_t FL_INDEX_SHIFT = (SL_INDEX_LOG2 + ALIGN_SIZE_LOG2);
    static constexpr size_t FL_INDEX_COUNT = (FL_INDEX_MAX - FL_INDEX_SHIFT + 1);
    static constexpr size_t SMALL_BLOCK_SIZE = (1 << FL_INDEX_SHIFT);
    static constexpr size_t ALIGN_SIZE = BLOCK_ALIGNMENT;
    /*
     ** Since block sizes are always at least a multiple of 4, the two least
     ** significant bits of the size field are used to store the block status:
     ** - bit 0: whether block is busy or free
     ** - bit 1: whether previous block is busy or free
     */
    static constexpr size_t free_bit = 1 << 0;
    static constexpr size_t prev_free_bit = 1 << 1;
    static constexpr size_t flag_mask = prev_free_bit | free_bit;

    static constexpr size_t block_header_overhead = sizeof(size_t);
    static constexpr size_t block_start_offset =
        offsetof(BlockHeader, size_and_flags) + block_header_overhead;
    static constexpr size_t block_size_min = sizeof(BlockHeader) - sizeof(BlockHeader*);
    static constexpr size_t block_size_max = static_cast<size_t>(1 << FL_INDEX_MAX);

    dsa_static_assert(sizeof(unsigned int) * CHAR_BIT >= SLI_COUNT);
    dsa_static_assert(BLOCK_ALIGNMENT == SMALL_BLOCK_SIZE / SLI_COUNT);

   public:
    /**
     * @brief Second-level structure for the TLSF allocator.
     *
     * This structure represents the second-level index in the Two-Level Segregated Fit allocator.
     * It maintains a bitmap (sl_bitmap) that indicates which shelves (free lists) have available
     * blocks, and an array of pointers (shelves) to the free block lists for blocks of specific
     * size ranges.
     */
    struct SecondLevel
    {
        uint32_t sl_bitmap = 0;
        BlockHeader* shelves[SLI_COUNT] = {nullptr};
    };

    /**
     * @brief Control structure for the TLSF allocator.
     *
     * This structure maintains the overall state of the allocator, including:
     * - A special block (block_null) used to indicate empty free lists.
     * - A bitmap (fl_bitmap) representing the first-level free block availability.
     * - An array (cabinets) of second-level indices that manage the free lists for specific size
     * ranges.
     */
    struct Control
    {
        /* Empty lists point at this block to indicate they are free. */
        BlockHeader block_null;
        uint32_t fl_bitmap = 0;
        SecondLevel cabinets[FL_INDEX_COUNT];
    };

    /* A type used for casting when doing pointer arithmetic. */
    typedef ptrdiff_t tlsfptr_t; ///< Type for pointer arithmetic.
    typedef void (*tlsf_walker)(void* ptr, size_t size, int used,
                                void* user); ///< Function pointer type for walking memory blocks.

    // Block Header Functions
    /**
     * @brief Gets the usable size of a block.
     *
     * This function returns the block size after masking out the flag bits.
     *
     * @param block Pointer to the block header.
     * @return The size of the block.
     */
    static inline size_t get_size(const BlockHeader* block)
    {
        return block->size_and_flags & ~flag_mask;
    }

    /**
     * @brief Sets the size of a block while preserving flag bits.
     *
     * This function assigns the given size to the block header, preserving the current flag bits.
     *
     * @param block Pointer to the block header.
     * @param size New size to set.
     */
    static inline void set_size(BlockHeader* block, size_t size)
    {
        block->size_and_flags = (size & ~flag_mask) | (block->size_and_flags & flag_mask);
    }

    /**
     * @brief Checks if a block is free.
     *
     * @param block Pointer to the block header.
     * @return True if the block is free, false otherwise.
     */
    static inline bool is_free(const BlockHeader* block)
    {
        return block->size_and_flags & free_bit;
    }

    /**
     * @brief Checks if the previous block is free.
     *
     * @param block Pointer to the block header.
     * @return True if the previous block is free, false otherwise.
     */
    static inline bool is_prev_free(const BlockHeader* block)
    {
        return block->size_and_flags & prev_free_bit;
    }

    /**
     * @brief Checks if the block is the last block in the heap.
     *
     * This is determined by checking if the block size is zero.
     *
     * @param block Pointer to the block header.
     * @return True if the block is the last block, false otherwise.
     */
    static inline bool is_last(const BlockHeader* block) { return get_size(block) == 0; }

    /**
     * @brief Marks the block as free.
     *
     * @param block Pointer to the block header.
     */
    static inline void set_free(BlockHeader* block) { block->size_and_flags |= free_bit; }

    /**
     * @brief Marks the block as used.
     *
     * @param block Pointer to the block header.
     */
    static inline void set_used(BlockHeader* block) { block->size_and_flags &= ~free_bit; }

    /**
     * @brief Marks the previous block as free.
     *
     * @param block Pointer to the block header.
     */
    static inline void set_prev_free(BlockHeader* block) { block->size_and_flags |= prev_free_bit; }

    /**
     * @brief Marks the previous block as used.
     *
     * @param block Pointer to the block header.
     */
    static inline void set_prev_used(BlockHeader* block)
    {
        block->size_and_flags &= ~prev_free_bit;
    }

    static inline const void* to_ptr(const BlockHeader* block)
    {
        return reinterpret_cast<const void*>(reinterpret_cast<const unsigned char*>(block)
                                             + block_start_offset);
    }
    static inline void* to_ptr_nc(BlockHeader* block)
    {
        return reinterpret_cast<void*>(reinterpret_cast<unsigned char*>(block)
                                       + block_start_offset);
    }
    static inline const BlockHeader* from_ptr(const void* ptr)
    {
        return reinterpret_cast<const BlockHeader*>(reinterpret_cast<const unsigned char*>(ptr)
                                                    - block_start_offset);
    }
    static inline BlockHeader* from_ptr_nc(void* ptr)
    {
        return reinterpret_cast<BlockHeader*>(reinterpret_cast<unsigned char*>(ptr)
                                              - block_start_offset);
    }

    /* Return location of next block after block of given size. */
    static inline const BlockHeader* offset_to_block(const void* ptr, size_t size)
    {
        return reinterpret_cast<const BlockHeader*>(reinterpret_cast<const char*>(ptr) + size);
    }
    static inline BlockHeader* offset_to_block_nc(void* ptr, size_t size)
    {
        return reinterpret_cast<BlockHeader*>(reinterpret_cast<char*>(ptr) + size);
    }

    /* Return location of previous block. */
    static inline BlockHeader* prev(const BlockHeader* block)
    {
        dsa_assert(is_prev_free(block) && "previous block must be free");
        return block->prev_phys_block;
    }

    /* Return location of next existing block. */
    static inline BlockHeader* next(BlockHeader* block)
    {
        BlockHeader* n =
            offset_to_block_nc(to_ptr_nc(block), get_size(block) - block_header_overhead);
        dsa_assert(!is_last(block));
        return n;
    }

    static inline const BlockHeader* next_const(const BlockHeader* block)
    {
        const BlockHeader* n =
            offset_to_block(to_ptr(block), get_size(block) - block_header_overhead);
        dsa_assert(!is_last(block));
        return n;
    }

    /* Link a new block with its physical neighbor, return the neighbor. */
    static inline BlockHeader* link_next(BlockHeader* block)
    {
        BlockHeader* n = next(block);
        n->prev_phys_block = block;
        return n;
    }

    static inline void mark_as_free(BlockHeader* block)
    {
        /* Link the block to the next block, first. */
        BlockHeader* next = link_next(block);
        set_prev_free(next);
        set_free(block);
    }

    static inline void mark_as_used(BlockHeader* block)
    {
        BlockHeader* n = next(block);
        set_prev_used(n);
        set_used(block);
    }

    static inline size_t align_up(size_t x, size_t align)
    {
        dsa_assert(0 == (align & (align - 1)) && "must align to a power of two");
        return (x + (align - 1)) & ~(align - 1);
    }

    static inline size_t align_down(size_t x, size_t align)
    {
        dsa_assert(0 == (align & (align - 1)) && "must align to a power of two");
        return x - (x & (align - 1));
    }

    static inline void* align_ptr(const void* ptr, size_t align)
    {
        const tlsfptr_t aligned = (reinterpret_cast<tlsfptr_t>(ptr) + (align - 1)) & ~(align - 1);
        dsa_assert(0 == (align & (align - 1)) && "must align to a power of two");
        return reinterpret_cast<void*>(aligned);
    }

    /**
     * @brief Adjusts the requested allocation size to be aligned to the specified alignment and
     *        ensures it is not smaller than the minimum block size.
     *
     * @param size The requested allocation size.
     * @param align The required alignment.
     * @return The adjusted allocation size.
     */
    static inline size_t adjust_request_size(size_t size, size_t align)
    {
        size_t adjust = 0;
        if(size)
        {
            const size_t aligned = align_up(size, align);
            if(aligned < block_size_max) adjust = dsa_max(aligned, block_size_min);
        }
        return adjust;
    }

    /**
     * @brief Maps the given size to TLSF first-level (fli) and second-level (sli) indices.
     *
     * For sizes smaller than SMALL_BLOCK_SIZE, the first-level index is set to 0, and the
     * second-level index is computed based on the size. For larger sizes, the indices are computed
     * using the most significant bit (via fls) with additional bit manipulations.
     *
     * @param size The size to be mapped.
     * @param fli Pointer to store the computed first-level index.
     * @param sli Pointer to store the computed second-level index.
     */
    static inline void mapping_insert(size_t size, int* fli, int* sli)
    {
        int fl, sl;
        if(size < SMALL_BLOCK_SIZE)
        {
            fl = 0;
            sl = static_cast<int>(size) / (SMALL_BLOCK_SIZE / SLI_COUNT);
        }
        else
        {
            fl = fls(size);

            sl = (static_cast<int>(size >> (fl - SL_INDEX_LOG2)) ^ (1 << SL_INDEX_LOG2));
            fl -= (FL_INDEX_SHIFT - 1);
        }
        *fli = fl;
        *sli = sl;
    }

    /**
     * @brief Rounds up the requested size and maps it to TLSF indices.
     *
     * If the requested size is at least SMALL_BLOCK_SIZE, a rounding value is computed and added
     * to the size. Then, mapping_insert is called to determine the corresponding TLSF indices.
     *
     * @param size The requested allocation size.
     * @param fli Pointer to store the computed first-level index.
     * @param sli Pointer to store the computed second-level index.
     */
    static inline void mapping_search(size_t size, int* fli, int* sli)
    {
        if(size >= SMALL_BLOCK_SIZE)
        {
            const size_t round = (1 << (fls(size) - SL_INDEX_LOG2)) - 1;
            size += round;
        }
        mapping_insert(size, fli, sli);
    }

    /**
     * @brief Searches for a suitable free block in the TLSF allocator.
     *
     * This function uses the provided first-level and second-level indices to locate a free block
     * that is large enough to satisfy the allocation requirements. If no block is found in the
     * current first-level list, the function searches in the next higher-level list.
     *
     * @param control Pointer to the TLSF control structure.
     * @param fli Pointer to the first-level index; updated during the search.
     * @param sli Pointer to the second-level index; updated during the search.
     * @return Pointer to a free block that satisfies the criteria, or nullptr if none is found.
     */
    static inline BlockHeader* search_suitable_block(Control* control, int* fli, int* sli)
    {
        int fl = *fli;
        int sl = *sli;
        unsigned int sl_map = control->cabinets[fl].sl_bitmap & (~0U << sl);
        if(!sl_map)
        {
            /* No block exists. Search in the next largest first-level list. */
            const unsigned int fl_map = control->fl_bitmap & (~0U << (fl + 1));
            if(!fl_map) return nullptr;

            fl = ffs(fl_map);
            *fli = fl;
            sl_map = control->cabinets[fl].sl_bitmap;
        }
        dsa_assert(sl_map && "internal error - second level bitmap is null");
        sl = ffs(sl_map);
        *sli = sl;

        /* Return the first block in the free list. */
        return control->cabinets[fl].shelves[sl];
    }

    /**
     * @brief Removes a block from the free list.
     *
     * Detaches the specified block from its free list by updating its previous and next pointers
     * and adjusts the free list bitmaps corresponding to the first-level and second-level indices.
     *
     * @param control Pointer to the TLSF control structure.
     * @param block Pointer to the block to be removed.
     * @param fl The first-level index associated with the block.
     * @param sl The second-level index associated with the block.
     */
    static inline void remove_free_block(Control* control, BlockHeader* block, int fl, int sl)
    {
        BlockHeader* prev = block->prev_free;
        BlockHeader* next = block->next_free;
        if(next) next->prev_free = prev;
        if(prev) prev->next_free = next;
        if(control->cabinets[fl].shelves[sl] == block)
        {
            control->cabinets[fl].shelves[sl] = next;
            if(next == &control->block_null)
            {
                control->cabinets[fl].sl_bitmap &= ~(1U << sl);
                if(!control->cabinets[fl].sl_bitmap)
                {
                    control->fl_bitmap &= ~(1U << fl);
                }
            }
        }
    }

    /**
     * @brief Inserts a block into the free list.
     *
     * Adds the specified block into the free list at the given first-level and second-level
     * indices, updating the free list pointers and associated bitmaps accordingly.
     *
     * @param control Pointer to the TLSF control structure.
     * @param block Pointer to the block to be inserted.
     * @param fl The first-level index where the block is to be inserted.
     * @param sl The second-level index where the block is to be inserted.
     */
    static inline void insert_free_block(Control* control, BlockHeader* block, int fl, int sl)
    {
        BlockHeader* current = control->cabinets[fl].shelves[sl];
        dsa_assert(current && "free list cannot have a null entry");
        dsa_assert(block && "cannot insert a null entry into the free list");
        block->next_free = current;
        block->prev_free = &control->block_null;
        if(current) current->prev_free = block;

        dsa_assert(to_ptr(block) == align_ptr(to_ptr(block), ALIGN_SIZE)
                   && "block not aligned properly");
        control->cabinets[fl].shelves[sl] = block;
        control->fl_bitmap |= (1U << fl);
        control->cabinets[fl].sl_bitmap |= (1U << sl);
    }

    /* Remove a given block from the free list. */
    static inline void remove(Control* control, BlockHeader* block)
    {
        int fl, sl;
        mapping_insert(get_size(block), &fl, &sl);
        remove_free_block(control, block, fl, sl);
    }

    /* Insert a given block into the free list. */
    static inline void insert(Control* control, BlockHeader* block)
    {
        int fl, sl;
        mapping_insert(get_size(block), &fl, &sl);
        insert_free_block(control, block, fl, sl);
    }

    /**
     * @brief Checks whether a block can be split into two blocks.
     *
     * This function verifies if the given block's size is large enough to be divided into
     * a block of the desired size plus the overhead required for the block header.
     *
     * @param block Pointer to the block header to check.
     * @param size The desired size for the first block after splitting.
     * @return True if the block can be split; false otherwise.
     */
    static inline bool can_split(BlockHeader* block, size_t size)
    {
        return get_size(block) >= sizeof(BlockHeader) + size;
    }

    /**
     * @brief Splits a block into two parts.
     *
     * The function reduces the size of the original block to the requested size and creates
     * a remaining block from the leftover space. It ensures that the remaining block is properly
     * aligned and meets the minimum block size requirement.
     *
     * @param block Pointer to the original block header.
     * @param size The size for the allocated portion of the block.
     * @return Pointer to the remaining block after the split.
     */
    static inline BlockHeader* split(BlockHeader* block, size_t size)
    {
        BlockHeader* remaining = offset_to_block_nc(to_ptr_nc(block), size - block_header_overhead);
        const size_t remain_size = get_size(block) - (size + block_header_overhead);
        dsa_assert(to_ptr(remaining) == align_ptr(to_ptr(remaining), BLOCK_ALIGNMENT)
                   && "remaining block not aligned properly");
        set_size(remaining, remain_size);
        dsa_assert(get_size(remaining) >= block_size_min && "block split with invalid size");
        set_size(block, size);
        mark_as_free(remaining);
        return remaining;
    }

    /**
     * @brief Absorbs the second block into the first.
     *
     * Merges a block with its successor by adding the size of the second block (plus header
     * overhead) to the first block and relinking the next block.
     *
     * @param prev Pointer to the preceding block which will absorb the next block.
     * @param block Pointer to the block to be absorbed.
     * @return Pointer to the updated block after absorption.
     */
    static inline BlockHeader* absorb(BlockHeader* prev, BlockHeader* block)
    {
        dsa_assert(!is_last(prev));
        set_size(prev, get_size(prev) + get_size(block) + block_header_overhead);
        link_next(prev);
        return prev;
    }

    /**
     * @brief Merges the current block with its previous free neighbor.
     *
     * If the previous physical block is free, it is removed from the free list and merged with the
     * current block.
     *
     * @param control Pointer to the TLSF control structure.
     * @param block Pointer to the current block header.
     * @return Pointer to the merged block.
     */
    static inline BlockHeader* merge_prev(Control* control, BlockHeader* block)
    {
        if(is_prev_free(block))
        {
            BlockHeader* p = prev(block);
            dsa_assert(p && "prev physical block can't be null");
            dsa_assert(is_free(p) && "prev block is not free though marked as such");
            remove(control, p);
            block = absorb(p, block);
        }
        return block;
    }

    /**
     * @brief Merges the current block with its next free neighbor.
     *
     * If the next physical block is free, it is removed from the free list and merged with the
     * current block.
     *
     * @param control Pointer to the TLSF control structure.
     * @param block Pointer to the current block header.
     * @return Pointer to the merged block.
     */
    static inline BlockHeader* merge_next(Control* control, BlockHeader* block)
    {
        BlockHeader* n = next(block);
        dsa_assert(n && "next physical block can't be null");
        if(is_free(n))
        {
            dsa_assert(!is_last(block) && "previous block can't be last");
            remove(control, n);
            block = absorb(block, n);
        }
        return block;
    }

    /**
     * @brief Trims a free block to a specified size.
     *
     * If the free block is large enough to be split, this function splits the block,
     * adjusts the next block pointer, marks the remaining block as free, and inserts it into the
     * free list.
     *
     * @param control Pointer to the TLSF control structure.
     * @param block Pointer to the free block to trim.
     * @param size The desired allocation size.
     */
    static inline void trim_free(Control* control, BlockHeader* block, size_t size)
    {
        dsa_assert(is_free(block) && "block must be free");
        if(can_split(block, size))
        {
            BlockHeader* remaining_block = split(block, size);
            link_next(block);
            set_prev_free(remaining_block);
            insert(control, remaining_block);
        }
    }

    /**
     * @brief Trims a used block to the specified allocation size.
     *
     * If the used block can be split, this function divides the block, marks the remaining part
     * as used, merges it with the next free block if possible, and reinserts it into the free list.
     *
     * @param control Pointer to the TLSF control structure.
     * @param block Pointer to the used block to trim.
     * @param size The requested allocation size.
     */
    static inline void trim_used(Control* control, BlockHeader* block, size_t size)
    {
        dsa_assert(!is_free(block) && "block must be free");
        if(can_split(block, size))
        {
            BlockHeader* remaining_block = split(block, size);
            set_prev_used(remaining_block);
            remaining_block = merge_next(control, remaining_block);
            insert(control, remaining_block);
        }
    }

    /**
     * @brief Trims the leading portion of a free block.
     *
     * Splits off a leading segment from a free block, adjusts pointers accordingly, and inserts
     * the remainder back into the free list.
     *
     * @param control Pointer to the TLSF control structure.
     * @param block Pointer to the original free block.
     * @param size The desired size for the leading segment.
     * @return Pointer to the remaining block after trimming.
     */
    static inline BlockHeader* trim_free_leading(Control* control, BlockHeader* block, size_t size)
    {
        BlockHeader* remaining_block = block;
        if(can_split(block, size))
        {
            remaining_block = split(block, size - block_header_overhead);
            set_prev_free(remaining_block);
            link_next(block);
            insert(control, block);
        }
        return remaining_block;
    }

    /**
     * @brief Locates a free block of sufficient size from the free list.
     *
     * Searches for a free block that meets or exceeds the requested size and removes it from the
     * free list.
     *
     * @param control Pointer to the TLSF control structure.
     * @param size The minimum required block size.
     * @return Pointer to a suitable free block, or null if none is found.
     */
    static inline BlockHeader* locate_free(Control* control, size_t size)
    {
        int fl = 0, sl = 0;
        BlockHeader* block = 0;
        if(size)
        {
            mapping_search(size, &fl, &sl);
            if(fl < FL_INDEX_COUNT) block = search_suitable_block(control, &fl, &sl);
        }
        if(block)
        {
            dsa_assert(get_size(block) >= size);
            remove_free_block(control, block, fl, sl);
        }
        return block;
    }

    /**
     * @brief Prepares a free block for allocation by marking it as used.
     *
     * Trims the block to the requested size, marks it as used, and returns a pointer to the usable
     * memory region. If the block is null, this function returns null.
     *
     * @param control Pointer to the TLSF control structure.
     * @param block Pointer to the free block to prepare.
     * @param size The desired allocation size.
     * @return Pointer to the allocated memory, or null if block is null.
     */
    static inline void* prepare_used(Control* control, BlockHeader* block, size_t size)
    {
        void* p = nullptr;
        if(block)
        {
            dsa_assert(size && "size must be non-zero");
            trim_free(control, block, size);
            mark_as_used(block);
            p = to_ptr_nc(block);
        }
        return p;
    }

    /**
     * @brief Initializes the TLSF allocator control structure.
     *
     * Sets up the initial state of the allocator, including the free list pointers and bitmaps.
     *
     * @param control Pointer to the control structure to initialize.
     */
    static inline void initialise_control(Control* control)
    {
        int i, j;

        control->block_null.next_free = &control->block_null;
        control->block_null.prev_free = &control->block_null;

        control->fl_bitmap = 0;
        for(i = 0; i < FL_INDEX_COUNT; ++i)
        {
            control->cabinets[i].sl_bitmap = 0;
            for(j = 0; j < SLI_COUNT; ++j)
            {
                control->cabinets[i].shelves[j] = &control->block_null;
            }
        }
    }

    /**
     * @brief Default walker callback function.
     *
     * This function is used as a default callback for iterating over memory blocks.
     *
     * @param ptr Pointer to the memory block.
     * @param size The size of the memory block in bytes.
     * @param used Flag indicating if the block is used (non-zero) or free (zero).
     * @param user User-defined parameter for callback context.
     */
    static inline void default_walker(void* ptr, size_t size, int used, void* user)
    {
        (void)user;
        LOG::INFO("\t%p %s size: %x (%p)\n", ptr, used ? "used" : "free", (unsigned int)size,
                  from_ptr_nc(ptr));
    }

    /**
     * @brief Checks the consistency of the free lists and bitmaps in the TLSF allocator.
     *
     * This function iterates over all cabinets and shelves in the control structure to
     * verify that the free lists and associated bitmaps accurately reflect the state
     * of the allocator.
     *
     * @param control The TLSF control structure.
     * @return An integer status code (0 indicates success).
     */
    static inline int check(Control* control)
    {
        int i, j;

        int status = 0;

        /* Check that the free lists and bitmaps are accurate. */
        for(i = 0; i < cabinets(); ++i)
        {
            for(j = 0; j < shelves(); ++j)
            {
                const int fl_map = control->fl_bitmap & (1U << i);
                const int sl_list = control->cabinets[i].sl_bitmap;
                const int sl_map = sl_list & (1U << j);
                const BlockHeader* block = control->cabinets[i].shelves[j];

                /* Check that first- and second-level lists agree. */
                if(!fl_map)
                {
                    dsa_insist(!sl_map && "second-level map must be null");
                }

                if(!sl_map)
                {
                    dsa_insist(block == &control->block_null && "block list must be null");
                    continue;
                }

                /* Check that there is at least one free block. */
                dsa_insist(sl_list && "no free blocks in second-level map");
                dsa_insist(block != &control->block_null && "block should not be null");

                while(block != &control->block_null)
                {
                    int fli, sli;
                    dsa_insist(is_free(block) && "block should be free");
                    dsa_insist(!is_prev_free(block) && "blocks should have coalesced");
                    dsa_insist(!is_free(next_const(block)) && "blocks should have coalesced");
                    dsa_insist(is_prev_free(next_const(block)) && "block should be free");
                    dsa_insist(get_size(block) >= min_block_size() && "block not minimum size");

                    mapping_insert(get_size(block), &fli, &sli);
                    dsa_insist(fli == i && sli == j && "block size indexed in wrong list");
                    block = block->next_free;
                }
            }
        }

        return status;
    }

    // Interface Functions
    /// Returns the number of first-level indices (cabinets) in the allocator.
    static constexpr inline size_t cabinets() { return FL_INDEX_COUNT; }

    /// Returns the number of shelves per cabinet.
    static constexpr inline size_t shelves() { return SLI_COUNT; }

    /// Returns the total number of shelves in the allocator.
    static constexpr inline size_t total_shelves() { return SLI_COUNT * FL_INDEX_COUNT; }

    /// Returns the size of the allocator's control structure.
    static constexpr inline size_t size() { return sizeof(Control); }

    /// Returns the alignment size used by the allocator.
    static constexpr inline size_t align_size() { return BLOCK_ALIGNMENT; }

    /// Returns the minimum allowable block size.
    static constexpr inline size_t min_block_size() { return block_size_min; }

    /// Returns the maximum allowable block size.
    static constexpr inline size_t max_block_size() { return block_size_max; }

    /// Returns the overhead (in bytes) of the memory pool due to allocator internal bookkeeping.
    static constexpr inline size_t pool_overhead() { return 2 * block_header_overhead; }

    /// Returns the overhead (in bytes) incurred during each allocation.
    static constexpr inline size_t alloc_overhead() { return block_header_overhead; }

    /// Returns the block size in bytes.
    static inline size_t block_size(void* ptr)
    {
        size_t size = 0;
        if(ptr)
        {
            const BlockHeader* block = from_ptr_nc(ptr);
            size = get_size(block);
        }
        return size;
    }

#if defined(EALLOC_ENABLE_OWNERSHIP_TAG) && EALLOC_ENABLE_OWNERSHIP_TAG
    /**
     * @brief Sets the ownership tag for a block.
     * @param block Pointer to the block header.
     * @param tag 32-bit ownership tag (e.g., task/thread ID).
     */
    static inline void set_owner_tag(BlockHeader* block, uint32_t tag)
    {
        if (block)
            block->owner_tag = tag;
    }

    /**
     * @brief Gets the ownership tag of a block.
     * @param block Pointer to the block header.
     * @return 32-bit ownership tag (e.g., task/thread ID), or 0 if block is null.
     */
    static inline uint32_t get_owner_tag(const BlockHeader* block)
    {
        return block ? block->owner_tag : 0;
    }
#endif
};
} // namespace dsa