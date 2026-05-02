#define _POSIX_C_SOURCE 200809L

#include "dock.h"

#include <errno.h>
#include <stdio.h>

/*
 * Semaforos POSIX no-nombrados: https://man7.org/linux/man-pages/man3/sem_init.3.html
 * Ejemplo:
 *   sem_t s;
 *   sem_init(&s, 0, 3);   // 0 = compartido entre hilos del mismo proceso
 *   sem_wait(&s);          // decrementa, bloquea si esta en 0
 *   sem_post(&s);          // incrementa
 *   sem_destroy(&s);
 */
int dock_init(Dock *d, int capacity) {
    if (d == NULL || capacity <= 0) {
        fprintf(stderr, "dock_init: parametros invalidos\n");
        return -1;
    }
    /* segundo parametro = 0 -> semaforo compartido solo entre hilos del mismo proceso */
    if (sem_init(&d->sem, 0, (unsigned int)capacity) != 0) {
        perror("dock_init: sem_init");
        return -1;
    }
    d->capacity = capacity;
    d->initialized = true;
    return 0;
}

int dock_acquire(Dock *d) {
    if (d == NULL || !d->initialized) {
        fprintf(stderr, "dock_acquire: dock no inicializado\n");
        return -1;
    }
    /* sem_wait puede retornar -1 con errno=EINTR si una signal interrumpe la espera.
     * En ese caso reintentamos; cualquier otro errno retorna -1. */
    while (sem_wait(&d->sem) != 0) {
        if (errno == EINTR) continue;
        perror("dock_acquire: sem_wait");
        return -1;
    }
    return 0;
}

int dock_release(Dock *d) {
    if (d == NULL || !d->initialized) {
        fprintf(stderr, "dock_release: dock no inicializado\n");
        return -1;
    }
    if (sem_post(&d->sem) != 0) {
        perror("dock_release: sem_post");
        return -1;
    }
    return 0;
}

int dock_destroy(Dock *d) {
    if (d == NULL || !d->initialized) {
        return 0;
    }
    /* Si algun hilo aun esta bloqueado en sem_wait, puede haber errores.
     * El llamador debe garantizar joins previos antes de destruir. */
    if (sem_destroy(&d->sem) != 0) {
        perror("dock_destroy: sem_destroy");
        return -1;
    }
    d->initialized = false;
    return 0;
}
