#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>

#include "parallel.h"
#include "bitio.h"

#define DEFAULT_LA_SIZE 15
#define DEFAULT_SB_SIZE 4095
#define MAX_BIT_BUFFER 16

/* Mutex utilizado apenas para logs/estatisticas compartilhadas. */
static pthread_mutex_t write_mutex;

/* Semaforo usado para controlar a entrada das threads na compressao. */
static sem_t thread_semaphore;

/* Estatistica global protegida por mutex. */
static int total_tokens_generated = 0;

static long get_file_size(FILE *file)
{
    long size;

    if (fseek(file, 0, SEEK_END) != 0) {
        return -1;
    }

    size = ftell(file);

    if (fseek(file, 0, SEEK_SET) != 0) {
        return -1;
    }

    return size;
}

/*
 * Funcao executada por cada thread.
 * Cada thread comprime um bloco independente em um vetor local de tokens.
 */
static void *compress_thread(void *thread_arguments)
{
    ThreadData *thread_data = (ThreadData *) thread_arguments;

    if (sem_trywait(&thread_semaphore) != 0) {
        printf("[THREAD %d] aguardando recurso...\n", thread_data->thread_id);
        sem_wait(&thread_semaphore);
    }

    printf(
        "[THREAD %d] processando bloco [%d - %d)\n",
        thread_data->thread_id,
        thread_data->start,
        thread_data->end
    );

    compress_block(
        thread_data->input,
        thread_data->start,
        thread_data->end,
        thread_data->la_size,
        thread_data->sb_size,
        &thread_data->output,
        &thread_data->output_size
    );

    pthread_mutex_lock(&write_mutex);
    total_tokens_generated += thread_data->output_size;
    printf(
        "[THREAD %d] compressao finalizada: %d tokens\n",
        thread_data->thread_id,
        thread_data->output_size
    );
    pthread_mutex_unlock(&write_mutex);

    sem_post(&thread_semaphore);
    pthread_exit(NULL);
}

/*
 * Compressao paralela de arquivo.
 *
 * Estrategia:
 * 1. Le o arquivo inteiro para um buffer.
 * 2. Divide o buffer em blocos independentes.
 * 3. Cada thread comprime um bloco em memoria.
 * 4. A thread principal grava os tokens no arquivo final na ordem original.
 *
 * Como os blocos sao independentes, a taxa de compressao pode ser um pouco
 * pior que a versao sequencial, mas a descompressao continua compativel com
 * a funcao decode original.
 */
