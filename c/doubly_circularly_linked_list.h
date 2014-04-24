/*
 * @file doubly_circularly_linked_list.c
 * @brief DoublyCircularlyLinkedList Header.
 * @author mopp
 * @version 0.1
 * @date 2014-04-23
 */

#ifndef _DOUBLY_CIRCULARLY_LINKED_LIST_H
#define _DOUBLY_CIRCULARLY_LINKED_LIST_H


#include <stdbool.h>

/*
 * free function for list node.
 * It is used in destruct_list().
 */
typedef void (*release_func)(void*);
/*
 * comparison function for list node.
 * It is used in search_list_node().
 */
typedef bool (*comp_func)(void*, void*);
/*
 * for each function for list node.
 * if return value is true, loop is abort
 */
typedef bool (*for_each_func)(void*);


/*
 * List node structure.
 * It is in List structure below.
 */
struct list_node {
    void* data;             /* pointer to stored data in node. */
    struct list_node* next; /* pointer to next position node. */
    struct list_node* prev; /* pointer to previous position node. */
};
typedef struct list_node List_node;


/* List structure */
struct list {
    List_node* node;       /* start position pointer to node.
                            * and XXX: this node is first, this node->prev is last.
                            */
    release_func free;     /* function for releasing allocated data. */
    size_t size;           /* the number of node. */
    size_t data_type_size; /* it provided by sizeof(data). */
};
typedef struct list List;


extern List* init_list(List*, size_t, release_func);
extern List_node* get_new_list_node(List*, void*);
extern List_node* insert_list_node_next(List*, List_node*, void*);
extern List_node* insert_list_node_prev(List*, List_node*, void*);
extern List* insert_list_node_first(List*, void*);
extern List* insert_list_node_last(List*, void*);
extern void delete_list_node(List*, List_node*);
extern void destruct_list(List*);
extern size_t get_list_size(List const*);
extern List_node* list_for_each(List* const, for_each_func const, bool const);
extern List_node* search_list_node(List*, void*);


#endif
