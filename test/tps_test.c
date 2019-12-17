#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tps.h>
#include <sem.h>
#include <thread.h>


static sem_t sem1, sem2;
static char msg1[TPS_SIZE] = "Hello world!\n";
static char msg2[TPS_SIZE] = "hello world!\n";
static char msg3[TPS_SIZE] = "Hello ECS150!\n";
static char msg4[TPS_SIZE] = "hello ECS150!\n";


void *thread2(void* arg)
{
    char *buffer = malloc(TPS_SIZE);
    
    /* Create TPS and initialize with *msg1 */
    tps_create();
    tps_write(0, TPS_SIZE, msg1);
    
    /* Read from TPS and make sure it contains the message */
    memset(buffer, 0, TPS_SIZE);
    tps_read(0, TPS_SIZE, buffer);
    assert(!memcmp(msg1, buffer, TPS_SIZE));
    printf("thread2: read OK!\n");
    
    /* Transfer CPU to thread 1 and get blocked */
    sem_up(sem1);
    sem_down(sem2);
    
    /* When we're back, read TPS and make sure it sill contains the original */
    memset(buffer, 0, TPS_SIZE);
    tps_read(0, TPS_SIZE, buffer);
    assert(!memcmp(msg1, buffer, TPS_SIZE));
    printf("thread2: read OK!\n");
    
    /* Transfer CPU to thread 1 and get blocked */
    sem_up(sem1);
    sem_down(sem2);
    
    /* Destroy TPS and quit */
    tps_destroy();
    return NULL;
}

void *thread1(void* arg)
{
    pthread_t tid;
    char *buffer = malloc(TPS_SIZE);
    
    /* Create thread 2 and get blocked */
    pthread_create(&tid, NULL, thread2, NULL);
    sem_down(sem1);
    
    /* When we're back, clone thread 2's TPS */
    tps_clone(tid);
    
    /* Read the TPS and make sure it contains the original */
    memset(buffer, 0, TPS_SIZE);
    tps_read(0, TPS_SIZE, buffer);
    assert(!memcmp(msg1, buffer, TPS_SIZE));
    printf("thread1: read OK!\n");
    
    /* Modify TPS to cause a copy on write */
    buffer[0] = 'h';
    tps_write(0, 1, buffer);
    
    /* Transfer CPU to thread 2 and get blocked */
    sem_up(sem2);
    sem_down(sem1);
    
    /* When we're back, make sure our modification is still there */
    memset(buffer, 0, TPS_SIZE);
    tps_read(0, TPS_SIZE, buffer);
    assert(!strcmp(msg2, buffer));
    printf("thread1: read OK!\n");
    
    /* Transfer CPU to thread 2 */
    sem_up(sem2);
    
    /* Wait for thread2 to die, and quit */
    pthread_join(tid, NULL);
    tps_destroy();
    return NULL;
}

void *CoW_thread(void* arg)
{
    
    char *buffer = malloc(TPS_SIZE);
    
    tps_create();
    tps_write(0, TPS_SIZE, msg3);
    sem_up(sem1);
    sem_down(sem2);
    
    tps_read(0, TPS_SIZE, buffer);
    assert(strcmp(buffer, msg3) == 0);
    
    sem_up(sem1);
    tps_destroy();
    return NULL;
}


void test_error_write(){
    int success;
    char *buffer = malloc(TPS_SIZE);
    
    tps_create();
    /* if the writing operation is out of bound */
    success = tps_write(0,TPS_SIZE+1, buffer);
    assert(success == -1);
    /* if buffer is NULL */
    success = tps_write(0,TPS_SIZE, NULL);
    assert(success == -1);
    
    tps_destroy();
}

void test_error_read(){
    int success;
    char *buffer = malloc(TPS_SIZE);
    
    tps_create();
    /* if the reading operation is out of bound */
    success = tps_read(0,TPS_SIZE+1, buffer);
    assert(success == -1);
    /* if buffer is NULL */
    success = tps_read(0,TPS_SIZE, NULL);
    assert(success == -1);
    
    tps_destroy();
    
}

void test_error_create(){
    int success;
    
    /* if current thread already has a TPS */
    tps_create();
    success = tps_create();
    assert(success == -1);
    tps_destroy();
}

void test_error_destroy(){
    int success;
    
    tps_create();
    /* if there is no TPS */
    tps_destroy();
    success = tps_destroy();
    assert(success == -1);
}

void test_error_init(){
    int success;
    
    /* if tps is initialized more than once */
    tps_init(1);
    success = tps_init(1);
    assert(success == -1);
}

void *simple_thread(void* arg){
    tps_create();
    printf("This is simple thread\n");
    tps_destroy();
    return NULL;
}

void test_error_clone(){
    int success;
    pthread_t tid;
    
    tps_create();
    pthread_create(&tid, NULL, simple_thread, NULL);
    
    /* cannot directly clone because of TPS proctect */
    success = tps_clone(tid);
    assert(success == -1);
    tps_destroy();
}


void test_create(){
    int success;
    
    success = tps_create();
    assert(success == 0);
    tps_destroy();
}

void test_destroy(){
    int success;
    
    tps_create();
    success = tps_destroy();
    assert(success == 0);
}

void test_init(){
    int success;
    
    success = tps_init(1);
    assert(success == 0);
}

void test_read_write()
{   pthread_t tid;
    
    sem1 = sem_create(0);
    sem2 = sem_create(0);
    tps_init(1);
    
    pthread_create(&tid, NULL, thread1, NULL);
    pthread_join(tid, NULL);
    
    sem_destroy(sem1);
    sem_destroy(sem2);
}

void test_copy_on_write(){
    int success;
    pthread_t tid;
    char *buffer = malloc(TPS_SIZE);
    sem1 = sem_create(0);
    sem2 = sem_create(0);
    
    pthread_create(&tid, NULL, CoW_thread, NULL);
    sem_down(sem1);
    
    success = tps_clone(tid);
    assert(success == 0);
    tps_read(0, TPS_SIZE, buffer);
    assert(strcmp(buffer, msg3) == 0);
    
    /*overwrite the copy, check whether is successful */
    tps_write(0, TPS_SIZE, msg4);
    memset(buffer, 0, TPS_SIZE);
    tps_read(0, TPS_SIZE, buffer);
    assert(strcmp(buffer, msg4) == 0);
    
    sem_up(sem2);
    sem_down(sem1);
    sem_destroy(sem1);
    sem_destroy(sem2);
    
    
}

int main(int argc, char **argv){
    test_init();
    test_create();
    test_destroy();
    test_read_write();
    test_copy_on_write();
    
    test_error_init();
    test_error_create();
    test_error_destroy();
    test_error_write();
    test_error_read();
    test_error_clone();
    
}

