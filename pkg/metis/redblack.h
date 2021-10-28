#ifndef _RBT_H
#define _RBT_H
#include "mr-def.h"

typedef void * ValType;

typedef void * KeyType;            // type of key

typedef enum { BLACK, RED } nodeColor;

typedef enum {
    RBT_STATUS_OK,
    RBT_STATUS_MEM_EXHAUSTED,
    RBT_STATUS_DUPLICATE_KEY,
    RBT_STATUS_KEY_NOT_FOUND
} RbtStatus;

typedef struct NodeTag {
    struct NodeTag *left;       // left child
    struct NodeTag *right;      // right child
    struct NodeTag *parent;     // parent
    nodeColor color;            // node color (BLACK, RED)
    KeyType key;                // key used for searching
    ValType val;                // data related to key
} NodeType;

typedef struct rbtree {
    NodeType *root;
    int nnode;
    key_cmp_t cmpfn;
}RedBlackTree;

NodeType *rbtFind(RedBlackTree *tree, KeyType key);
RbtStatus rbtInsert(RedBlackTree *tree, KeyType key, ValType val);
void rbtInorder(RedBlackTree *tree, void *arg, void (callback)(void *, NodeType *));
void rbtDestroy(RedBlackTree *tree);
void rbtInit(RedBlackTree *tree, key_cmp_t cmpfn);
int rbtGetNodes(RedBlackTree *tree);

#endif
