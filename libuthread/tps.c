#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "queue.h"
#include "thread.h"
#include "tps.h"



/* page data structure:
 * @shared: keep track of how many TPSes are currently “sharing” the same memory page
 * @page: memory page's address
 */
struct page_block{
    int shared;
    void* page;
};

typedef struct page_block* page_t;

/* TPS data structure:
 * @tid: the client thread's tid
 * @page_set: the memory page's address which this TPS point to
 */
struct TPS {
    pthread_t tid;
    page_t page_set;
};

typedef struct TPS* TPS_t;

/* Enum that represents the type of modification that needs to be made
 * with protection
 */
enum modify_type{
    Read,
    Write,
    Copy
};

/* shared TPS list which containes all thread's TPS */
queue_t tps_queue = NULL;

/* flag to identify whether is first time initialization */
int initialized = 0;

/* Queue function used to find the specific thread given the tid */
static int find_tid(void *data, void *arg)
{
    pthread_t *target_TID = (pthread_t*)arg;
    if (((TPS_t)data)->tid == *target_TID)
        return 1;
    
    return 0;
}

/* Queue function used to find the specific memory page's address */
static int find_address(void *data, void *arg)
{
    if (((TPS_t)data)->page_set->page == arg)
        return 1;
    
    return 0;
}

/* function used to find the specific TPS given tid using queue_iterate
 * if the target thread is found, return its address, else NULL
 * @tid: tid of the target thread
 */
TPS_t get_TPS(pthread_t tid)
{
    TPS_t found_TPS = NULL;
    queue_iterate(tps_queue, find_tid, &tid, (void **)&found_TPS);
    return found_TPS;
}

static void segv_handler(int sig, siginfo_t *si, void *context)
{    /*
      * Get the address corresponding to the beginning of the page where the
      * fault occurred
      */
    void *p_fault = (void*)((uintptr_t)si->si_addr & ~(TPS_SIZE - 1));
    TPS_t found_TPS = NULL;
    /*
     * Iterate through all the TPS areas and find if p_fault matches one of them
     */
    queue_iterate(tps_queue, find_address, p_fault, (void **)&found_TPS);

    /* There is a match */
    if (found_TPS != NULL){
        fprintf(stderr, "TPS protection error!\n");
    }
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    raise(sig);
}

int tps_init(int segv)
{
    /* checks if it is the first time initialization. */
    if(initialized){
        return -1;
    }
    initialized = 1;
    if (segv) {
        struct sigaction sa;
        
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO;
        sa.sa_sigaction = segv_handler;
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGSEGV, &sa, NULL);
    }
    return 0;
}

/* function used to create new tps
 * if success, returns the address of the tps, else NULL
 */
TPS_t new_tps(){
    TPS_t new_TPS = (TPS_t)malloc(sizeof(struct TPS));
    if(new_TPS == NULL){
        return NULL;
    }
    new_TPS->tid = pthread_self();
    new_TPS->page_set = NULL;
    return new_TPS;
}

/* function used to create new page block
 * if success, returns the address of the page block, else NULL
 */
