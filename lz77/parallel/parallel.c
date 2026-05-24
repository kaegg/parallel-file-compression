#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#include <time.h>

#include "parallel.h"
#include "../bitio.h"

#define DEFAULT_LA_SIZE 15
#define DEFAULT_SB_SIZE 4095
#define MAX_BIT_BUFFER 16

/* Mutex utilizado apenas para logs/estatisticas compartilhadas. */
static pthread_mutex_t write_mutex;

/* Semaforo usado para controlar a entrada das threads na compressao. */
static sem_t thread_semaphore;

/* Estatistica global protegida por mutex. */
static int total_tokens_generated = 0;

typedef struct {
    const char *input_path;       /* Arquivo comprimido compartilhado. */
    char *temporary_output_path;  /* Arquivo temporario descomprimido do bloco. */
    long long start_bit;          /* Posicao inicial do bloco no arquivo, em bits. */
    int token_count;              /* Quantidade de tokens do bloco. */
    int thread_id;                /* Identificador usado nos logs. */
    int la_size;                  /* Tamanho do lookahead lido do cabecalho. */
    int sb_size;                  /* Tamanho do search-buffer lido do cabecalho. */
    int status;                   /* 0 em sucesso e -1 em erro. */
} DecompressThreadData;

/*
 * Funcao: get_wall_time_seconds
 * -----------------------------
 * Retorna o tempo monotônico atual em segundos para medir desempenho.
 */
static double get_wall_time_seconds(void)
{
    struct timespec current_time; /* Valor retornado por clock_gettime. */

    clock_gettime(CLOCK_MONOTONIC, &current_time);
    return (double) current_time.tv_sec + ((double) current_time.tv_nsec / 1000000000.0);
}

/*
 * Funcao: get_file_size
 * ---------------------
 * Descobre o tamanho de um arquivo ja aberto sem deixar o cursor no final.
 *
 * Retorno:
 *      tamanho em bytes ou -1 em erro.
 */
