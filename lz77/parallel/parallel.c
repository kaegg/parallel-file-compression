#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include "parallel.h"
#include "../lz77.h"

/*
 * Mutex utilizado para proteger regiões críticas.
 */
pthread_mutex_t write_mutex;

/*
 * Semáforo utilizado para controlar a quantidade
 * de threads executando simultaneamente.
 */
sem_t thread_semaphore;

/*
 * Função executada por cada thread.
 *
 * Cada thread:
 * - recebe um bloco do arquivo;
 * - realiza a compressão daquele bloco;
 * - salva o resultado localmente.
 */
void *compress_thread(void *thread_arguments)
{
    /*
     * Converte o argumento genérico recebido
     * para a estrutura ThreadData.
     */
    ThreadData *thread_data = (ThreadData *) thread_arguments;

    /*
     * Tenta acessar o semáforo sem bloquear.
     */
    if (sem_trywait(&thread_semaphore) != 0) {

        printf(
            "[THREAD %d] aguardando recurso...\n",
            thread_data->thread_id
        );

        /*
         * Aguarda até que exista vaga disponível.
         */
        sem_wait(&thread_semaphore);
    }

    printf(
        "[THREAD %d] processando bloco [%d - %d]\n",
        thread_data->thread_id,
        thread_data->start,
        thread_data->end
    );

    /*
     * Realiza a compressão do bloco.
     */
    compress_block(
        thread_data->input,
        thread_data->start,
        thread_data->end,
        &thread_data->output,
        &thread_data->output_size
    );

    /*
     * Região crítica protegida por mutex.
     *
     * Aqui vocês podem:
     * - atualizar estatísticas globais;
     * - registrar logs;
     * - escrever em arquivo.
     */
    pthread_mutex_lock(&write_mutex);

    printf(
        "[THREAD %d] compressão finalizada\n",
        thread_data->thread_id
    );

    pthread_mutex_unlock(&write_mutex);

    /*
     * Libera vaga no semáforo.
     */
    sem_post(&thread_semaphore);

    /*
     * Finaliza a thread.
     */
    pthread_exit(NULL);
}

/*
 * Função principal da compressão paralela.
 *
 * Responsável por:
 * - dividir o arquivo em blocos;
 * - criar as threads;
 * - sincronizar execução;
 * - juntar os resultados finais.
 */
void compress_parallel(
    unsigned char *input_file_buffer,
    int input_file_size,
    int number_of_threads
)
{
    /*
     * Vetor contendo identificadores das threads.
     */
    pthread_t threads[number_of_threads];

    /*
     * Vetor contendo os dados individuais
     * de cada thread.
     */
    ThreadData thread_data[number_of_threads];

    /*
     * Calcula tamanho de cada bloco.
     */
    int block_size =
        input_file_size / number_of_threads;

    /*
     * Inicializa mutex.
     */
    pthread_mutex_init(&write_mutex, NULL);

    /*
     * Inicializa semáforo.
     *
     * Limita quantidade máxima de threads
     * executando simultaneamente.
     */
    sem_init(
        &thread_semaphore,
        0,
        number_of_threads
    );

    printf("\n");
    printf("====================================\n");
    printf("COMPRESSAO PARALELA INICIADA\n");
    printf("====================================\n");

    /*
     * Criação das threads.
     */
    for (int thread_index = 0;
         thread_index < number_of_threads;
         thread_index++)
    {
        /*
         * Define buffer compartilhado.
         */
        thread_data[thread_index].input =
            input_file_buffer;

        /*
         * Define posição inicial do bloco.
         */
        thread_data[thread_index].start =
            thread_index * block_size;

        /*
         * Última thread recebe restante do arquivo.
         */
        if (thread_index ==
            number_of_threads - 1)
        {
            thread_data[thread_index].end =
                input_file_size;
        }
        else
        {
            thread_data[thread_index].end =
                (thread_index + 1) * block_size;
        }

        /*
         * Define identificador da thread.
         */
        thread_data[thread_index].thread_id =
            thread_index;

        /*
         * Inicializa saída.
         */
        thread_data[thread_index].output = NULL;

        thread_data[thread_index].output_size = 0;

        /*
         * Cria thread.
         */
        pthread_create(
            &threads[thread_index],
            NULL,
            compress_thread,
            &thread_data[thread_index]
        );
    }

    /*
     * Aguarda finalização de todas as threads.
     */
    for (int thread_index = 0;
         thread_index < number_of_threads;
         thread_index++)
    {
        pthread_join(
            threads[thread_index],
            NULL
        );
    }

    printf("\n");
    printf("====================================\n");
    printf("TODAS AS THREADS FINALIZARAM\n");
    printf("====================================\n");

    /*
     * Junta os resultados comprimidos.
     *
     * Aqui vocês podem:
     * - concatenar os blocos;
     * - salvar em arquivo;
     * - gerar saída final.
     */
    for (int thread_index = 0;
         thread_index < number_of_threads;
         thread_index++)
    {
        printf(
            "[THREAD %d] tamanho comprimido: %d bytes\n",
            thread_data[thread_index].thread_id,
            thread_data[thread_index].output_size
        );

        /*
         * Exemplo:
         * salvar resultado da thread no arquivo final.
         */
    }

    /*
     * Libera recursos alocados.
     */
    for (int thread_index = 0;
         thread_index < number_of_threads;
         thread_index++)
    {
        free(thread_data[thread_index].output);
    }

    /*
     * Destroi mutex.
     */
    pthread_mutex_destroy(&write_mutex);

    /*
     * Destroi semáforo.
     */
    sem_destroy(&thread_semaphore);

    printf("\n");
    printf("Compressao paralela concluida.\n");
}