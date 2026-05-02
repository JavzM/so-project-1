#ifndef DOCK_H
#define DOCK_H

#include <semaphore.h>
#include <stdbool.h>

/*
 * Wrapper sobre un semaforo POSIX que modela los muelles de carga.
 * Capacidad inicial = M; cada dock_acquire toma un muelle, dock_release lo devuelve.
 *
 * Orden global de recursos: el dock debe adquirirse
 * antes que cualquier mutex (queue, log). Nunca al reves.
 */
typedef struct {
    sem_t sem;
    int   capacity;
    bool  initialized;
} Dock;

/*
 * Inicializa el semaforo con capacidad >= 1.
 * Retorna 0 ok, -1 si sem_init falla.
 */
int dock_init(Dock *d, int capacity);

/* Bloquea hasta haber un muelle libre. */
int dock_acquire(Dock *d);

/* Devuelve un muelle al pool. */
int dock_release(Dock *d);

/* sem_destroy. Pre: ningun hilo bloqueado en dock_acquire. */
int dock_destroy(Dock *d);

#endif /* DOCK_H */
