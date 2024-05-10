#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <set>
#include "AVLTree.cpp"  // lazy


#define container_of(ptr, type, member) ({                  \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type, member) );})

// ptr: This is a pointer to the member within a structure. The macro expects a pointer to a member of a structure.
// type: This is the type of the structure that contains the member pointed to by ptr. It specifies the data type of the structure.
// member: This is the name of the member within the structure that ptr points to. It indicates which member of the structure the pointer ptr refers to.

struct Data {
    AVLNode node;
    uint32_t val = 0; //value of the node
};

struct Container {
    AVLNode *root = NULL;
};

// inserts a new node with the value val into the AVL tree stored in the Container object c
static void add(Container &c, uint32_t val) {
	Data *data = new Data();
	avl_init(&data->node);
	data->val=val;
	
	AVLNode *cur = NULL; // a pointer cur that will be used to traverse the AVL tree during insertion.
	// This initializes a pointer from to the address of the root pointer (root) of the AVL tree stored in the Container object c. 
	// This pointer will be updated during tree traversal.
	AVLNode **from = &c.root;   // the incoming pointer to the next node
	while (*from) {             // tree search as long as the pointer from is not NULL
		cur = *from;
		uint32_t node_val = container_of(cur,Data,node)->val;
		from = (val<node_val)? &cur->left: &cur->right;
	}
	*from = &data->node;
	data->node.parent= cur;
	c.root=avl_fix(&data->node);
}

static bool del(Container &c, uint32_t val){
	AVLNode *cur = c.root;
	while(cur){
		uint32_t node_val = container_of(cur,Data,node)->val;
		if(val==node_val){
			break;
		}
		cur = val<node_val?cur->left:cur->right;
	}
	if(!cur) // if cur is null after the loop that is that node is not there.
		return false;
	c.root= avl_del(cur);
	delete container_of(cur,Data,node);
	return true;
}

// avl_verify function checks various properties of the AVL tree to ensure its integrity and adherence to AVL tree invariants
static void avl_verify(AVLNode *parent, AVLNode *node){
	if(!node)
		return;
	// checks if parent and current nodes parents are same
	
	// 1. The parent pointer is correct.
	assert(parent==node->parent);
	// verify subtrees recursively
	avl_verify(node,node->left);
	avl_verify(node,node->right);
	
	// 2. The auxiliary data is correct.
	assert(node->cnt == 1 + avl_cnt(node->left) + avl_cnt(node->right));
	uint32_t l = avl_depth(node->left);
    uint32_t r = avl_depth(node->right);
    
    // 3. The height invariant is OK.
    assert(l == r || l + 1 == r || l == r + 1);
    assert(node->depth == 1 + max(l, r)); // if depth is properly updated or not
     
    // 4. The data is ordered.
    uint32_t val = container_of(node,Data,node)->val;
    if(node->left){
    	assert(node->left->parent==node);
    	assert(container_of(node->left,Data,node)->val<=val);
	}
	if(node->right){
		assert(node->right->parent==node);
		assert(container_of(node->right,Data,node)->val>=val);
	}
}

// this function recursively traverses the AVL tree in an inorder manner (left, root, right), extracting values from each node and inserting them into a multiset.
static void extract(AVLNode *node, std::multiset<uint32_t> &extracted){
	if(node==NULL)
		return;
	extract(node->left, extracted);
    extracted.insert(container_of(node, Data, node)->val);
    extract(node->right, extracted);
}

// this function verifies the integrity of the AVL tree stored in the Container c by comparing it with a reference multiset ref, 
// ensuring that they contain the same set of values and maintain AVL tree properties.
static void container_verify(Container &c, const std::multiset<uint32_t> &ref){
    avl_verify(NULL, c.root);
    assert(avl_cnt(c.root) == ref.size());
    std::multiset<uint32_t> extracted;
    extract(c.root, extracted);
    assert(extracted == ref);
}

// dispose function is responsible for deallocating memory associated with both the AVL tree
static void dispose(Container &c){
	while(c.root){
		AVLNode *node = c.root;
        c.root = avl_del(c.root);
        delete container_of(node, Data, node);
	}
}

static void test_insert(uint32_t sz) {
    for (uint32_t val = 0; val < sz; ++val) {
        Container c;
        std::multiset<uint32_t> ref;
        for (uint32_t i = 0; i < sz; ++i) {
            if (i == val) {
                continue;
            }
            add(c, i);
            ref.insert(i);
        }
        container_verify(c, ref);

        add(c, val);
        ref.insert(val);
        container_verify(c, ref);
        dispose(c);
    }
}

static void test_insert_dup(uint32_t sz) {
    for (uint32_t val = 0; val < sz; ++val) {
        Container c;
        std::multiset<uint32_t> ref;
        for (uint32_t i = 0; i < sz; ++i) {
            add(c, i);
            ref.insert(i);
        }
        container_verify(c, ref);

        add(c, val);
        ref.insert(val);
        container_verify(c, ref);
        dispose(c);
    }
}

static void test_remove(uint32_t sz) {
    for (uint32_t val = 0; val < sz; ++val) {
        Container c;
        std::multiset<uint32_t> ref;
        for (uint32_t i = 0; i < sz; ++i) {
            add(c, i);
            ref.insert(i);
        }
        container_verify(c, ref);

        assert(del(c, val));
        ref.erase(val);
        container_verify(c, ref);
        dispose(c);
    }
}

int main() {
    Container c;

    // some quick tests
    container_verify(c, {});
    add(c, 123);
    container_verify(c, {123});
    assert(!del(c, 124));
    assert(del(c, 123));
    container_verify(c, {});

    // sequential insertion
    std::multiset<uint32_t> ref;
    for (uint32_t i = 0; i < 1000; i += 3) {
        add(c, i);
        ref.insert(i);
        container_verify(c, ref);
    }

    // random insertion
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t val = (uint32_t)rand() % 1000;
        add(c, val);
        ref.insert(val);
        container_verify(c, ref);
    }

    // random deletion
    for (uint32_t i = 0; i < 200; i++) {
        uint32_t val = (uint32_t)rand() % 1000;
        auto it = ref.find(val);
        if (it == ref.end()) {
            assert(!del(c, val));
        } else {
            assert(del(c, val));
            ref.erase(it);
        }
        container_verify(c, ref);
    }

    // insertion/deletion at various positions
    for (uint32_t i = 0; i < 200; ++i) {
        test_insert(i);
        test_insert_dup(i);
        test_remove(i);
    }

    dispose(c);
    return 0;
}