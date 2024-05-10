#include <assert.h>
#include <string.h>
#include <stdlib.h>
// proj
#include "zset.h"
#include "common.h"

static ZNode *znode_new(const char *name, size_t len, double score){
	ZNode *node = (ZNode *)malloc(sizeof(ZNode) + len);
	assert(node);
	avl_init(&node->tree);
	node->hmap.next = null;
	node->hmap.hcode = str_hash((uint8_t *)name, len);
	node->score=score;
	node->len = len;
	memcpy(&node->name[0], name, len);
	return node;
}

static uint32_t min(size_t lhs, size_t rhs) {
    return lhs < rhs ? lhs : rhs;
}

// a helper structure for the hashtable lookup
struct HKey {
    HNode node;
    const char *name = NULL;
    size_t len = 0;
};

// compare the names stored in ZNode objects with the names stored in HKey object
static bool hcmp(HNode *node, HNode *key){
	ZNode *znode = container_of(node,ZNode, hmap);
	HKey *hkey = container_of(key, HKey,node);
	if(znode->len != hkey->len)
		return false;
	return 0 == memcmp(znode->name,hkey->name,znode->len);
}

// lookup by name
ZNode *zset_lookup(ZSet *zset, const char *name, size_t len){
	if(!zset->tree)
		return NULL;
	
	HKey key;
	key.node.hcode= str_hash((uint8_t *)name, len);
	key.name = name;
	key.len = len;
	HNode *found = hm_lookup(&zset->hmap, &key.node, &hcmp);
	return found? container_of(found,ZNode,hmap):NULL;
}