/***************************************************************************
 *          Lempel, Ziv Encoding and Decoding
 *
 *   File    : main.c
 *   Authors : David Costa and Pietro De Rosa
 *
 *                              DESCRIPTION
 *
 *   LZ77 is a lossless data compression algorithms published by Abraham 
 *   Lempel and Jacob Ziv in 1977. It is a dictionary coder and maintains a
 *   sliding window during compression. The sliding window is divided in two
 *   parts: Search-Buffer (dictionary - encoded data) and Lookahead (uncomp-
 *   ressed data). LZ77 algorithms achieve compression by addressing byte 
 *   sequences from former contents instead of the original data. All data 
 *   will be coded in the same form (called token): -Address to already 
 *   coded contents; -Sequence length; -First deviating symbol.
 *   The window is contained in a fixed size buffer.
 *   The match between SB and LA is made by a binary tree, implemented in an
 *   array.
 ***************************************************************************/

/***************************************************************************
 *                             INCLUDED FILES
 ***************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "getopt.h"
#include "bitio.h"
#include "lz77.h"
#include "parallel/parallel.h"

/***************************************************************************
 *                                CONSTANTS
 ***************************************************************************/
#define MIN_LA_SIZE 2       /* min lookahead size */
#define MAX_LA_SIZE 255     /* max lookahead size */
#define MIN_SB_SIZE 1       /* min search buffer size */
#define MAX_SB_SIZE 65535   /* max search buffer size */

/***************************************************************************
 *                            TYPE DEFINITIONS
 ***************************************************************************/
typedef enum{
    ENCODE, /* Modo de compressao selecionado por -c. */
    DECODE  /* Modo de descompressao selecionado por -d. */
} MODES;

/*
 * Funcao: get_wall_time_seconds
 * -----------------------------
 * Retorna o tempo monotônico atual em segundos.
 *
 * Retorno:
 *      valor em segundos, usado para medir duracao das fases do programa.
 */
static double get_wall_time_seconds(void)
{
    struct timespec current_time; /* Tempo monotônico retornado pelo SO. */

    clock_gettime(CLOCK_MONOTONIC, &current_time);
    return (double) current_time.tv_sec + ((double) current_time.tv_nsec / 1000000000.0);
}

/***************************************************************************
 *                            USER INTERFACE
 * Syntax: ./lz77 <options>
 * Options: -c: compression mode
 *          -d: decompression mode
 *          -i <filename>: input file
 *          -o <filename>: output file
 *          -l <value> : lookahead size (default 15)
 *          -s <value> : search-buffer size (default 4095)
 *          -h: help
 ***************************************************************************/
