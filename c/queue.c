/**
 * @file queue.c
 * @brief queue by list.
 * @author mopp
 * @version 0.1
 * @date 2014-04-24
 */

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "queue.h"


Queue* init_queue(Queue* q, size_t size, release_func f, bool is_data_pointer) {
    assert(q != NULL);

    q->list = (List*)malloc(sizeof(List));
    q->is_data_type_pointer = is_data_pointer;

    list_init(q->list, size, f);

    return q;
}


bool is_queue_empty(Queue const* q) {
    assert(q != NULL);

    return (get_list_size(q->list) == 0) ? true : false;
}


void* get_queue_first(Queue* q) {
    assert(q != NULL);

    if (true == is_queue_empty(q)) {
        return NULL;
    }

    return q->list->node->data;
}


void delete_queue_first(Queue* q) {
    assert(q != NULL);

    if (true == is_queue_empty(q)) {
        return;
    }

    delete_list_node(q->list, q->list->node);
}


void* enqueue(Queue* q, void* data) {
    assert(q != NULL && data != NULL);

    insert_list_node_last(q->list, data);

    return data;
}


void* dequeue(Queue* q) {
    assert(q != NULL);

    if (true == is_queue_empty(q)) {
        return NULL;
    }

    void* t = NULL;
    if (q->is_data_type_pointer == false) {
        t = get_queue_first(q);
    } else {
        t = malloc(q->list->data_type_size);
        memcpy(t, get_queue_first(q), q->list->data_type_size);
    }


    delete_queue_first(q);

    return t;
}


void destruct_queue(Queue* q) {
    assert(q != NULL);

    destruct_list(q->list);
    free(q->list);
}


size_t get_queue_size(Queue const* q) {
    assert(q != NULL);

    return get_list_size(q->list);
}


/* ---------------------------------------------------------------------------------------------------- */
#ifndef NDEBUG

#include <stdio.h>
#include <string.h>

#define MAX_SIZE 10
static int test_array[MAX_SIZE] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
static char const* test_words[] = {"Apple", "Orange", "Banana", "Lemon", "Lime", "Strawberry"};
#define TEST_WORDS_SIZE (sizeof(test_words) / sizeof(test_words[0]))
static int const check_size = MAX_SIZE;


static void str_release_func(void* d) {
    free(*(char**)d);
    free(d);
}


int main(void) {
    Queue q;
    Queue* const qp = &q;
    init_queue(qp, sizeof(int), NULL, false);

    assert(dequeue(qp) == NULL);
    assert(get_queue_first(qp) == NULL);
    assert(get_queue_size(qp) == 0);

    printf("Enqueue -----------------------\n");
    for (int i = 0; i < check_size; i++) {
        enqueue(qp, &test_array[i]);
        printf("%d ", test_array[check_size - i - 1]);
        assert(*(int*)get_queue_first(qp) == test_array[0]);
    }
    assert(get_queue_size(qp) == check_size);
    printf("\n-------------------------------\n");

    printf("Dequeue -----------------------\n");
    for (int i = 0; i < check_size; i++) {
        int n = *(int*)dequeue(qp);
        assert(n == test_array[i]);
        printf("%d ", n);
    }
    assert(get_queue_size(qp) == 0);
    printf("\n-------------------------------\n");

    destruct_queue(qp);

    printf("Release func-------------------\n");
    init_queue(qp, sizeof(char*), str_release_func, true);
    for (int i = 0; i < TEST_WORDS_SIZE; i++) {
        char* c = (char*)malloc(strlen(test_words[i]));
        strcpy(c, test_words[i]);
        printf("Enqueue and Dequeue: %s\n", test_words[i]);
        enqueue(qp, &c);
        assert(strcmp(*(char**)dequeue(qp), test_words[i]) == 0);
    }
    assert(get_queue_size(qp) == 0);
    destruct_queue(qp);
    assert(get_queue_size(qp) == 0);
    printf("Relese All element\n");
    printf("-------------------------------\n");

    return 0;
}


#endif
/* ---------------------------------------------------------------------------------------------------- */
