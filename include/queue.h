#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include "types.h"

/*
 * Cola FIFO de Truck* (Camiones). Internamente: lista enlazada simple +
 * un mutex que protege la estructura + una condicion para que los
 * consumidores se duerman cuando la cola esta vacia.
 *
 * Orden global de recursos:
 *   - Adquirirse DESPUES del semaforo de muelles.
 *   - Adquirirse ANTES del mutex del log.
 *   - Nunca llamar logger_event reteniendo este mutex.
 *
 */
typedef struct QueueNode {
    Truck            *t;
    struct QueueNode *next;
} QueueNode;

typedef struct {
    QueueNode      *head;
    QueueNode      *tail;
    int             size;
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    bool            closed;       /* si true, queue_pop_fifo retorna NULL al vaciarse */
    bool            initialized;
} Queue;

/* Inicializa cola vacia. Retorna 0 ok, -1 si falla mutex/cond init. */
int queue_init(Queue *q);

/* Append en tail. Despierta UN consumidor (pthread_cond_signal). */
int queue_push(Queue *q, Truck *t);

/*
 * Inserta t antes del primer camion no-perecedero de la cola.
 * Si todos son perecederos (o la cola esta vacia), inserta al final.
 * Thread-safe: toma el mismo mutex que queue_push.
 * Pre: t->cargo == CARGO_PERISHABLE (quien llama ya lo verifico).
 */
int queue_push_priority(Queue *q, Truck *t);

/* Extrae el frente. Bloquea con pthread_cond_wait si esta vacia.
 * Retorna NULL si la cola esta vacia y fue cerrada con queue_close. */
Truck *queue_pop_fifo(Queue *q);

/* Tamano actual. Toma el mutex mientras se ejecuta. */
int queue_size(Queue *q);

/* Marca cerrada y broadcast a todos los consumidores bloqueados para
 * que se enteren y salgan. */
void queue_close(Queue *q);

/* Libera nodos remanentes y destruye mutex/cond. */
int queue_destroy(Queue *q);

#endif /* QUEUE_H */
