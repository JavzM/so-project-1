#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdbool.h>
#include "types.h"

/*
 * Interfaz Strategy del planificador.
 * El resto del sistema (simulation.c, truck.c) solo conoce esta struct
 * nunca sabe cual algoritmo concreto esta corriendo. Cada implementacion
 * (FIFO, RR) tiene su propia solución y expone una factory que rellena los 
 * punteros a funcion.
 */
typedef struct Scheduler Scheduler;

struct Scheduler {
    const char *name;                                              /* "FIFO", "RR" */
    void   (*init)(Scheduler *self, const SimConfig *cfg);
    void   (*enqueue)(Scheduler *self, Truck *t);                  /* el orden depende del algoritmo */
    Truck *(*next)(Scheduler *self);                               /* extrae el siguiente; bloqueante */
    bool   (*cont_switch)(Scheduler *self, const Truck *running);  /* true cuando se cumple el quantum */
    int    (*quantum_ms)(Scheduler *self);                         /* 0 si no aplica */
    void   (*destroy)(Scheduler *self);
    void  *state;                                                  /* dato interno (cola, contadores...) */
};

/*
 * Factory FIFO. Retorna un Scheduler* listo, o NULL en error.
 */
Scheduler *scheduler_fifo_create(const SimConfig *cfg);

/*
 * Factory Round Robin. Retorna un Scheduler* listo, o NULL en error.
 * Pre: cfg->quantum_ms > 0.
 */
Scheduler *scheduler_rr_create(const SimConfig *cfg);

/*
 * Wrapper que delega en self->destroy y libera el propio Scheduler.
 */
void scheduler_destroy(Scheduler *s);

#endif /* SCHEDULER_H */
