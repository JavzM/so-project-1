#ifndef SIMULATION_H
#define SIMULATION_H

#include "types.h"
#include "scheduler.h"
#include "dock.h"

/*
 * Recursos compartidos por todos los camiones.
 *
 * Vive en el stack de simulation_run. El campo Truck::sim_ctx (de tipo
 * void*) se rellena con un puntero a esta struct antes de pthread_create.
 *
 * Los cambios de contexto y las metricas de tiempo se registran en el
 * modulo metrics (metrics.h), que es el dueno canonico de esos contadores.
 */
typedef struct SimContext {
    Dock             dock;
    Scheduler       *sched;
    const SimConfig *cfg;
} SimContext;

/*
 * Orquesta una corrida completa: inicializa dock, genera N camiones con
 * llegadas escalonadas, lanza hilos truck_run, hace pthread_join a todos
 * y limpia recursos.
 *
 * Retorna 0 si todos los camiones llegaron a TERMINATED, -1 ante error.
 * No destruye el scheduler: lo libera el caller con scheduler_destroy.
 */
int simulation_run(const SimConfig *cfg, Scheduler *sched);

#endif /* SIMULATION_H */
