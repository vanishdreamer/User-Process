#include <stddef.h>
#include <stdlib.h>

#include "queue.h"
#include "sem.h"
#include "thread.h"

/* semaphore data structure:
 * @waiting_queue: a queue of threads which are waiting to access a critical section
 * @count: integer which used to keep track of the avaliable resources
 */
struct semaphore {
    queue_t waiting_queue;
    size_t count;
};

sem_t sem_create(size_t count)
{
    sem_t new_sem = (sem_t)malloc(sizeof(struct semaphore));
    queue_t new_queue = queue_create();
    /* error checking: failure of memory allocation */
    if((new_sem == NULL) || (new_queue == NULL)){
        return NULL;
    }
    new_sem->waiting_queue = new_queue;
    new_sem->count = count;
    return new_sem;
}

int sem_destroy(sem_t sem)
{
    /* error checking: if @sem is NULL */
    if(sem == NULL){
        return -1;
    }
    /* error checking: if other threads are still being blocked on @sem */
    if(queue_length(sem->waiting_queue) != 0){
        return -1;
    }
    queue_destroy(sem->waiting_queue);
    free(sem);
    return 0;
}

int sem_down(sem_t sem)
{
    /* error checking: if @sem is NULL */
    if(sem == NULL){
        return -1;
    }
    enter_critical_section();// need to deal with shared resources, so enter critical section
    
    /* if tries to grab a resource when there is no avaliable resource,
     we need to add this thread to the waiting list and block itself */
    while(sem->count == 0){
        pthread_t tid;
        tid = pthread_self();
        queue_enqueue(sem->waiting_queue, &tid);
        thread_block();
    }
    sem->count -= 1;
    exit_critical_section();
    return 0;
}



int sem_up(sem_t sem)
{
    /* error checking: if @sem is NULL */
    if(sem == NULL){
        return -1;
    }
    /* need to deal with shared resources, so enter critical section */
    enter_critical_section();
    
    /* if there are avaliable resources for threads, just release the resource and update count */
    if(sem->count != 0){
        sem->count += 1;
        exit_critical_section();
        return 0;
    }
    
    /* if there isn't any avaliable resource and there are some threads waiting for the resources,
     wake up the first thread in the waiting list and unblock it so that it can run later */
    if(queue_length(sem->waiting_queue) != 0){
        pthread_t* tid;
        /* wake up the first waiting thread */
        queue_dequeue(sem->waiting_queue, (void **)&tid);
        thread_unblock(*tid);
    }
    sem->count += 1;
    exit_critical_section();
    return 0;
}

int sem_getvalue(sem_t sem, int *sval)
{
    /* error checking: if @sem or @sval are NULL */
    if((sem == NULL) || (sval == NULL)){
        return -1;
    }
    enter_critical_section();
    /* if there are avaliable resources, assign the count to data item pointed by sval */
    if(sem->count > 0){
        *sval = sem->count;
    } else {
        /* if there aren't avaliable resources, assign the absolute value
         of blocked threads to data item pointed by sval */
        *sval = (0 - queue_length(sem->waiting_queue));
    }
    exit_critical_section();
    return 0;
}

