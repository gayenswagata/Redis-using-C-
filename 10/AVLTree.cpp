#include <stddef.h>
#include <stdint.h>

struct AVLNode{
	uint32_t depth = 0;     // subtree height
    uint32_t cnt = 0;       // subtree size
    AVLNode *left = NULL;
    AVLNode *right = NULL;
    AVLNode *parent = NULL;
};

static void avl_init(AVLNode *node) {
    node->depth = 1;
    node->cnt = 1;
    node->left = node->right = node->parent = NULL;
};

static uint32_t avl_depth(AVLNode *node) {
    return node ? node->depth : 0;
}
static uint32_t avl_cnt(AVLNode *node) {
    return node ? node->cnt : 0;
}
static uint32_t max(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? rhs : lhs;
}

// maintain the depth and cnt field
static void avl_update(AVLNode *node) {
    node->depth = 1 + max(avl_depth(node->left), avl_depth(node->right));
    node->cnt = 1 + avl_cnt(node->left) + avl_cnt(node->right);
}

static AVLNode *rot_left(AVLNode *node){
	AVLNode *new_node = node->right;
	if(new_node->left){
		new_node->left->parent=node;
	}
	node->right=new_node->left; //rotation
	new_node->left=node; //rotation
	new_node->parent=node->parent;
	node->parent=new_node;
	avl_update(node);
	avl_update(new_node);
	return new_node;
}

static AVLNode *rot_right(AVLNode *node){
	AVLNode *new_node = node->left;
	if(new_node->right){
		new_node->right->parent=node;
	}
	node->left = new_node->right; // rotation
	new_node->right = node; // rotation
	new_node->parent=node->parent;
	node->parent=new_node;
	avl_update(node);
	avl_update(new_node);
	return new_node;
}

// the left subtree is too deep
static AVLNode *avl_fix_left(AVLNode *root) {
    if (avl_depth(root->left->left) < avl_depth(root->left->right)) {
        root->left = rot_left(root->left);
    }
    return rot_right(root);
}

// the right subtree is too deep
static AVLNode *avl_fix_right(AVLNode *root) {
    if (avl_depth(root->right->right) < avl_depth(root->right->left)) {
        root->right = rot_right(root->right);
    }
    return rot_left(root);
}

// fix imbalanced nodes and maintain invariants until the root is reached
static AVLNode *avl_fix(AVLNode *node){
	while(true){
		avl_update(node);
		uint32_t l = avl_depth(node->left);
		uint32_t r = avl_depth(node->right);
		AVLNode **from = NULL;
		if(node->parent){
			from = (node->parent->left==node)?&node->parent->left : &node->parent->right;
		}
		if(l==r+2){
			node=avl_fix_left(node);
		}else if (l + 2 == r) {
            node = avl_fix_right(node);
        }
        if (!from) { //if it has reached the root
            return node;
        }
        *from = node;
        node = node->parent;
	}
}

// detach a node and returns the new root of the tree
static AVLNode *avl_del(AVLNode *node){
	if(node->right==NULL){
		// no right subtree, replace the node with the left subtree
        // link the left subtree to the parent
        // If the node has no right subtree,
		// it means it's either a leaf node or has only a left subtree. In this case, we will replace the node with its left subtree and link the left subtree to the parent.
        AVLNode *parent = node->parent;
        if (node->left) {
            node->left->parent = parent;
        }
        if(parent){
        	// attach the left subtree to the parent
        	(parent->left==node)?parent->left:parent->right = node->left;
        	return avl_fix(parent);
		}else{ // if deleting the root
			return node->left;
		}
	}else{ //detach the successor
		AVLNode *victim = node->right;
		while(victim->left){ // searching the smallest node in the right subtree
			victim=victim->left;
		}
		AVLNode *root = avl_del(victim);
		//keeping the child of victim attached to it.
		if(victim->left){
			victim->left->parent=victim;
		}if(victim->right){
			victim->right->parent=victim;
		}
		AVLNode *parent = node->parent;
		//now swapping victim with node, so BST will be maintained and and node will be deleted.
		if(parent){
			(parent->left==node)?parent->left:parent->right = victim;
			return root;
		}else{
			// deleting root?
			return victim;
		}
	}
}