int compress_parallel_file(
    const char *input_path,
    const char *output_path,
    int number_of_threads,
    int la,
    int sb
)
{
    FILE *input_file = NULL;
    struct bitFILE *output_file = NULL;
    unsigned char *input_buffer = NULL;
    pthread_t *threads = NULL;
    ThreadData *thread_data = NULL;
    long long_size;
    int input_file_size;
    int block_size;
    int thread_index;
    int token_index;
    int LA_SIZE = (la == -1) ? DEFAULT_LA_SIZE : la;
    int SB_SIZE = (sb == -1) ? DEFAULT_SB_SIZE : sb;
    int status = -1;

    if (input_path == NULL || output_path == NULL || number_of_threads < 1) {
        return -1;
    }

    if (LA_SIZE < 1) {
        LA_SIZE = DEFAULT_LA_SIZE;
    }

    if (SB_SIZE < 1) {
        SB_SIZE = DEFAULT_SB_SIZE;
    }

    input_file = fopen(input_path, "rb");
    if (input_file == NULL) {
        perror("Opening input file");
        goto cleanup;
    }

    long_size = get_file_size(input_file);
    if (long_size < 0 || long_size > INT_MAX) {
        fprintf(stderr, "Invalid or too large input file.\n");
        goto cleanup;
    }

    input_file_size = (int) long_size;

    if (input_file_size > 0) {
        input_buffer = (unsigned char *) malloc((size_t) input_file_size);
        if (input_buffer == NULL) {
            fprintf(stderr, "Memory allocation error.\n");
            goto cleanup;
        }

        if (fread(input_buffer, 1, (size_t) input_file_size, input_file) != (size_t) input_file_size) {
            fprintf(stderr, "Error reading input file.\n");
            goto cleanup;
        }
    }

    if (number_of_threads > input_file_size && input_file_size > 0) {
        number_of_threads = input_file_size;
    }

    output_file = bitIO_open(output_path, BIT_IO_W);
    if (output_file == NULL) {
        perror("Opening output file");
        goto cleanup;
    }

    /* Arquivo vazio: grava apenas o cabecalho padrao. */
    if (input_file_size == 0) {
        bitIO_write(output_file, &SB_SIZE, MAX_BIT_BUFFER);
        bitIO_write(output_file, &LA_SIZE, MAX_BIT_BUFFER);
        status = 0;
        goto cleanup;
    }

    threads = (pthread_t *) malloc((size_t) number_of_threads * sizeof(pthread_t));
    thread_data = (ThreadData *) calloc((size_t) number_of_threads, sizeof(ThreadData));

    if (threads == NULL || thread_data == NULL) {
        fprintf(stderr, "Memory allocation error.\n");
        goto cleanup;
    }

    block_size = input_file_size / number_of_threads;

    pthread_mutex_init(&write_mutex, NULL);
    sem_init(&thread_semaphore, 0, number_of_threads);
    total_tokens_generated = 0;

    printf("\n====================================\n");
    printf("COMPRESSAO PARALELA INICIADA\n");
    printf("Arquivo: %s\n", input_path);
    printf("Tamanho original: %d bytes\n", input_file_size);
    printf("Threads: %d\n", number_of_threads);
    printf("Lookahead: %d | Search buffer: %d\n", LA_SIZE, SB_SIZE);
    printf("====================================\n");

    for (thread_index = 0; thread_index < number_of_threads; thread_index++) {
        thread_data[thread_index].input = input_buffer;
        thread_data[thread_index].start = thread_index * block_size;
        thread_data[thread_index].end = (thread_index == number_of_threads - 1)
            ? input_file_size
            : (thread_index + 1) * block_size;
        thread_data[thread_index].thread_id = thread_index;
        thread_data[thread_index].la_size = LA_SIZE;
        thread_data[thread_index].sb_size = SB_SIZE;
        thread_data[thread_index].output = NULL;
        thread_data[thread_index].output_size = 0;

        if (pthread_create(&threads[thread_index], NULL, compress_thread, &thread_data[thread_index]) != 0) {
            fprintf(stderr, "Error creating thread %d.\n", thread_index);
            number_of_threads = thread_index;
            goto join_threads;
        }
    }

join_threads:
    for (thread_index = 0; thread_index < number_of_threads; thread_index++) {
        pthread_join(threads[thread_index], NULL);
    }

    printf("\n====================================\n");
    printf("TODAS AS THREADS FINALIZARAM\n");
    printf("Total de tokens: %d\n", total_tokens_generated);
    printf("====================================\n");

    /* Cabecalho padrao do formato original. */
    bitIO_write(output_file, &SB_SIZE, MAX_BIT_BUFFER);
    bitIO_write(output_file, &LA_SIZE, MAX_BIT_BUFFER);

    /* Escrita final em ordem para manter o arquivo descomprimivel. */
    for (thread_index = 0; thread_index < number_of_threads; thread_index++) {
        for (token_index = 0; token_index < thread_data[thread_index].output_size; token_index++) {
            writecode(
                thread_data[thread_index].output[token_index],
                output_file,
                LA_SIZE,
                SB_SIZE
            );
        }
    }

    status = 0;

    sem_destroy(&thread_semaphore);
    pthread_mutex_destroy(&write_mutex);

cleanup:
    if (input_file != NULL) {
        fclose(input_file);
    }

    if (output_file != NULL) {
        bitIO_close(output_file);
    }

    if (thread_data != NULL) {
        for (thread_index = 0; thread_index < number_of_threads; thread_index++) {
            free(thread_data[thread_index].output);
        }
    }

    free(thread_data);
    free(threads);
    free(input_buffer);

    return status;
}