page_t new_page(){
    page_t new_page_set = (page_t)malloc(sizeof(struct page_block));
    void* new_page = mmap(NULL, TPS_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if((new_page_set == NULL) || (new_page == MAP_FAILED)){
        return NULL;
    }
    new_page_set->page = new_page;
    new_page_set->shared = 0;
    return new_page_set;
}

int tps_create(void)
{
    /* create tps queue if there is no current queue exists */
    if(tps_queue == NULL){
        enter_critical_section();
        tps_queue = queue_create();
        exit_critical_section();
    }
    
    enter_critical_section();
    TPS_t found_TPS = get_TPS(pthread_self());
    exit_critical_section();
    
    /* error checking: if current thread already has a TPS */
    if(found_TPS != NULL){
        return -1;
    }
    
    TPS_t new_TPS = new_tps();
    page_t new_page_set = new_page();
    
    /* error checking: failure during memory allocation */
    if((new_TPS == NULL) || (new_page_set == NULL)){
        return -1;
    }
    
    new_TPS->page_set = new_page_set;
    enter_critical_section();
    queue_enqueue(tps_queue, new_TPS);
    exit_critical_section();
    
    return 0;
}

int tps_destroy(void)
{
    enter_critical_section();
    TPS_t found_TPS = get_TPS(pthread_self());
    exit_critical_section();
    
    /* error checking: if current thread doesn't have a TPS */
    if(found_TPS == NULL){
        return -1;
    }
    
    /* free all the elements in the tps and deletes it from the tps queue */
    enter_critical_section();
    queue_delete(tps_queue, found_TPS);
    exit_critical_section();
    munmap(found_TPS->page_set->page, TPS_SIZE);
    free(found_TPS->page_set);
    free(found_TPS);
    
    /* free TPS queue when the queue is empty */
    if(!queue_length(tps_queue)){
        enter_critical_section();
        queue_destroy(tps_queue);
        tps_queue = NULL;
        exit_critical_section();
    }
    
    return 0;
}

/* function used to modify the content of a page when it is protected
 * @type: type of modification that needs to be made
 */
void modify_page(size_t offset, size_t length, char *buffer, void* page, void* clone_page, enum modify_type type)
{
    switch(type){
        case Read:
            /* changes the access protection that can be read of the calling thread's
             memory pages in the interval */
            mprotect(page, TPS_SIZE, PROT_READ);
            /* copy the data from offset to buffer */
            memcpy(buffer, page + offset, length);
            /* close all memory access of the calling thread's memory pages in the interval */
            mprotect(page, TPS_SIZE, PROT_NONE);
            break;
        case Write:
            /* change the access protection of this new memory page so that it can be
             written with buffer, the original memory page can only be readable but
             not writable */
            mprotect(page, TPS_SIZE, PROT_WRITE);
            memcpy(page + offset, buffer, length);
            mprotect(page, TPS_SIZE, PROT_NONE);
            break;
        case Copy:
            mprotect(clone_page, TPS_SIZE, PROT_READ);
            mprotect(page, TPS_SIZE, PROT_WRITE);
            memcpy(page, clone_page, TPS_SIZE);
            memcpy(page + offset, buffer, length);
            mprotect(clone_page, TPS_SIZE, PROT_NONE);
            mprotect(page, TPS_SIZE, PROT_NONE);
            break;
    }
}

int tps_read(size_t offset, size_t length, char *buffer)
{
    enter_critical_section();
    TPS_t found_TPS = get_TPS(pthread_self());
    exit_critical_section();
    
    /* error checking: if current thread doesn't have a TPS
     * or if the reading operation is out of bound, or if @buffer is NULL
     */
    if((found_TPS == NULL) || ((offset + length) > TPS_SIZE) || (buffer == NULL)){
        return -1;
    }
    
    modify_page(offset, length, buffer, found_TPS->page_set->page, NULL, Read);
    
    return 0;
}

int tps_write(size_t offset, size_t length, char *buffer)
{
    enter_critical_section();
    TPS_t found_TPS = get_TPS(pthread_self());
    exit_critical_section();
    /* error checking: if current thread doesn't have a TPS
     * or the writing operation is out of bound, or if @buffer is NULL
     */
    if((found_TPS == NULL) || ((offset + length) > TPS_SIZE) || (buffer == NULL)){
        return -1;
    }
    
    /* if the page is not shared, just make modification on it
     * else we need to make a new page and copy the content of the old page
     * and make modification on the new page
     */
    if(!found_TPS->page_set->shared){
        modify_page(offset, length, buffer, found_TPS->page_set->page, NULL, Write);
    } else {
        page_t new_page_set = new_page();
        modify_page(offset, length, buffer, new_page_set->page, found_TPS->page_set->page, Copy);
        found_TPS->page_set->shared -= 1;
        found_TPS->page_set = new_page_set;
    }
    
    return 0;
}

int tps_clone(pthread_t tid)
{
    enter_critical_section();
    TPS_t current_TPS = get_TPS(pthread_self());
    TPS_t clone_TPS = get_TPS(tid);
    exit_critical_section();
    
    /* error checking: if thread @tid doesn't have a TPS, or if current thread already has a TPS */
    if((current_TPS != NULL) || (clone_TPS == NULL)){
        return -1;
    }
    
    /* create a new TPS which refers to the same memory page as the cloned TPS */
    TPS_t new_TPS = new_tps();
    if(new_TPS == NULL){
        return -1;
    }
    
    /* shares the page and update the number of page's reference counter */
    clone_TPS->page_set->shared += 1;
    new_TPS->page_set = clone_TPS->page_set;
    
    enter_critical_section();
    queue_enqueue(tps_queue, new_TPS);
    exit_critical_section();
    
    return 0;
}

