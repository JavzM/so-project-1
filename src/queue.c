#define _POSIX_C_SOURCE 200809L

#include "queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Patron clasico productor-consumidor con pthread_mutex + pthread_cond.
 * https://man7.org/linux/man-pages/man3/pthread_cond_wait.3p.html
 *
 * Ejemplo de uso:
 *   pthread_mutex_lock(&m);
 *   while (!condicion) pthread_cond_wait(&c, &m);  // libera m al dormir, lo retoma al despertar
 *   // ...consumir...
 *   pthread_mutex_unlock(&m);
 *
 * OJO: ninguna funcion de este modulo llama a logger_event
 * mientras retiene el mutex. Si fuera necesario loguear, primero
 * hace unlock y luego logger_event. Si no se hace así vuelve a haber
 * deadlock según valgrind.
 *
 */

int queue_init(Queue *q) {
    if (q == NULL) {
        fprintf(stderr, "queue_init: q es NULL\n");
        return -1;
    }
    q->head = q->tail = NULL;
    q->size = 0;
    q->closed = false;
    q->initialized = false;

    int rc = pthread_mutex_init(&q->mutex, NULL);
    if (rc != 0) {
        fprintf(stderr, "queue_init: pthread_mutex_init: %s\n", strerror(rc));
        return -1;
    }
    rc = pthread_cond_init(&q->not_empty, NULL);
    if (rc != 0) {
        fprintf(stderr, "queue_init: pthread_cond_init: %s\n", strerror(rc));
        pthread_mutex_destroy(&q->mutex);
        return -1;
    }
    q->initialized = true;
    return 0;
}

/* Inserta al final. Si la cola fue cerrada, rechaza el push. */
int queue_push(Queue *q, Truck *t) {
    if (q == NULL || !q->initialized || t == NULL) {
        return -1;
    }
    QueueNode *node = (QueueNode *)malloc(sizeof(QueueNode));
    if (node == NULL) {
        perror("queue_push: malloc");
        return -1;
    }
    node->t = t;
    node->next = NULL;

    int rc = pthread_mutex_lock(&q->mutex); // Reclama mutex para insertar
    if (rc != 0) {
        fprintf(stderr, "queue_push: lock: %s\n", strerror(rc));
        free(node);
        return -1;
    }
    if (q->closed) { // Si la cola esta cerrada
        pthread_mutex_unlock(&q->mutex); // Libera
        free(node); // Libera memoria del node
        return -1; // error
    }
    if (q->tail == NULL) { // Si la cola esta vacía
        q->head = q->tail = node;
    } else {
        q->tail->next = node;
        q->tail = node;
    }
    q->size++;
    /* Signal en vez de broadcast: solo un consumidor puede tomar este nuevo elemento. */
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

/*
 * Inserta t antes del primer camion no-perecedero de la cola.
 * Si todos son perecederos (o la cola esta vacia), inserta al final.
 *
 * Casos posibles tras la busqueda lineal:
 *   curr == NULL, prev == NULL  → cola vacia: insertar como unico nodo
 *   curr == NULL, prev != NULL  → todos perecederos: insertar al final
 *   curr != NULL, prev == NULL  → primer nodo es no-perecedero: insertar al frente
 *   curr != NULL, prev != NULL  → insertar entre prev y curr
 */
int queue_push_priority(Queue *q, Truck *t) {
    if (q == NULL || !q->initialized || t == NULL) {
        return -1;
    }
    QueueNode *node = (QueueNode *)malloc(sizeof(QueueNode));
    if (node == NULL) {
        perror("queue_push_priority: malloc");
        return -1;
    }
    node->t    = t;
    node->next = NULL;

    int rc = pthread_mutex_lock(&q->mutex);
    if (rc != 0) {
        fprintf(stderr, "queue_push_priority: lock: %s\n", strerror(rc));
        free(node);
        return -1;
    }
    if (q->closed) {
        pthread_mutex_unlock(&q->mutex);
        free(node);
        return -1;
    }

    /* Buscar el primer nodo no-perecedero (linear search). */
    QueueNode *prev = NULL;
    QueueNode *curr = q->head;
    while (curr != NULL && curr->t->cargo == CARGO_PERISHABLE) {
        prev = curr;
        curr = curr->next;
    }

    if (q->head == NULL) {
        /* Cola vacía. */
        q->head = q->tail = node;
    } else if (curr == NULL) {
        /* Todos son perecederos: insertar al final. */
        q->tail->next = node;
        q->tail       = node;
    } else if (prev == NULL) {
        /* El primero es no-perecedero: insertar al frente. */
        node->next = q->head;
        q->head    = node;
    } else {
        /* Insertar entre prev y curr. */
        node->next  = curr;
        prev->next  = node;
    }

    q->size++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

Truck *queue_pop_fifo(Queue *q) {
    if (q == NULL || !q->initialized) {
        return NULL;
    }
    int rc = pthread_mutex_lock(&q->mutex); // Reclama mutex
    if (rc != 0) {
        fprintf(stderr, "queue_pop_fifo: lock: %s\n", strerror(rc));
        return NULL;
    }
    /* Patrón standard */
    while (q->size == 0 && !q->closed) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }

    if (q->size == 0 && q->closed) { // Si no hay nada
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }

    QueueNode *node = q->head;
    q->head = node->next;

    if (q->head == NULL) {  // Si es el ultimo
        q->tail = NULL;
    }

    q->size--;
    Truck *t = node->t;
    pthread_mutex_unlock(&q->mutex);
    free(node);
    return t;
}

int queue_size(Queue *q) {
    if (q == NULL || !q->initialized) {
        return 0;
    }
    pthread_mutex_lock(&q->mutex);
    int s = q->size;
    pthread_mutex_unlock(&q->mutex);
    return s;
}

void queue_close(Queue *q) {
    if (q == NULL || !q->initialized) {
        return;
    }
    pthread_mutex_lock(&q->mutex);
    q->closed = true;

    /* broadcast: todos los consumidores dormidos deben despertarse para
     * ver el flag de cierre y salir. signal solo despertaria a uno.
     * https://pubs.opengroup.org/onlinepubs/007904975/functions/pthread_cond_broadcast.html
     */
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}

int queue_destroy(Queue *q) {
    if (q == NULL || !q->initialized) {
        return 0;
    }
    /* Libera nodos sobrantes, si es que hay */
    QueueNode *node = q->head;
    while (node != NULL) {
        QueueNode *next = node->next;
        free(node);
        node = next;
    }
    q->head = q->tail = NULL;
    q->size = 0;

    int rc1 = pthread_cond_destroy(&q->not_empty);
    int rc2 = pthread_mutex_destroy(&q->mutex);
    q->initialized = false;
    if (rc1 != 0) {
        fprintf(stderr, "queue_destroy: cond_destroy: %s\n", strerror(rc1));
        return -1;
    }
    if (rc2 != 0) {
        fprintf(stderr, "queue_destroy: mutex_destroy: %s\n", strerror(rc2));
        return -1;
    }
    return 0;
}