static long get_file_size(FILE *file)
{
    long size; /* Tamanho obtido por ftell. */

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
 * Funcao: create_temporary_output_path
 * ------------------------------------
 * Monta o caminho de um arquivo temporario associado a uma thread/bloco.
 *
 * output_path:
 *      caminho final escolhido pelo usuario.
 * thread_id:
 *      identificador do bloco usado no sufixo.
 *
 * Retorno:
 *      string alocada dinamicamente ou NULL em erro.
 */
static char *create_temporary_output_path(const char *output_path, int thread_id)
{
    char *temporary_output_path; /* Caminho temporario alocado. */
    int path_size;               /* Tamanho necessario para a string. */

    path_size = snprintf(NULL, 0, "%s.thread_%d.tmp", output_path, thread_id);
    if (path_size < 0) {
        return NULL;
    }

    temporary_output_path = (char *) malloc((size_t) path_size + 1);
    if (temporary_output_path == NULL) {
        return NULL;
    }

    snprintf(
        temporary_output_path,
        (size_t) path_size + 1,
        "%s.thread_%d.tmp",
        output_path,
        thread_id
    );

    return temporary_output_path;
}

/*
 * Funcao: read_bits_from_plain_file
 * ---------------------------------
 * Le uma quantidade arbitraria de bits de um FILE comum, preservando a posicao
 * de bit entre chamadas. E usada para acessar metadados sem depender de
 * struct bitFILE.
 *
 * Retorno:
 *      quantidade de bits lidos ou -1 em erro de parametro.
 */
static int read_bits_from_plain_file(
    FILE *input_file,
    void *info,
    int info_size,
    int bit_count,
    int *input_bit_position,
    int *input_current_byte,
    int *input_has_current_byte
)
{
    int bit_index;                 /* Indice do bit sendo copiado. */
    int output_byte_position = 0;  /* Byte atual no destino info. */
    int output_bit_position = 0;   /* Bit atual no destino info. */

    if (input_file == NULL || info == NULL || info_size <= 0 || bit_count < 0) {
        return -1;
    }

    memset(info, 0, (size_t) info_size);

    for (bit_index = 0; bit_index < bit_count; bit_index++) {
        if (*input_has_current_byte == 0) {
            *input_current_byte = fgetc(input_file);
            if (*input_current_byte == EOF) {
                break;
            }
            *input_has_current_byte = 1;
        }

        if (((unsigned char) *input_current_byte & (1 << *input_bit_position)) != 0) {
            ((unsigned char *) info)[output_byte_position] |= (1 << output_bit_position);
        }

        output_byte_position = (output_bit_position < 7)
            ? output_byte_position
            : output_byte_position + 1;
        output_bit_position = (output_bit_position < 7) ? output_bit_position + 1 : 0;

        *input_bit_position = (*input_bit_position < 7) ? *input_bit_position + 1 : 0;
        if (*input_bit_position == 0) {
            *input_has_current_byte = 0;
        }
    }

    return bit_index;
}

/*
 * Funcao: read_bits_at_position
 * -----------------------------
 * Posiciona o arquivo em uma posicao absoluta de bits e le um campo.
 *
 * Retorno:
 *      quantidade de bits lidos ou -1 em erro.
 */
static int read_bits_at_position(
    FILE *input_file,
    long long start_bit,
    void *info,
    int info_size,
    int bit_count
)
{
    int input_bit_position;       /* Bit inicial dentro do byte posicionado. */
    int input_current_byte = 0;   /* Byte atualmente usado como fonte. */
    int input_has_current_byte = 0; /* Indica se input_current_byte e valido. */

    if (fseek(input_file, (long) (start_bit / 8), SEEK_SET) != 0) {
        return -1;
    }

    input_bit_position = (int) (start_bit % 8);

    return read_bits_from_plain_file(
        input_file,
        info,
        info_size,
        bit_count,
        &input_bit_position,
        &input_current_byte,
        &input_has_current_byte
    );
}

/*
 * Funcao: copy_temporary_bitstream
 * --------------------------------
 * Copia exatamente os bits validos de um bloco comprimido temporario para o
 * arquivo final, preservando alinhamento de bits entre blocos.
 *
 * Retorno:
 *      0 em sucesso e -1 em erro.
 */
static int copy_temporary_bitstream(
    const char *temporary_output_path,
    struct bitFILE *output_file,
    int token_count,
    int la_size,
    int sb_size
)
{
    struct bitFILE *temporary_file; /* Arquivo temporario lido em bits. */
    long long remaining_bits;       /* Bits ainda nao copiados. */
    int bits_per_token;             /* Tamanho de um token em bits. */
    int bits_to_copy;               /* Bits copiados nesta iteracao. */
    int bits_read;                  /* Bits efetivamente lidos. */
    int bits_written;               /* Bits efetivamente gravados. */
    unsigned char bit_buffer;       /* Buffer de ate 8 bits. */
    int status = 0;                 /* Resultado acumulado da copia. */

    temporary_file = bitIO_open(temporary_output_path, BIT_IO_R);
    if (temporary_file == NULL) {
        perror("Opening temporary compressed block");
        return -1;
    }

    bits_per_token = bitof(sb_size) + bitof(la_size) + 8;
    remaining_bits = (long long) token_count * bits_per_token;

    while (remaining_bits > 0) {
        bits_to_copy = (remaining_bits > 8) ? 8 : (int) remaining_bits;
        bit_buffer = 0;

        bits_read = bitIO_read(
            temporary_file,
            &bit_buffer,
            sizeof(bit_buffer),
            bits_to_copy
        );
        if (bits_read != bits_to_copy) {
            status = -1;
            break;
        }

        bits_written = bitIO_write(output_file, &bit_buffer, bits_to_copy);
        if (bits_written != bits_to_copy) {
            status = -1;
            break;
        }

        remaining_bits -= bits_to_copy;
    }

    bitIO_close(temporary_file);
    return status;
}

/*
 * Funcao: compress_thread
 * -----------------------
 * Funcao executada por pthread_create para comprimir um bloco independente.
 * A thread grava o bloco comprimido em um arquivo temporario e registra a
 * quantidade de tokens gerados.
 *
 * thread_arguments:
 *      ponteiro para ThreadData.
 *
 * Retorno:
 *      NULL via pthread_exit.
 */
static void *compress_thread(void *thread_arguments)
{
    ThreadData *thread_data = (ThreadData *) thread_arguments; /* Dados da thread. */

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

    thread_data->status = compress_block_to_file(
        thread_data->input,
        thread_data->start,
        thread_data->end,
        thread_data->la_size,
        thread_data->sb_size,
        thread_data->temporary_output_path,
        &thread_data->output_size
    );

    pthread_mutex_lock(&write_mutex);
    if (thread_data->status == 0) {
        total_tokens_generated += thread_data->output_size;
        printf(
            "[THREAD %d] compressao finalizada: %d tokens\n",
            thread_data->thread_id,
            thread_data->output_size
        );
    } else {
        printf(
            "[THREAD %d] erro ao comprimir bloco\n",
            thread_data->thread_id
        );
    }
    pthread_mutex_unlock(&write_mutex);

    sem_post(&thread_semaphore);
    pthread_exit(NULL);
}

/*
 * Funcao: decompress_thread
 * -------------------------
 * Funcao executada por pthread_create para descomprimir um bloco do arquivo
 * paralelo. Cada thread escreve sua parte em um temporario descomprimido.
 *
 * thread_arguments:
 *      ponteiro para DecompressThreadData.
 *
 * Retorno:
 *      NULL via pthread_exit.
 */
static void *decompress_thread(void *thread_arguments)
{
    DecompressThreadData *thread_data = (DecompressThreadData *) thread_arguments; /* Dados do bloco. */

    if (sem_trywait(&thread_semaphore) != 0) {
        printf("[THREAD %d] aguardando recurso...\n", thread_data->thread_id);
        sem_wait(&thread_semaphore);
    }

    printf(
        "[THREAD %d] descomprimindo %d tokens\n",
        thread_data->thread_id,
        thread_data->token_count
    );

    thread_data->status = decode_tokens_from_file(
        thread_data->input_path,
        thread_data->temporary_output_path,
        thread_data->start_bit,
        thread_data->token_count,
        thread_data->la_size,
        thread_data->sb_size
    );

    pthread_mutex_lock(&write_mutex);
    if (thread_data->status == 0) {
        printf("[THREAD %d] descompressao finalizada\n", thread_data->thread_id);
    } else {
        printf("[THREAD %d] erro ao descomprimir bloco\n", thread_data->thread_id);
    }
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
 * 3. Cada thread comprime um bloco em um arquivo temporario.
 * 4. A thread principal copia os bits validos dos temporarios em ordem.
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
    FILE *input_file = NULL;             /* Arquivo original aberto em bytes. */
    struct bitFILE *output_file = NULL;  /* Arquivo comprimido final. */
    unsigned char *input_buffer = NULL;  /* Conteudo inteiro do arquivo original. */
    pthread_t *threads = NULL;           /* Vetor de threads de compressao. */
    ThreadData *thread_data = NULL;      /* Argumentos passados para cada thread. */
    long long_size;                      /* Tamanho retornado por ftell. */
    int input_file_size;                 /* Tamanho validado em int. */
    int block_size;                      /* Tamanho base de cada bloco. */
    int thread_index;                    /* Indice usado nos loops de threads. */
    int created_threads = 0;             /* Quantidade de threads criadas. */
    int synchronization_initialized = 0; /* Indica se mutex/semaforo devem ser destruidos. */
    int LA_SIZE = (la == -1) ? DEFAULT_LA_SIZE : la; /* Lookahead efetivo. */
    int SB_SIZE = (sb == -1) ? DEFAULT_SB_SIZE : sb; /* Search-buffer efetivo. */
    int status = -1;                     /* Retorno da funcao. */
    double total_start_time;             /* Inicio da compressao paralela. */
    double read_start_time = 0.0;        /* Inicio da leitura do arquivo. */
    double read_end_time = 0.0;          /* Fim da leitura do arquivo. */
    double setup_start_time = 0.0;       /* Inicio da preparacao das estruturas. */
    double setup_end_time = 0.0;         /* Fim da preparacao das estruturas. */
    double compression_start_time = 0.0; /* Inicio da fase com threads. */
    double compression_end_time = 0.0;   /* Fim da fase com threads. */
    double write_start_time = 0.0;       /* Inicio da escrita/copia final. */
    double write_end_time = 0.0;         /* Fim da escrita/copia final. */

    total_start_time = get_wall_time_seconds();

    if (input_path == NULL || output_path == NULL || number_of_threads < 1) {
        return -1;
    }

    if (LA_SIZE < 1) {
        LA_SIZE = DEFAULT_LA_SIZE;
    }

    if (SB_SIZE < 1) {
        SB_SIZE = DEFAULT_SB_SIZE;
    }

    read_start_time = get_wall_time_seconds();

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

    read_end_time = get_wall_time_seconds();

    if (number_of_threads > input_file_size && input_file_size > 0) {
        number_of_threads = input_file_size;
    }

    setup_start_time = get_wall_time_seconds();

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
    synchronization_initialized = 1;
    total_tokens_generated = 0;

    printf("\n====================================\n");
    printf("COMPRESSAO PARALELA INICIADA\n");
    printf("Arquivo: %s\n", input_path);
    printf("Tamanho original: %d bytes\n", input_file_size);
    printf("Threads: %d\n", number_of_threads);
    printf("Lookahead: %d | Search buffer: %d\n", LA_SIZE, SB_SIZE);
    printf("====================================\n");

    setup_end_time = get_wall_time_seconds();
    compression_start_time = get_wall_time_seconds();

    for (thread_index = 0; thread_index < number_of_threads; thread_index++) {
        thread_data[thread_index].input = input_buffer;
        thread_data[thread_index].start = thread_index * block_size;
        thread_data[thread_index].end = (thread_index == number_of_threads - 1)
            ? input_file_size
            : (thread_index + 1) * block_size;
        thread_data[thread_index].thread_id = thread_index;
        thread_data[thread_index].la_size = LA_SIZE;
        thread_data[thread_index].sb_size = SB_SIZE;
        thread_data[thread_index].temporary_output_path =
            create_temporary_output_path(output_path, thread_index);
        thread_data[thread_index].output_size = 0;
        thread_data[thread_index].status = -1;

        if (thread_data[thread_index].temporary_output_path == NULL) {
            fprintf(stderr, "Error creating temporary path for thread %d.\n", thread_index);
            goto join_threads;
        }

        if (pthread_create(&threads[thread_index], NULL, compress_thread, &thread_data[thread_index]) != 0) {
            fprintf(stderr, "Error creating thread %d.\n", thread_index);
            goto join_threads;
        }
        created_threads++;
    }

join_threads:
    for (thread_index = 0; thread_index < created_threads; thread_index++) {
        pthread_join(threads[thread_index], NULL);
    }
    compression_end_time = get_wall_time_seconds();

    if (created_threads != number_of_threads) {
        goto cleanup;
    }

    for (thread_index = 0; thread_index < number_of_threads; thread_index++) {
        if (thread_data[thread_index].status != 0) {
            goto cleanup;
        }
    }

    printf("\n====================================\n");
    printf("TODAS AS THREADS FINALIZARAM\n");
    printf("Total de tokens: %d\n", total_tokens_generated);
    printf("====================================\n");

    write_start_time = get_wall_time_seconds();

    /* Cabecalho paralelo: marcador, magia, parametros e blocos intercalados. */
    bitIO_write(output_file, &(int){LZ77_PARALLEL_MARKER}, MAX_BIT_BUFFER);
    bitIO_write(output_file, &(int){LZ77_PARALLEL_MAGIC}, MAX_BIT_BUFFER);
    bitIO_write(output_file, &SB_SIZE, MAX_BIT_BUFFER);
    bitIO_write(output_file, &LA_SIZE, MAX_BIT_BUFFER);
    bitIO_write(output_file, &number_of_threads, MAX_BIT_BUFFER);

    /* Copia final em ordem para manter o arquivo descomprimivel. */
    for (thread_index = 0; thread_index < number_of_threads; thread_index++) {
        bitIO_write(output_file, &thread_data[thread_index].output_size, 32);

        if (copy_temporary_bitstream(
                thread_data[thread_index].temporary_output_path,
                output_file,
                thread_data[thread_index].output_size,
                LA_SIZE,
                SB_SIZE
            ) != 0) {
            fprintf(stderr, "Error copying temporary block %d.\n", thread_index);
            goto cleanup;
        }
    }
    write_end_time = get_wall_time_seconds();

    status = 0;

cleanup:
    if (status == 0) {
        printf("\n====================================\n");
        printf("TEMPOS DA COMPRESSAO PARALELA\n");
        printf("Leitura do arquivo: %.6f s\n", read_end_time - read_start_time);
        printf("Preparacao: %.6f s\n", setup_end_time - setup_start_time);
        printf("Compressao nas threads: %.6f s\n", compression_end_time - compression_start_time);
        printf("Escrita/copia final: %.6f s\n", write_end_time - write_start_time);
        printf("Tempo total paralelo: %.6f s\n", get_wall_time_seconds() - total_start_time);
        printf("====================================\n");
    }

    if (synchronization_initialized) {
        sem_destroy(&thread_semaphore);
        pthread_mutex_destroy(&write_mutex);
    }

    if (input_file != NULL) {
        fclose(input_file);
    }

    if (output_file != NULL) {
        bitIO_close(output_file);
    }

    if (thread_data != NULL) {
        for (thread_index = 0; thread_index < number_of_threads; thread_index++) {
            if (thread_data[thread_index].temporary_output_path != NULL) {
                remove(thread_data[thread_index].temporary_output_path);
                free(thread_data[thread_index].temporary_output_path);
            }
        }
    }

    free(thread_data);
    free(threads);
    free(input_buffer);

    return status;
}

int decompress_parallel_file(
    const char *input_path,
    const char *output_path,
    int number_of_threads
)
{
    FILE *input_file = NULL;              /* Arquivo comprimido lido em bytes. */
    FILE *output_file = NULL;             /* Arquivo final descomprimido. */
    FILE *temporary_file = NULL;          /* Temporario de bloco durante juncao. */
    pthread_t *threads = NULL;            /* Vetor de threads de descompressao. */
    DecompressThreadData *thread_data = NULL; /* Argumentos por bloco/thread. */
    int *token_counts = NULL;             /* Quantidade de tokens de cada bloco. */
    long long *start_bits = NULL;         /* Offset em bits de cada bloco. */
    int marker;                           /* Marcador do cabecalho paralelo. */
    int magic;                            /* Assinatura do cabecalho paralelo. */
    int SB_SIZE;                          /* Search-buffer lido do cabecalho. */
    int LA_SIZE;                          /* Lookahead lido do cabecalho. */
    int block_count = 0;                  /* Quantidade de blocos comprimidos. */
    int block_index;                      /* Indice usado em loops de blocos. */
    int created_threads = 0;              /* Quantidade de threads criadas. */
    int synchronization_initialized = 0;  /* Indica se mutex/semaforo existem. */
    int bits_per_token;                   /* Tamanho de cada token em bits. */
    int status = -1;                      /* Retorno da funcao. */
    int semaphore_limit;                  /* Maximo de threads simultaneas. */
    long long current_bit;                /* Cursor logico em bits no arquivo. */
    unsigned char copy_buffer[8192];      /* Buffer usado para juntar temporarios. */
    size_t bytes_read;                    /* Bytes lidos de cada temporario. */
    double total_start_time;              /* Inicio da descompressao paralela. */
    double read_start_time = 0.0;         /* Inicio da leitura de metadados. */
    double read_end_time = 0.0;           /* Fim da leitura de metadados. */
    double setup_start_time = 0.0;        /* Inicio da preparacao das threads. */
    double setup_end_time = 0.0;          /* Fim da preparacao das threads. */
    double decompression_start_time = 0.0;/* Inicio da fase com threads. */
    double decompression_end_time = 0.0;  /* Fim da fase com threads. */
    double write_start_time = 0.0;        /* Inicio da juncao final. */
    double write_end_time = 0.0;          /* Fim da juncao final. */

    total_start_time = get_wall_time_seconds();

    if (input_path == NULL || output_path == NULL || number_of_threads < 1) {
        return -1;
    }

    read_start_time = get_wall_time_seconds();
    input_file = fopen(input_path, "rb");
    if (input_file == NULL) {
        perror("Opening input file");
        goto cleanup;
    }

    current_bit = 0;

    if (read_bits_at_position(input_file, current_bit, &marker, sizeof(marker), MAX_BIT_BUFFER) != MAX_BIT_BUFFER) {
        goto cleanup;
    }
    current_bit += MAX_BIT_BUFFER;

    if (marker != LZ77_PARALLEL_MARKER) {
        fprintf(stderr, "Arquivo sem cabecalho paralelo; use descompressao sequencial.\n");
        goto cleanup;
    }

    if (read_bits_at_position(input_file, current_bit, &magic, sizeof(magic), MAX_BIT_BUFFER) != MAX_BIT_BUFFER) {
        goto cleanup;
    }
    current_bit += MAX_BIT_BUFFER;
    if (magic != LZ77_PARALLEL_MAGIC) {
        fprintf(stderr, "Cabecalho paralelo invalido.\n");
        goto cleanup;
    }

    if (read_bits_at_position(input_file, current_bit, &SB_SIZE, sizeof(SB_SIZE), MAX_BIT_BUFFER) != MAX_BIT_BUFFER) {
        goto cleanup;
    }
    current_bit += MAX_BIT_BUFFER;

    if (read_bits_at_position(input_file, current_bit, &LA_SIZE, sizeof(LA_SIZE), MAX_BIT_BUFFER) != MAX_BIT_BUFFER) {
        goto cleanup;
    }
    current_bit += MAX_BIT_BUFFER;

    if (read_bits_at_position(input_file, current_bit, &block_count, sizeof(block_count), MAX_BIT_BUFFER) != MAX_BIT_BUFFER) {
        goto cleanup;
    }
    current_bit += MAX_BIT_BUFFER;

    if (SB_SIZE < 1 || LA_SIZE < 1 || block_count < 1) {
        fprintf(stderr, "Cabecalho paralelo invalido.\n");
        goto cleanup;
    }

    token_counts = (int *) calloc((size_t) block_count, sizeof(int));
    start_bits = (long long *) calloc((size_t) block_count, sizeof(long long));
    if (token_counts == NULL || start_bits == NULL) {
        fprintf(stderr, "Memory allocation error.\n");
        goto cleanup;
    }

    bits_per_token = bitof(SB_SIZE) + bitof(LA_SIZE) + 8;

    for (block_index = 0; block_index < block_count; block_index++) {
        if (read_bits_at_position(
                input_file,
                current_bit,
                &token_counts[block_index],
                sizeof(int),
                32
            ) != 32) {
            fprintf(stderr, "Erro lendo metadados do bloco %d.\n", block_index);
            goto cleanup;
        }

        current_bit += 32;
        start_bits[block_index] = current_bit;

        if (token_counts[block_index] < 0) {
            fprintf(stderr, "Quantidade de tokens invalida no bloco %d.\n", block_index);
            goto cleanup;
        }

        current_bit += (long long) token_counts[block_index] * bits_per_token;
    }

    fclose(input_file);
    input_file = NULL;
    read_end_time = get_wall_time_seconds();

    setup_start_time = get_wall_time_seconds();

    threads = (pthread_t *) malloc((size_t) block_count * sizeof(pthread_t));
    thread_data = (DecompressThreadData *) calloc((size_t) block_count, sizeof(DecompressThreadData));
    if (threads == NULL || thread_data == NULL) {
        fprintf(stderr, "Memory allocation error.\n");
        goto cleanup;
    }

    semaphore_limit = (number_of_threads < block_count) ? number_of_threads : block_count;
    pthread_mutex_init(&write_mutex, NULL);
    sem_init(&thread_semaphore, 0, semaphore_limit);
    synchronization_initialized = 1;

    printf("\n====================================\n");
    printf("DESCOMPRESSAO PARALELA INICIADA\n");
    printf("Arquivo: %s\n", input_path);
    printf("Blocos no arquivo: %d\n", block_count);
    printf("Threads: %d\n", semaphore_limit);
    printf("Lookahead: %d | Search buffer: %d\n", LA_SIZE, SB_SIZE);
    printf("====================================\n");

    setup_end_time = get_wall_time_seconds();
    decompression_start_time = get_wall_time_seconds();

    for (block_index = 0; block_index < block_count; block_index++) {
        thread_data[block_index].input_path = input_path;
        thread_data[block_index].temporary_output_path =
            create_temporary_output_path(output_path, block_index);
        thread_data[block_index].start_bit = start_bits[block_index];
        thread_data[block_index].token_count = token_counts[block_index];
        thread_data[block_index].thread_id = block_index;
        thread_data[block_index].la_size = LA_SIZE;
        thread_data[block_index].sb_size = SB_SIZE;
        thread_data[block_index].status = -1;

        if (thread_data[block_index].temporary_output_path == NULL) {
            fprintf(stderr, "Error creating temporary path for block %d.\n", block_index);
            goto join_threads;
        }

        if (pthread_create(&threads[block_index], NULL, decompress_thread, &thread_data[block_index]) != 0) {
            fprintf(stderr, "Error creating thread %d.\n", block_index);
            goto join_threads;
        }
        created_threads++;
    }

join_threads:
    for (block_index = 0; block_index < created_threads; block_index++) {
        pthread_join(threads[block_index], NULL);
    }
    decompression_end_time = get_wall_time_seconds();

    if (created_threads != block_count) {
        goto cleanup;
    }

    for (block_index = 0; block_index < block_count; block_index++) {
        if (thread_data[block_index].status != 0) {
            goto cleanup;
        }
    }

    write_start_time = get_wall_time_seconds();

    output_file = fopen(output_path, "wb");
    if (output_file == NULL) {
        perror("Opening output file");
        goto cleanup;
    }

    for (block_index = 0; block_index < block_count; block_index++) {
        temporary_file = fopen(thread_data[block_index].temporary_output_path, "rb");
        if (temporary_file == NULL) {
            perror("Opening temporary decompressed block");
            goto cleanup;
        }

        while ((bytes_read = fread(copy_buffer, 1, sizeof(copy_buffer), temporary_file)) > 0) {
            if (fwrite(copy_buffer, 1, bytes_read, output_file) != bytes_read) {
                fprintf(stderr, "Error writing output file.\n");
                fclose(temporary_file);
                temporary_file = NULL;
                goto cleanup;
            }
        }

        fclose(temporary_file);
        temporary_file = NULL;
    }

    write_end_time = get_wall_time_seconds();
    status = 0;

cleanup:
    if (status == 0) {
        printf("\n====================================\n");
        printf("TEMPOS DA DESCOMPRESSAO PARALELA\n");
        printf("Leitura/metadados: %.6f s\n", read_end_time - read_start_time);
        printf("Preparacao: %.6f s\n", setup_end_time - setup_start_time);
        printf("Descompressao nas threads: %.6f s\n", decompression_end_time - decompression_start_time);
        printf("Escrita/juncao final: %.6f s\n", write_end_time - write_start_time);
        printf("Tempo total paralelo: %.6f s\n", get_wall_time_seconds() - total_start_time);
        printf("====================================\n");
    }

    if (temporary_file != NULL) {
        fclose(temporary_file);
    }

    if (output_file != NULL) {
        fclose(output_file);
    }

    if (input_file != NULL) {
        fclose(input_file);
    }

    if (synchronization_initialized) {
        sem_destroy(&thread_semaphore);
        pthread_mutex_destroy(&write_mutex);
    }

    if (thread_data != NULL) {
        for (block_index = 0; block_index < block_count; block_index++) {
            if (thread_data[block_index].temporary_output_path != NULL) {
                remove(thread_data[block_index].temporary_output_path);
                free(thread_data[block_index].temporary_output_path);
            }
        }
    }

    free(thread_data);
    free(threads);
    free(token_counts);
    free(start_bits);

    return status;
}
