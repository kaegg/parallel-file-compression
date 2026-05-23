#ifndef PARALLEL_H
#define PARALLEL_H

#include <pthread.h>
#include <semaphore.h>
#include "lz77.h"

/*
 * Estrutura que armazena todos os dados necessários para uma thread
 * executar a compressão de uma parte específica do arquivo/buffer.
 */
typedef struct {
    unsigned char *input;
    int start;
    int end;
    int thread_id;
    int la_size;
    int sb_size;
    struct token *output;
    int output_size;      /* quantidade de tokens gerados */
} ThreadData;

/*
 * Realiza a compressao paralela de um arquivo usando blocos independentes.
 * Retorna 0 em sucesso e -1 em erro.
 */
int compress_parallel_file(
    const char *input_path,
    const char *output_path,
    int number_of_threads,
    int la,
    int sb
);

#endif
