#pragma once

#include <stddef.h>
#include <stdint.h>


struct HeapItem {
    // val holds the ttl time in microsecond
    uint64_t val = 0; // This member holds the value that is used for sorting within the heap. It is initialized to 0.
    size_t *ref = NULL; //  This member is used to store the reference to the position of this HeapItem in the heap. It is initialized to NULL
};

void heap_update(HeapItem *a, size_t pos, size_t len);
