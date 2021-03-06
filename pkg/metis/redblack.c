#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "redblack.h"

/////////////////////////////////////
// implementation independent code //
/////////////////////////////////////

#define SENTINEL &sentinel      // all leafs are sentinels
static NodeType sentinel = { SENTINEL, SENTINEL, 0, BLACK, 0};

static void rotateLeft(NodeType **root, NodeType *x) {

    // rotate node x to left

    NodeType *y = x->right;

    // establish x->right link
    x->right = y->left;
    if (y->left != SENTINEL) y->left->parent = x;

    // establish y->parent link
    if (y != SENTINEL) y->parent = x->parent;
    if (x->parent) {
        if (x == x->parent->left)
            x->parent->left = y;
        else
            x->parent->right = y;
    } else {
        *root = y;
    }

    // link x and y
    y->left = x;
    if (x != SENTINEL) x->parent = y;
}

static void rotateRight(NodeType **root, NodeType *x) {

    // rotate node x to right

    NodeType *y = x->left;

    // establish x->left link
    x->left = y->right;
    if (y->right != SENTINEL) y->right->parent = x;

    // establish y->parent link
    if (y != SENTINEL) y->parent = x->parent;
    if (x->parent) {
        if (x == x->parent->right)
            x->parent->right = y;
        else
            x->parent->left = y;
    } else {
        *root = y;

    }

    // link x and y
    y->right = x;
    if (x != SENTINEL) x->parent = y;
}

static void insertFixup(NodeType **root, NodeType *x) {

    // maintain red-black tree balance
    // after inserting node x

    // check red-black properties
    while (x != *root && x->parent->color == RED) {
        // we have a violation
        if (x->parent == x->parent->parent->left) {
            NodeType *y = x->parent->parent->right;
            if (y->color == RED) {

                // uncle is RED
                x->parent->color = BLACK;
                y->color = BLACK;
                x->parent->parent->color = RED;
                x = x->parent->parent;
            } else {

                // uncle is BLACK
                if (x == x->parent->right) {
                    // make x a left child
                    x = x->parent;
                    rotateLeft(root, x);
                }

                // recolor and rotate
                x->parent->color = BLACK;
                x->parent->parent->color = RED;
                rotateRight(root, x->parent->parent);
            }
        } else {

            // mirror image of above code
            NodeType *y = x->parent->parent->left;
            if (y->color == RED) {

                // uncle is RED
                x->parent->color = BLACK;
                y->color = BLACK;
                x->parent->parent->color = RED;
                x = x->parent->parent;
            } else {

                // uncle is BLACK
                if (x == x->parent->left) {
                    x = x->parent;
                    rotateRight(root, x);
                }
                x->parent->color = BLACK;
                x->parent->parent->color = RED;
                rotateLeft(root, x->parent->parent);
            }
        }
    }
    (*root)->color = BLACK;
}

// insert new node (no duplicates allowed)
RbtStatus rbtInsert(RedBlackTree *tree, KeyType key, ValType val) {
    NodeType *current, *parent, *x;
    tree->nnode++;
    // allocate node for data and insert in tree
    // find future parent
    current = tree->root;
    parent = 0;
    while (current != SENTINEL) {
        if (!tree->cmpfn(key, current->key)) 
            return RBT_STATUS_DUPLICATE_KEY;
        parent = current;
        current = (tree->cmpfn(key, current->key) < 0) ?
            current->left : current->right;
    }

    // setup new node
    if ((x = malloc (sizeof(*x))) == 0)
        return RBT_STATUS_MEM_EXHAUSTED;
    x->parent = parent;
    x->left = SENTINEL;
    x->right = SENTINEL;
    x->color = RED;
    x->key = key;
    x->val = val;

    // insert node in tree
    if(parent) {
        if(tree->cmpfn(key, parent->key) < 0)
            parent->left = x;
        else
            parent->right = x;
    } else {
        tree->root = x;
    }

    insertFixup(&tree->root, x);

    return RBT_STATUS_OK;
}

static void deleteFixup(NodeType **root, NodeType *x) {

    // maintain red-black tree balance
    // after deleting node x

    while (x != *root && x->color == BLACK) {
        if (x == x->parent->left) {
            NodeType *w = x->parent->right;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                rotateLeft(root, x->parent);
                w = x->parent->right;
            }
            if (w->left->color == BLACK && w->right->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->right->color == BLACK) {
                    w->left->color = BLACK;
                    w->color = RED;
                    rotateRight(root, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->right->color = BLACK;
                rotateLeft(root, x->parent);
                x = *root;
            }
        } else {
            NodeType *w = x->parent->left;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                rotateRight(root, x->parent);
                w = x->parent->left;
            }
            if (w->right->color == BLACK && w->left->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->left->color == BLACK) {
                    w->right->color = BLACK;
                    w->color = RED;
                    rotateLeft(root, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->left->color = BLACK;
                rotateRight(root, x->parent);
                x = *root;
            }
        }
    }
    x->color = BLACK;
}

// delete node
RbtStatus rbtErase(NodeType **root, NodeType * z) {
    NodeType *x, *y;

    if (z->left == SENTINEL || z->right == SENTINEL) {
        // y has a SENTINEL node as a child
        y = z;
    } else {
        // find tree successor with a SENTINEL node as a child
        y = z->right;
        while (y->left != SENTINEL) y = y->left;
    }

    // x is y's only child
    if (y->left != SENTINEL)
        x = y->left;
    else
        x = y->right;

    // remove y from the parent chain
    x->parent = y->parent;
    if (y->parent)
        if (y == y->parent->left)
            y->parent->left = x;
        else
            y->parent->right = x;
    else {
        *root = x;
    }

    if (y != z) {
        z->key = y->key;
        z->val = y->val;
    }


    if (y->color == BLACK)
        deleteFixup (root, x);

    free (y);

    return RBT_STATUS_OK;
}

// find key
NodeType *rbtFind(RedBlackTree *tree, KeyType key) {
    NodeType *current;
    current = tree->root;
    while(current != SENTINEL) {
        if(!tree->cmpfn(key, current->key)) {
            return current;
        } else {
            current = (tree->cmpfn(key, current->key) < 0) ?
                current->left : current->right;
        }
    }
    return NULL;
}

// in-order walk of tree
static void rbtInorderTraverse(NodeType *p,void *arg, void (callback)(void *arg, NodeType *)) {
    if (p == SENTINEL) return;
    rbtInorderTraverse(p->left, arg, callback);
    callback(arg, p);
    rbtInorderTraverse(p->right, arg, callback);
}
void rbtInorder(RedBlackTree *tree, void *arg, void (callback)(void *, NodeType *))
{
    rbtInorderTraverse(tree->root, arg, callback);
}

// delete nodes depth-first
static void rbtDelete(NodeType *p) 
{
    if (p == SENTINEL) return;
    rbtDelete(p->left);
    rbtDelete(p->right);
    free(p);
}

void rbtDestroy(RedBlackTree *tree)
{
    rbtDelete(tree->root);
}

void rbtInit(RedBlackTree *tree, key_cmp_t cmpfn)
{
    tree->root = SENTINEL;
    tree->nnode = 0;
    tree->cmpfn = cmpfn;
    //printf("tree is %p, root is %p\n", tree, tree->root);
}

int rbtGetNodes(RedBlackTree *tree)
{
    return tree->nnode;
}
