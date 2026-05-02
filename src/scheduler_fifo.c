#include "scheduler.h"
#include "queue.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Implementacion FIFO del planificador.
 *
 * Con --priority activo, los camiones perecederos se insertan antes del
 * primer no-perecedero de la cola (via queue_push_priority). Entre si,
 * los perecederos mantienen FIFO. Sin --priority, inserta al final siempre.
 */
typedef struct {
    Queue queue;
    bool  priority_enabled;
} FifoState;

static void fifo_init(Scheduler *self, const SimConfig *cfg) {
    FifoState *st = (FifoState *)self->state;
    st->priority_enabled = cfg->priority_enabled;
    if (queue_init(&st->queue) != 0) {
        fprintf(stderr, "scheduler_fifo: fallo queue_init\n");
    }
}

static void fifo_enqueue(Scheduler *self, Truck *t) {
    FifoState *st = (FifoState *)self->state;
    if (st->priority_enabled && t->cargo == CARGO_PERISHABLE) {
        queue_push_priority(&st->queue, t);
    } else {
        queue_push(&st->queue, t);
    }
}

static Truck *fifo_next(Scheduler *self) {
    FifoState *st = (FifoState *)self->state;
    return queue_pop_fifo(&st->queue);
}

/* FIFO no hace context switch: una vez que un camion empieza, corre hasta terminar. */
static bool fifo_cont_switch(Scheduler *self, const Truck *running) {
    (void)self;
    (void)running;
    return false;
}

static int fifo_quantum_ms(Scheduler *self) {
    (void)self;
    return 0;
}

static void fifo_destroy(Scheduler *self) {
    FifoState *st = (FifoState *)self->state;
    if (st == NULL) return;
    queue_close(&st->queue);
    queue_destroy(&st->queue);
    free(st);
    self->state = NULL;
}

Scheduler *scheduler_fifo_create(const SimConfig *cfg) {
    if (cfg == NULL) return NULL;

    Scheduler *s = (Scheduler *)calloc(1, sizeof(Scheduler));
    if (s == NULL) {
        perror("scheduler_fifo_create: calloc Scheduler");
        return NULL;
    }

    FifoState *st = (FifoState *)calloc(1, sizeof(FifoState));
    if (st == NULL) {
        perror("scheduler_fifo_create: calloc state");
        free(s);
        return NULL;
    }

    s->name        = "FIFO";
    s->init        = fifo_init;
    s->enqueue     = fifo_enqueue;
    s->next        = fifo_next;
    s->cont_switch = fifo_cont_switch;
    s->quantum_ms  = fifo_quantum_ms;
    s->destroy     = fifo_destroy;
    s->state       = st;

    s->init(s, cfg);
    return s;
}
