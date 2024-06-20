#include <stddef.h>
#include <stdint.h>
#include "heap.h"

//computes the index of the parent node for a given node in a binary heap stored as an array.
static size_t heap_parent(size_t i) {
    return (i + 1) / 2 - 1;
}

static size_t heap_left(size_t i) {
    return i * 2 + 1;
}

static size_t heap_right(size_t i) {
    return i * 2 + 2;
}

// The heap_up function is used to restore the heap property after inserting a new element at the end of the heap or modifying an element's value in the heap. (min heap property)
static void heap_up(HeapItem *a, size_t pos){
	HeapItem t = a[pos];
	// parent node's value is greater than the value of the element t
	while(pos>0 && a[heap_parent(pos)].val>t.val){
		//swap with the parent
		a[pos] = a[heap_parent(pos)]; // This line assigns the value of the parent node to the current position pos
		*a[pos].ref = pos; // a[pos].ref is a pointer to the position of the heap item. This line updates the reference to the new position pos.
		pos = heap_parent(pos); // This line updates pos to be the index of the parent node. The loop will continue, and the element t will be compared with its new parent.
	}
	a[pos] = t;
	*a[pos].ref = pos;
}

// The heap_down function ensures that the heap property is maintained by moving the element at the given position pos down the heap until it is in the correct position. 
// It repeatedly compares the element with its children, swapping it with the smaller child if necessary.(ACBT property)
static void heap_down(HeapItem *a, size_t pos, size_t len){
	HeapItem t = a[pos];
	while(true){
		// find the smallest one among the parent and their kids
		size_t l = heap_left(pos);
        size_t r = heap_right(pos);
        size_t min_pos = -1;
        size_t min_val = t.val;
        //  Check if the left child is within the heap bounds and if its value is less than the current minimum value.
        if (l < len && a[l].val < min_val){
        	min_pos = l;
        	min_val=a[l].val;
		}
		// Check if the right child is within the heap bounds and if its value is less than the current minimum value.
		if(r<len && a[r].val<min_val){
			min_pos =r;
		}
		// check for no swap case
		if (min_pos == (size_t)-1) {
            break;
        }
        a[pos] = a[min_pos];
        *a[pos].ref = pos;
        pos = min_pos;
	}
	a[pos] = t;
    *a[pos].ref = pos;
}
void heap_update(HeapItem *a, size_t pos, size_t len) {
    if (pos > 0 && a[heap_parent(pos)].val > a[pos].val) {
        heap_up(a, pos);
    } else {
        heap_down(a, pos, len);
    }
}
