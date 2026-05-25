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
    unsigned char *input;        // Ponteiro para o buffer completo de entrada.
    int start;                   // Índice inicial do trecho que a thread deve processar.
    int end;                     // Índice final exclusivo do trecho que a thread deve processar.
    int thread_id;               // Identificador numérico da thread.
    int la_size;                 // Tamanho do lookahead usado na compressão.
    int sb_size;                 // Tamanho do search-buffer usado na compressão.
    char *temporary_output_path; // Caminho do arquivo temporário gerado pela thread.
    int output_size;             // Quantidade de tokens gerados pela thread.
    int status;                  // Status da thread: 0 para sucesso e -1 para erro.
} ThreadData;

/*
 *  Realiza a compressão paralela de um arquivo usando blocos independentes.
 *  O arquivo final recebe metadados para permitir a descompressão paralela.
 */
int compress_parallel_file(
    const char *input_path,  // Caminho do arquivo original que será comprimido.
    const char *output_path, // Caminho do arquivo comprimido que será gerado.
    int number_of_threads,   // Quantidade de threads solicitadas pelo usuário.
    int la,                  // Tamanho do lookahead; -1 usa o valor padrão.
    int sb                   // Tamanho do search-buffer; -1 usa o valor padrão.
);

/*
 *  Realiza a descompressão paralela de um arquivo gerado pela compressão
 *  paralela. Usa os metadados do cabeçalho para localizar cada bloco.
 */
int decompress_parallel_file(
    const char *input_path,  // Caminho do arquivo comprimido que será descomprimido.
    const char *output_path, // Caminho do arquivo descomprimido que será gerado.
    int number_of_threads    // Quantidade máxima de threads simultâneas.
);

#endif
