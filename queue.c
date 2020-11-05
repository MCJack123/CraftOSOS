#include "queue.h"
#include <stdlib.h>

bool queue_empty(queue_t * queue) {
    return queue->_back == NULL;
}

size_t queue_size(queue_t * queue) {
    int i = 0;
    for (struct pointer_type * current = queue->_front; current != NULL; current = current->next) i++;
    return i;
}

const char * queue_front(queue_t * queue) {
    if (queue->_front == NULL) return NULL;
    return queue->_front->data;
}

const char * queue_back(queue_t * queue) {
    if (queue->_back == NULL) return NULL;
    return queue->_back->data;
}

void queue_push(queue_t * queue, const char * val) {
    struct pointer_type * p = (struct pointer_type*)malloc(sizeof(struct pointer_type));
    p->data = val;
    p->next = NULL;
    if (queue->_back != NULL && queue->_front != NULL) queue->_back->next = p;
    queue->_back = p;
    if (queue->_front == NULL) queue->_front = p;
}

void queue_pop(queue_t * queue) {
    if (queue->_front == NULL) return;
    struct pointer_type * p = queue->_front;
    queue->_front = p->next;
    if (queue->_front == NULL) queue->_back = NULL;
    free(p);
}