#include "scheduler.h"
#include "queue.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Implementacion Round Robin del planificador.
 *
 * La cola es identica a FIFO; la diferencia esta en que quantum_ms > 0 y
 * cont_switch retorna true, lo que habilita al camion a hacer
 * self-preemption cooperativa: corre lo que puede de quantum_ms, y si le
 * queda burst, suelta el muelle y se re-encola.
 *
 * Con --priority activo, los perecederos se insertan antes del primer
 * no perecedero (igual que en FIFO).
 */

typedef struct {
    Queue queue;
    int   quantum_ms;
    bool  priority_enabled;
} RrState;

static void rr_init(Scheduler *self, const SimConfig *cfg) {
    RrState *st = (RrState *)self->state;
    st->quantum_ms       = cfg->quantum_ms;
    st->priority_enabled = cfg->priority_enabled;
    if (queue_init(&st->queue) != 0) {
        fprintf(stderr, "scheduler_rr: fallo queue_init\n");
    }
}

static void rr_enqueue(Scheduler *self, Truck *t) {
    RrState *st = (RrState *)self->state;
    if (st->priority_enabled && t->cargo == CARGO_PERISHABLE) {
        queue_push_priority(&st->queue, t);
    } else {
        queue_push(&st->queue, t);
    }
}

static Truck *rr_next(Scheduler *self) {
    RrState *st = (RrState *)self->state;
    return queue_pop_fifo(&st->queue);
}

/*
 * RR siempre indica "puede haber preemption". El camion decide si aplica
 * comparando su remaining_ms con el quantum; si remaining_ms > 0 tras el
 * trozo, se re-encola.
 */
static bool rr_cont_switch(Scheduler *self, const Truck *running) {
    (void)self;
    (void)running;
    return true;
}

static int rr_quantum_ms(Scheduler *self) {
    RrState *st = (RrState *)self->state;
    return st->quantum_ms;
}

static void rr_destroy(Scheduler *self) {
    RrState *st = (RrState *)self->state;
    if (st == NULL) return;
    queue_close(&st->queue);
    queue_destroy(&st->queue);
    free(st);
    self->state = NULL;
}

Scheduler *scheduler_rr_create(const SimConfig *cfg) {
    if (cfg == NULL) return NULL;

    Scheduler *s = (Scheduler *)calloc(1, sizeof(Scheduler));
    if (s == NULL) {
        perror("scheduler_rr_create: calloc Scheduler");
        return NULL;
    }

    RrState *st = (RrState *)calloc(1, sizeof(RrState));
    if (st == NULL) {
        perror("scheduler_rr_create: calloc state");
        free(s);
        return NULL;
    }

    s->name        = "RR";
    s->init        = rr_init;
    s->enqueue     = rr_enqueue;
    s->next        = rr_next;
    s->cont_switch = rr_cont_switch;
    s->quantum_ms  = rr_quantum_ms;
    s->destroy     = rr_destroy;
    s->state       = st;

    s->init(s, cfg);
    return s;
}
