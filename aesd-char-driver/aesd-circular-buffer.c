/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

#undef PDEBUG             /* undef it, just in case */
#ifdef AESD_CIRCULAR_BUFFER_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "aesd_circular_buffer: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#undef PDEBUGG
#define PDEBUGG(fmt, args...) /* nothing: it's a placeholder */

/* Helper macro for incrementing the given circular buffer index, considering wraparound at the defined buffer size */
#define CIRCULAR_BUFFER_INCREMENT_INDEX(idx) (idx = ((idx + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED))

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    struct aesd_buffer_entry *entry_it;
    size_t idx;

    if(buffer == NULL || entry_offset_byte_rtn == NULL)
    {
        PDEBUG("aesd_circular_buffer_find_entry_offset_for_fpos: nullpointer given");
        return NULL;
    }

    if(!buffer->full && (buffer->out_offs == buffer->in_offs))
    {
        // buffer is empty
        return NULL;
    }

    // iterate over buffer entries until finding the entry corresponding to the given character offset
    idx = buffer->out_offs;
    do {
        entry_it = &buffer->entry[idx];
        if(char_offset < entry_it->size)
        {
            // found corresponding entry -> return current character offset and pointer to entry
            *entry_offset_byte_rtn = char_offset;
            return entry_it;
        }
        char_offset -= entry_it->size;
        CIRCULAR_BUFFER_INCREMENT_INDEX(idx);
    } while(idx != buffer->in_offs);

    // given position is not available in buffer
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    if(buffer == NULL || add_entry == NULL)
    {
        PDEBUG("aesd_circular_buffer_add_entry: nullpointer given");
        return;
    }

    // add new entry
    buffer->entry[buffer->in_offs].buffptr = add_entry->buffptr;
    buffer->entry[buffer->in_offs].size = add_entry->size;

    // advance write location
    CIRCULAR_BUFFER_INCREMENT_INDEX(buffer->in_offs);

    // if buffer is already full, also advance read position and drop oldest entry
    if(buffer->full)
        CIRCULAR_BUFFER_INCREMENT_INDEX(buffer->out_offs);

    // if read and write locations are equal, mark buffer as full
    buffer->full = (buffer->in_offs == buffer->out_offs);
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
