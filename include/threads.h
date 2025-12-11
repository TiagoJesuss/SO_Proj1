#ifndef THREADS_H
#define THREADS_H

#include "board.h"
#include <pthread.h>

typedef struct {
    char path[1024];
    level_info *level_info;
} thread_args_t;

typedef struct {
    board_t *game_board;
    int ghost_index;
    bool *leave_thread;
    pthread_mutex_t *mutex;
    //pthread_rwlock_t *lock;
} ghost_thread_args_t;

typedef struct {
    board_t *game_board;
    int *result;
    bool *leave_thread;
    pthread_mutex_t *mutex;
} pacman_thread_args_t;

void *ghost_thread(void *arg);

void *read_file_thread(void *arg);

#endif