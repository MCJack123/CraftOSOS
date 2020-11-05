#include <stddef.h>
#include <stdbool.h>

struct pointer_type {
    const char * data;
    struct pointer_type * next;
};

typedef struct queue {
    struct pointer_type * _front;
    struct pointer_type * _back;
} queue_t;

bool queue_empty(queue_t*);
size_t queue_size(queue_t*);
const char * queue_front(queue_t*);
const char * queue_back(queue_t*);
void queue_push(queue_t*, const char *);
void queue_pop(queue_t*);