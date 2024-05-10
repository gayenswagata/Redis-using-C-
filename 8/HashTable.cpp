#include <assert.h>
#include <stdlib.h>
#include "hashtable.h"
// n must be a power of 2
static void h_init(HTab *htab, size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0);
    htab->tab = (HNode **)calloc(sizeof(HNode *), n);
    htab->mask = n - 1; //for calculating the index of the bucket in the hash table.
    htab->size = 0;
}

// hashtable insertion
static void h_insert(HTab *htab, HNode *node) {
    size_t pos = node->hcode & htab->mask;
    HNode *next = htab->tab[pos];
    node->next = next;
    htab->tab[pos] = node;
    htab->size++;
}

// hashtable look up subroutine.
// Pay attention to the return value. It returns the address of
// the parent pointer that owns the target node,
// which can be used to delete the target node.
static HNode **h_lookup(HTab *htab, HNode *key, bool (*eq)(HNode *, HNode *)) {
	// Pointer to a function that compares two keys for equality. It takes two HNode* arguments and returns a boolean value indicating whether the keys are equal.
    if (!htab->tab) {
        return NULL;
    }

    size_t pos = key->hcode & htab->mask; //This ensures that the position falls within the bounds of the hash table's array.
    HNode **from = &htab->tab[pos];     // incoming pointer to the result
    for (HNode *cur; (cur = *from) != NULL; from = &cur->next) {
        if (cur->hcode == key->hcode && eq(cur, key)) {
            return from;
        }
    }
    return NULL;
}

static HNode *h_detach(HTab *htab, HNode **from) {
    HNode *node = *from; // here node needs to be detached or deleted.
    *from = node->next; // so if we replace the from with the next pointer of node it will be done
    htab->size--; //have to decrease size of hash table
    return node; // After detaching the node, the function returns a pointer to the detached node. This allows the caller to perform further operations on the detached node if needed.
}

//struct HMap {
//    HTab ht1;   // newer hash table
//    HTab ht2;   // older hash table
//    size_t resizing_pos = 0; // position used during resizing
//    // This variable is used to keep track of the position during resizing operations. It is used to move elements from ht2 to ht1.
//};

const size_t k_max_load_factor = 8;
const size_t k_resizing_work = 128; // constant work

void hm_start_resizing(HMap *hmap){
	assert(hmap->ht2.tab == NULL);
    // create a bigger hashtable and swap them
    hmap->ht2 = hmap->ht1;
    h_init(&hmap->ht1,(hmap->ht1.mask+1)*2); //doubling the size
    hmap->resizing_pos = 0; 
	//Resetting it ensures that the resizing process starts from the beginning of the table, allowing all elements to be transferred to the new table correctly.
}

void hm_help_resizing(HMap *hmap){
	size_t nwork = 0; // initializes a counter nwork to keep track of the number of resizing operations performed in the current iteration.
	while(nwork < k_resizing_work && hmap->ht2.size > 0){
		// scan for nodes from ht2 and move them to ht1
		HNode **from = &hmap->ht2.tab[hmap->resizing_pos];
		if (!*from) { // if there is no node the resizing_pos is incremented to move to the next position in the table, and the loop continues.
            hmap->resizing_pos++;
            continue;
        }
        // If there is a node at the current position in the older table, it is detached from ht2 and inserted into the newer table ht1
        h_insert(&hmap->ht1, h_detach(&hmap->ht2,from));
        nwork++;
	}
	// If there are no more elements left in the older table (ht2) and it still has allocated memory (ht2.tab is not null), it is freed to release memory
	if (hmap->ht2.size == 0 && hmap->ht2.tab) {
        // done
        free(hmap->ht2.tab);
        hmap->ht2 = HTab{};
    }
}

void hm_insert(HMap *hmap, HNode *node){
	if(!hmap->ht1.tab){
		h_init(&hmap->ht1, 4);  // 1. Initialize the table if it is empty.
	}
	h_insert(&hmap->ht1,node); // 2. Insert the key into the newer table.
	if(!hmap->ht2.tab){ // 3. Check the load factor
		 size_t load_factor = hmap->ht1.size / (hmap->ht1.mask + 1);
        if (load_factor >= k_max_load_factor){
        	hm_start_resizing(hmap);    // create a larger table
		}
	}
	hm_help_resizing(hmap);     // 4. Move some keys into the newer table.
}

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
    hm_help_resizing(hmap);
    HNode **from = h_lookup(&hmap->ht1, key, eq);
    from = from ? from : h_lookup(&hmap->ht2, key, eq);
    return from ? *from : NULL;
}