int main(int argc, char *argv[])
{
    /* variables */
    int opt;                         /* Opcao atual lida por getopt. */
    FILE *file = NULL;               /* Arquivo comum usado por fread/fwrite. */
    struct bitFILE *bitF = NULL;     /* Arquivo com leitura/escrita em bits. */
    MODES mode = -1;                 /* Modo escolhido: ENCODE ou DECODE. */
    char *filenameIn = NULL;         /* Caminho do arquivo de entrada. */
    char *filenameOut = NULL;        /* Caminho do arquivo de saida. */
    int la_size = -1;                /* Lookahead; -1 usa padrao. */
    int sb_size = -1;                /* Search-buffer; -1 usa padrao. */
    int num_threads = 1;             /* 1 executa versao sequencial. */
    double operation_start_time;     /* Inicio da operacao principal. */
    double operation_end_time;       /* Fim da operacao principal. */
    double close_start_time;         /* Inicio do fechamento/flush do bitFILE. */
    double sequential_setup_start_time = 0.0; /* Inicio da preparacao sequencial. */
    double sequential_setup_end_time = 0.0;   /* Fim da preparacao sequencial. */
    LZ77Timing sequential_timing;    /* Tempos detalhados da versao sequencial. */

    sequential_timing.read_time = 0.0;
    sequential_timing.compression_time = 0.0;
    sequential_timing.write_time = 0.0;
    sequential_timing.total_time = 0.0;
    
    /* Interpreta os argumentos da linha de comando. */
    while ((opt = getopt(argc, argv, "cdi:o:l:s:p:h")) != -1)
    {
        switch(opt)
        {
            case 'c':       /* compression mode */
                mode = ENCODE;
                break;
                
            case 'd':       /* decompression mode */
                mode = DECODE;
                break;
                
            case 'i':       /* input file name */
                if (filenameIn != NULL){
                    fprintf(stderr, "Multiple input files not allowed.\n");
                    goto error;
                }
                filenameIn = malloc(strlen(optarg) + 1);
                strcpy(filenameIn, optarg);
                
                break;
                
            case 'o':       /* output file name */
                if (filenameOut != NULL){
                    fprintf(stderr, "Multiple output files not allowed.\n");
                    goto error;
                }
                filenameOut = malloc(strlen(optarg) + 1);
                strcpy(filenameOut, optarg);
                
                break;
                
            case 'l':       /* lookahead size */
                la_size = atoi(optarg);
                if (la_size < MIN_LA_SIZE || la_size > MAX_LA_SIZE){
                    fprintf(stderr, "Bad lookahead size value.\n");
                    goto error;
                }
                break;
                
            case 's':       /* search-buffer size */
                sb_size = atoi(optarg);
                if (sb_size < MIN_SB_SIZE || sb_size > MAX_SB_SIZE){
                    fprintf(stderr, "Bad search-buffer size value.\n");
                    goto error;
                }
                break;

            case 'p':       /* parallel compression */
                num_threads = atoi(optarg);
                if (num_threads < 1){
                    fprintf(stderr, "Bad thread count value.\n");
                    goto error;
                }
                break;
                
            case 'h':       /* help */
                printf("Usage: lz77 <options>\n");
                printf("  -c : Encode input file to output file.\n");
                printf("  -d : Decode input file to output file.\n");
                printf("  -i <filename> : Name of input file.\n");
                printf("  -o <filename> : Name of output file.\n");
                printf("  -l <value> : Lookahead size (default 15)\n");
                printf("  -s <value> : Search-buffer size (default 4095)\n");
                printf("  -p <threads> : Parallel compression/decompression using N threads.\n");
                printf("  -h : Command line options.\n\n");
                break;
                
        }
    }
    
    /* validate command line */
    if (filenameIn == NULL){
        fprintf(stderr, "Input file must be provided\n");
        goto error;
    }else if (filenameOut == NULL)
    {
        fprintf(stderr, "Output file must be provided\n");
        goto error;
    }

    /* A partir daqui medimos o tempo total da operacao pedida. */
    operation_start_time = get_wall_time_seconds();
    
    if (mode == ENCODE){
        if (num_threads > 1) {
            if (compress_parallel_file(filenameIn, filenameOut, num_threads, la_size, sb_size) != 0) {
                fprintf(stderr, "Parallel compression failed.\n");
                goto error;
            }
        } else {
            sequential_setup_start_time = get_wall_time_seconds();
            if ((file = fopen(filenameIn, "rb")) == NULL){
                perror("Opening input file");
                goto error;
            }
            if ((bitF = bitIO_open(filenameOut, BIT_IO_W)) == NULL) {
                perror("Opening output file");
                goto error;
            }
            sequential_setup_end_time = get_wall_time_seconds();
            encode_with_timing(file, bitF, la_size, sb_size, &sequential_timing);
        }
            
    }else if (mode == DECODE){
        if (num_threads > 1) {
            if (decompress_parallel_file(filenameIn, filenameOut, num_threads) != 0) {
                fprintf(stderr, "Parallel decompression failed.\n");
                goto error;
            }
        } else {
            sequential_setup_start_time = get_wall_time_seconds();
            if ((bitF = bitIO_open(filenameIn, BIT_IO_R)) == NULL) {
                perror("Opening input file");
                goto error;
            }
            if ((file = fopen(filenameOut, "wb")) == NULL){
                perror("Opening output file");
                goto error;
            }
            sequential_setup_end_time = get_wall_time_seconds();
            decode_with_timing(bitF, file, &sequential_timing);
        }
            
    }else{
        fprintf(stderr, "Select ENCODE or DECODE mode\n");
        goto error;
    }

    /* Fecha arquivos antes de imprimir tempos para incluir flush no total. */
    if (file != NULL) {
        fclose(file);
        file = NULL;
    }
    if (bitF != NULL) {
        close_start_time = get_wall_time_seconds();
        bitIO_close(bitF);
        if ((mode == ENCODE || mode == DECODE) && num_threads == 1) {
            sequential_timing.write_time += get_wall_time_seconds() - close_start_time;
        }
        bitF = NULL;
    }

    operation_end_time = get_wall_time_seconds();

    /* Impressao detalhada das fases sequenciais. */
    if (mode == ENCODE && num_threads == 1) {
        printf("\n====================================\n");
        printf("TEMPOS DA COMPRESSAO SEQUENCIAL\n");
        printf("Leitura do arquivo: %.6f s\n", sequential_timing.read_time);
        printf("Preparacao: %.6f s\n", sequential_setup_end_time - sequential_setup_start_time);
        printf("Compressao sequencial: %.6f s\n", sequential_timing.compression_time);
        printf("Escrita: %.6f s\n", sequential_timing.write_time);
        printf("Tempo total sequencial: %.6f s\n", operation_end_time - operation_start_time);
        printf("====================================\n");
    } else if (mode == DECODE && num_threads == 1) {
        printf("\n====================================\n");
        printf("TEMPOS DA DESCOMPRESSAO SEQUENCIAL\n");
        printf("Leitura/metadados: %.6f s\n", sequential_timing.read_time);
        printf("Preparacao: %.6f s\n", sequential_setup_end_time - sequential_setup_start_time);
        printf("Descompressao sequencial: %.6f s\n", sequential_timing.compression_time);
        printf("Escrita/juncao final: %.6f s\n", sequential_timing.write_time);
        printf("Tempo total sequencial: %.6f s\n", operation_end_time - operation_start_time);
        printf("====================================\n");
    }

    /* Impressao resumida do tempo total. */
    printf("\n====================================\n");
    if (mode == ENCODE && num_threads > 1) {
        printf("TEMPO TOTAL DA COMPRESSAO PARALELA: %.6f s\n", operation_end_time - operation_start_time);
    } else if (mode == ENCODE) {
        printf("TEMPO TOTAL DA COMPRESSAO SEQUENCIAL: %.6f s\n", operation_end_time - operation_start_time);
    } else if (mode == DECODE && num_threads > 1) {
        printf("TEMPO TOTAL DA DESCOMPRESSAO PARALELA: %.6f s\n", operation_end_time - operation_start_time);
    } else {
        printf("TEMPO TOTAL DA DESCOMPRESSAO: %.6f s\n", operation_end_time - operation_start_time);
    }
    printf("====================================\n");
    
    if (file != NULL) {
        fclose(file);
    }
    if (bitF != NULL) {
        bitIO_close(bitF);
    }
    return 0;
    
    /* handle error */
error:
    if (file != NULL){
        fclose(file);
    }
    if (bitF != NULL){
        bitIO_close(bitF);
    }
    exit (EXIT_FAILURE);
}
