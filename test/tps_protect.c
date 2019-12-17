#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tps.h>
#include <sem.h>


/* global variable to make address returned by mmap accessible */
void *latest_mmap_addr;

void *__real_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off); void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
    latest_mmap_addr = __real_mmap(addr, len, prot, flags, fildes, off);
    return latest_mmap_addr;
}

void *protection_thread(void * arg)
{
    /* Create TPS */
    tps_create();
    /* Get TPS page address as allocated via mmap() */
    char *tps_addr = latest_mmap_addr;
    /* Cause an intentional TPS protection error */
    tps_addr[0] = '\0';
    return NULL;
}

/* function to test TPS protection */
void test_TPS_protection()
{
    pthread_t tid;
    tps_init(1);
    pthread_create(&tid, NULL, protection_thread, NULL);
    pthread_join(tid, NULL);
}

int main(int argc, char **argv){
    test_TPS_protection();
}

