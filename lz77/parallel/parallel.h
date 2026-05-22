#ifndef PARALLEL_H
#define PARALLEL_H

#include <pthread.h>
#include <semaphore.h>
#include "../lz77.h"

/*
 *  Estrutura que armazena todos os dados necessários para uma thread 
 *  executar a compressão de uma parte específica do arquivo/buffer.
 */
typedef struct {
    unsigned char *input; // Ponteiro para o buffer de entrada
    int start;            // Índice inicial do trecho que a thread deve processar dentro do buffer.
    int end;              // Índice final do trecho que a thread deve processar dentro do buffer.
    int thread_id;        // Identificador da thread
    struct token *output; // Vetor onde será armazenada a saída comprimida produzida pela thread.
    int output_size;      // Tamanho do vetor de saída produzido pela thread.

} ThreadData;

/*
 *  Realiza a compressão paralela de um buffer utilizando múltiplas threads.
 */
void compress_parallel(
    unsigned char *buffer, // Ponteiro para os dados de entrada que serão comprimidos.
    int file_size,         // Tamanho total do buffer/arquivo em bytes.
    int num_threads        // Quantidade de threads que serão criadas para compressão.
);

#endif