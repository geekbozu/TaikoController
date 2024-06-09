#ifndef INPUTQUEUE_H
#define INPUTQUEUE_H

#include "pico/util/queue.h"
#include <pico/platform.h>
typedef struct queueItem
{
    float r_ka;
    float r_don;
    float l_don;
    float l_ka;
};

extern queue_t queue;
extern
#endif /* INPUTQUEUE_H */