/***************************************************************************
 *          Lempel, Ziv Encoding and Decoding
 *
 *   File    : lz77.c
 *   Authors : David Costa and Pietro De Rosa
 *
 ***************************************************************************/

/***************************************************************************
 *                             INCLUDED FILES
 ***************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bitio.h"
#include "tree.h"
#include "lz77.h"

/***************************************************************************
 *                                CONSTANTS
 ***************************************************************************/
#define DEFAULT_LA_SIZE 15      /* lookahead size */
#define DEFAULT_SB_SIZE 4095    /* search buffer size */
#define N 3
#define MAX_BIT_BUFFER 16

/***************************************************************************
 *                         FUNCTIONS DECLARATION
 ***************************************************************************/
void writecode(struct token t, struct bitFILE *out, int la_size, int sb_size);

struct token match(struct node *tree, int root, unsigned char *window, int la, int la_size);

static double get_wall_time_seconds(void)
{
    struct timespec current_time; /* Tempo monotônico retornado pelo sistema. */

    clock_gettime(CLOCK_MONOTONIC, &current_time);
    return (double) current_time.tv_sec + ((double) current_time.tv_nsec / 1000000000.0);
}

/*
 * Funcao: read_bits_from_file
 * ---------------------------
 * Le bits de um FILE comum preservando a posicao de bit entre chamadas.
 * Usada pela descompressao paralela para iniciar a leitura diretamente em um
 * offset de bit do arquivo comprimido.
 *
 * Retorno:
 *      quantidade de bits lidos ou -1 em erro de parametro.
 */
static int read_bits_from_file(
    FILE *input_file,
    void *info,
    int info_size,
    int bit_count,
    int *input_bit_position,
    int *input_current_byte,
    int *input_has_current_byte
)
{
    int bit_index;                /* Indice do bit sendo lido. */
    int output_byte_position = 0; /* Byte atual no destino. */
    int output_bit_position = 0;  /* Bit atual no destino. */

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

/***************************************************************************
 *                            ENCODE FUNCTION
 * Name         : encode - compress file
 * Parameters   : file - file to encode
 *                out - compressed file
 ***************************************************************************/
void encode(FILE *file, struct bitFILE *out, int la, int sb)
{
    encode_with_timing(file, out, la, sb, NULL);
}

void encode_with_timing(FILE *file, struct bitFILE *out, int la, int sb, LZ77Timing *timing)
{
    /* variables */
    int i, root = -1;          /* i percorre bytes do token; root e raiz da arvore. */
    int eof;                   /* Indica se fread ja atingiu fim do arquivo. */
    struct node *tree;         /* Arvore binaria usada para buscar matches. */
    struct token t;            /* Token gerado a cada iteracao. */
    unsigned char *window;     /* Janela deslizante com SB + lookahead. */
    int la_size, sb_size = 0;  /* Tamanhos atuais de lookahead e search-buffer. */
    int buff_size;             /* Bytes validos ainda presentes na janela. */
    int sb_index = 0;          /* Inicio atual do search-buffer na janela. */
    int la_index = 0;          /* Inicio atual do lookahead na janela. */
    int LA_SIZE, SB_SIZE, WINDOW_SIZE; /* Parametros efetivos da janela. */
    double total_start_time;   /* Inicio da compressao sequencial. */
    double phase_start_time;   /* Inicio de uma subfase medida. */
    double read_time = 0.0;    /* Tempo acumulado lendo entrada. */
    double write_time = 0.0;   /* Tempo acumulado escrevendo saida. */
    double total_time;         /* Tempo total da funcao. */

    total_start_time = get_wall_time_seconds();

    if (timing != NULL) {
        timing->read_time = 0.0;
        timing->compression_time = 0.0;
        timing->write_time = 0.0;
        timing->total_time = 0.0;
    }
    
    /* set window parameters */
    LA_SIZE = (la == -1) ? DEFAULT_LA_SIZE : la;
    SB_SIZE = (sb == -1) ? DEFAULT_SB_SIZE : sb;
    WINDOW_SIZE = (SB_SIZE * N) + LA_SIZE;
    
    window = calloc(WINDOW_SIZE, sizeof(unsigned char));
    
    tree = createTree(SB_SIZE);
    
    /* write header */
    phase_start_time = get_wall_time_seconds();
    bitIO_write(out, &SB_SIZE, MAX_BIT_BUFFER);
    bitIO_write(out, &LA_SIZE, MAX_BIT_BUFFER);
    write_time += get_wall_time_seconds() - phase_start_time;
    
    /* fill the lookahead with the first LA_SIZE bytes or until EOF is reached */
    phase_start_time = get_wall_time_seconds();
    buff_size = fread(window, 1, WINDOW_SIZE, file);
    read_time += get_wall_time_seconds() - phase_start_time;
    if(ferror(file)) {
        printf("Error loading the data in the window.\n");
        return;
   	}
    
    eof = feof(file);
    
    /* set lookahead's size */
    la_size = (buff_size > LA_SIZE) ? LA_SIZE : buff_size;
    
	while(buff_size > 0){
		
        /* find the longest match of the lookahead in the tree*/
        t = match(tree, root, window, la_index, la_size);
        
        /* write the token in the output file */
        phase_start_time = get_wall_time_seconds();
        writecode(t, out, LA_SIZE, SB_SIZE);
        write_time += get_wall_time_seconds() - phase_start_time;
        
        /* read as many bytes as matched in the previuos iteration */
        for(i = 0; i < t.len + 1; i++){
            
            /* if search buffer's length is max, the oldest node is removed from the tree */
            if(sb_size == SB_SIZE){
                delete(tree, &root, window, sb_index, SB_SIZE);
                sb_index++;
            }else
                sb_size++;
            
            /* insert a new node in the tree */
            insert(tree, &root, window, la_index, la_size, SB_SIZE);
            la_index++;
            
            if (eof == 0){
                /* scroll backward the buffer when it is almost full */
                if (sb_index == SB_SIZE * (N - 1)){
                    memmove(window, &(window[sb_index]), sb_size+la_size);
                    
                    /* update the node's offset when the buffer is scrolled */
                    updateOffset(tree, sb_index, SB_SIZE);
                    
                    sb_index = 0;
                    la_index = sb_size;
                    
                    /* read from file */
                    phase_start_time = get_wall_time_seconds();
                    buff_size += fread(&(window[sb_size+la_size]), 1, WINDOW_SIZE-(sb_size+la_size), file);
                    read_time += get_wall_time_seconds() - phase_start_time;
                    if(ferror(file)) {
                        printf("Error loading the data in the window.\n");
                        return;
                    }
                    eof = feof(file);
                }
            }
            
            buff_size--;
            /* case where we hit EOF before filling lookahead */
            la_size = (buff_size > LA_SIZE) ? LA_SIZE : buff_size;
        }
	}
    
    destroyTree(tree);
    free(window);

    total_time = get_wall_time_seconds() - total_start_time;
    if (timing != NULL) {
        timing->read_time = read_time;
        timing->write_time = write_time;
        timing->total_time = total_time;
        timing->compression_time = total_time - read_time - write_time;
        if (timing->compression_time < 0.0) {
            timing->compression_time = 0.0;
        }
    }
}

/***************************************************************************
 *                            DECODE FUNCTION
 * Name         : decode - decompress file
 * Parameters   : file - compressed file
 *                out - output file
 ***************************************************************************/
void decode(struct bitFILE *file, FILE *out)
{
    decode_with_timing(file, out, NULL);
}

void decode_with_timing(struct bitFILE *file, FILE *out, LZ77Timing *timing)
{
    /* variables */
    struct token t;            /* Token lido do arquivo comprimido. */
    int back = 0, off;         /* back aponta fim do buffer; off aponta copia. */
    unsigned char *buffer;     /* Buffer circular usado na reconstrucao. */
    int SB_SIZE, LA_SIZE, WINDOW_SIZE; /* Parametros lidos do cabecalho. */
    int magic;                 /* Assinatura do formato paralelo. */
    int block_count;           /* Quantidade de blocos no formato paralelo. */
    int block_index;           /* Indice do bloco atual. */
    int token_index;           /* Indice do token dentro de um bloco. */
    int token_count;           /* Tokens existentes no bloco atual. */
    double total_start_time;   /* Inicio da descompressao sequencial. */
    double phase_start_time;   /* Inicio de uma subfase medida. */
    double read_time = 0.0;    /* Tempo acumulado lendo entrada. */
    double write_time = 0.0;   /* Tempo acumulado escrevendo saida. */
    double total_time;         /* Tempo total da funcao. */

    total_start_time = get_wall_time_seconds();

    if (timing != NULL) {
        timing->read_time = 0.0;
        timing->compression_time = 0.0;
        timing->write_time = 0.0;
        timing->total_time = 0.0;
    }
    
    /* read header */
    phase_start_time = get_wall_time_seconds();
    bitIO_read(file, &SB_SIZE, sizeof(SB_SIZE), MAX_BIT_BUFFER);
    read_time += get_wall_time_seconds() - phase_start_time;

    if (SB_SIZE == LZ77_PARALLEL_MARKER) {
        phase_start_time = get_wall_time_seconds();
        bitIO_read(file, &magic, sizeof(magic), MAX_BIT_BUFFER);
        read_time += get_wall_time_seconds() - phase_start_time;
        if (magic != LZ77_PARALLEL_MAGIC) {
            fprintf(stderr, "Invalid compressed file header.\n");
            return;
        }

        phase_start_time = get_wall_time_seconds();
        bitIO_read(file, &SB_SIZE, sizeof(SB_SIZE), MAX_BIT_BUFFER);
        bitIO_read(file, &LA_SIZE, sizeof(LA_SIZE), MAX_BIT_BUFFER);
        bitIO_read(file, &block_count, sizeof(block_count), MAX_BIT_BUFFER);
        read_time += get_wall_time_seconds() - phase_start_time;

        WINDOW_SIZE = (SB_SIZE * N) + LA_SIZE;
        buffer = (unsigned char*)calloc(WINDOW_SIZE, sizeof(unsigned char));

        for (block_index = 0; block_index < block_count; block_index++) {
            phase_start_time = get_wall_time_seconds();
            bitIO_read(file, &token_count, sizeof(token_count), 32);
            read_time += get_wall_time_seconds() - phase_start_time;
            back = 0;

            for (token_index = 0; token_index < token_count; token_index++) {
                phase_start_time = get_wall_time_seconds();
                t = readcode(file, LA_SIZE, SB_SIZE);
                read_time += get_wall_time_seconds() - phase_start_time;

                if(back + t.len > WINDOW_SIZE - 1){
                    memcpy(buffer, &(buffer[back - SB_SIZE]), SB_SIZE);
                    back = SB_SIZE;
                }

                while(t.len > 0)
                {
                    off = back - t.off;
                    buffer[back] = buffer[off];
                    phase_start_time = get_wall_time_seconds();
                    putc(buffer[back], out);
                    write_time += get_wall_time_seconds() - phase_start_time;
                    back++;
                    t.len--;
                }
                buffer[back] = t.next;
                phase_start_time = get_wall_time_seconds();
                putc(buffer[back], out);
                write_time += get_wall_time_seconds() - phase_start_time;
                back++;
            }
        }

        free(buffer);
        total_time = get_wall_time_seconds() - total_start_time;
        if (timing != NULL) {
            timing->read_time = read_time;
            timing->write_time = write_time;
            timing->total_time = total_time;
            timing->compression_time = total_time - read_time - write_time;
            if (timing->compression_time < 0.0) {
                timing->compression_time = 0.0;
            }
        }
        return;
    }

    phase_start_time = get_wall_time_seconds();
    bitIO_read(file, &LA_SIZE, sizeof(LA_SIZE), MAX_BIT_BUFFER);
    read_time += get_wall_time_seconds() - phase_start_time;
    
    WINDOW_SIZE = (SB_SIZE * N) + LA_SIZE;
    
    buffer = (unsigned char*)calloc(WINDOW_SIZE, sizeof(unsigned char));
    
    while(1)
    {
        /* read the code from the input file */
        phase_start_time = get_wall_time_seconds();
        t = readcode(file, LA_SIZE, SB_SIZE);
        read_time += get_wall_time_seconds() - phase_start_time;

        if(t.off == -1)
            break;
        
        if(back + t.len > WINDOW_SIZE - 1){
            memcpy(buffer, &(buffer[back - SB_SIZE]), SB_SIZE);
            back = SB_SIZE;
        }
        
        /* reconstruct the original byte*/
        while(t.len > 0)
        {
            off = back - t.off;
            buffer[back] = buffer[off];
            
            /* write the byte in the output file*/
            phase_start_time = get_wall_time_seconds();
            putc(buffer[back], out);
            write_time += get_wall_time_seconds() - phase_start_time;
            
            back++;
            t.len--;
        }
        buffer[back] = t.next;
        
        /* write the byte in the output file*/
        phase_start_time = get_wall_time_seconds();
        putc(buffer[back], out);
        write_time += get_wall_time_seconds() - phase_start_time;
        
        back++;
    }

    free(buffer);

    total_time = get_wall_time_seconds() - total_start_time;
    if (timing != NULL) {
        timing->read_time = read_time;
        timing->write_time = write_time;
        timing->total_time = total_time;
        timing->compression_time = total_time - read_time - write_time;
        if (timing->compression_time < 0.0) {
            timing->compression_time = 0.0;
        }
    }
    
}

int decode_tokens_from_file(
    const char *input_path,
    const char *output_path,
    long long start_bit,
    int token_count,
    int la_size,
    int sb_size
)
{
    FILE *input_file;              /* Arquivo comprimido fonte. */
    FILE *output_file;             /* Temporario descomprimido do bloco. */
    struct token t;                /* Token lido da entrada. */
    unsigned char *buffer;         /* Buffer de reconstrucao LZ77. */
    int WINDOW_SIZE;               /* Tamanho do buffer de reconstrucao. */
    int back = 0;                  /* Proxima posicao livre no buffer. */
    int off;                       /* Posicao de origem para copia. */
    int token_index;               /* Indice do token atual. */
    int input_bit_position;        /* Bit inicial dentro do byte atual. */
    int input_current_byte = 0;    /* Byte fonte atual. */
    int input_has_current_byte = 0;/* Indica se input_current_byte e valido. */
    int status = -1;               /* Resultado da descompressao do bloco. */

    input_file = fopen(input_path, "rb");
    if (input_file == NULL) {
        return -1;
    }

    output_file = fopen(output_path, "wb");
    if (output_file == NULL) {
        fclose(input_file);
        return -1;
    }

    if (fseek(input_file, (long) (start_bit / 8), SEEK_SET) != 0) {
        goto cleanup;
    }
    input_bit_position = (int) (start_bit % 8);

    WINDOW_SIZE = (sb_size * N) + la_size;
    buffer = (unsigned char*)calloc(WINDOW_SIZE, sizeof(unsigned char));
    if (buffer == NULL) {
        goto cleanup;
    }

    for (token_index = 0; token_index < token_count; token_index++) {
        if (read_bits_from_file(
                input_file,
                &t.off,
                sizeof(t.off),
                bitof(sb_size),
                &input_bit_position,
                &input_current_byte,
                &input_has_current_byte
            ) != bitof(sb_size)) {
            free(buffer);
            goto cleanup;
        }

        if (read_bits_from_file(
                input_file,
                &t.len,
                sizeof(t.len),
                bitof(la_size),
                &input_bit_position,
                &input_current_byte,
                &input_has_current_byte
            ) != bitof(la_size)) {
            free(buffer);
            goto cleanup;
        }

        if (read_bits_from_file(
                input_file,
                &t.next,
                sizeof(t.next),
                8,
                &input_bit_position,
                &input_current_byte,
                &input_has_current_byte
            ) != 8) {
            free(buffer);
            goto cleanup;
        }

        if(back + t.len > WINDOW_SIZE - 1){
            memcpy(buffer, &(buffer[back - sb_size]), sb_size);
            back = sb_size;
        }

        while(t.len > 0)
        {
            off = back - t.off;
            buffer[back] = buffer[off];
            putc(buffer[back], output_file);
            back++;
            t.len--;
        }

        buffer[back] = t.next;
        putc(buffer[back], output_file);
        back++;
    }

    free(buffer);
    status = 0;

cleanup:
    fclose(output_file);
    fclose(input_file);
    return status;
}

/***************************************************************************
 *                            MATCH FUNCTION
 * Name         : match - find the longest match and create the token
 * Parameters   : tree - binary search tree
 *                root - index of the root
 *                window - pointer to the buffer
 *                la - starting index of the lookahead
 *                la_size - actual lookahead size
 * Returned     : token of the best match
 ***************************************************************************/
struct token match(struct node *tree, int root, unsigned char *window, int la, int la_size)
{
    /* variables */
    struct token t;
    struct ret r;
    
    /* find the longest match */
    r = find(tree, root, window, la, la_size);
    
    /* create the token */
    t.off = r.off;
    t.len = r.len;
    t.next = window[la+r.len];
    
    return t;
}


/***************************************************************************
 *                       APPEND TOKEN HELPER FUNCTION
 * Adds a token to a dynamically allocated vector.
 ***************************************************************************/
static int append_token(struct token **tokens, int *size, int *capacity, struct token t)
{
    struct token *tmp;

    if (*size >= *capacity) {
        *capacity = (*capacity == 0) ? 128 : (*capacity * 2);
        tmp = realloc(*tokens, (*capacity) * sizeof(struct token));
        if (tmp == NULL) {
            free(*tokens);
            *tokens = NULL;
            *size = 0;
            *capacity = 0;
            return -1;
        }
        *tokens = tmp;
    }

    (*tokens)[*size] = t;
    (*size)++;
    return 0;
}

typedef int (*token_writer)(struct token token_value, void *writer_data);

/* Dados usados quando o writer deve armazenar tokens em um vetor dinamico. */
typedef struct {
    struct token **tokens; /* Vetor de tokens gerado. */
    int *size;             /* Quantidade atual de tokens. */
    int *capacity;         /* Capacidade alocada do vetor. */
} TokenVectorWriter;

/* Dados usados quando o writer deve gravar tokens direto em bitFILE. */
typedef struct {
    struct bitFILE *output_file; /* Arquivo de destino em bits. */
    int la_size;                 /* Lookahead para codificar len. */
    int sb_size;                 /* Search-buffer para codificar off. */
} TokenFileWriter;

/*
 * Funcao: write_token_to_vector
 * -----------------------------
 * Callback usado por compress_block_with_writer para adicionar um token ao
 * vetor dinamico.
 */
static int write_token_to_vector(struct token token_value, void *writer_data)
{
    TokenVectorWriter *vector_writer = (TokenVectorWriter *) writer_data; /* Estado do vetor. */

    return append_token(
        vector_writer->tokens,
        vector_writer->size,
        vector_writer->capacity,
        token_value
    );
}

/*
 * Funcao: write_token_to_file
 * ---------------------------
 * Callback usado por compress_block_with_writer para gravar o token direto no
 * arquivo temporario de uma thread.
 */
static int write_token_to_file(struct token token_value, void *writer_data)
{
    TokenFileWriter *file_writer = (TokenFileWriter *) writer_data; /* Estado do arquivo. */

    writecode(
        token_value,
        file_writer->output_file,
        file_writer->la_size,
        file_writer->sb_size
    );

    return 0;
}

/***************************************************************************
 *                    COMPRESS BLOCK WITH WRITER FUNCTION
 * Compresses a memory block and sends each token to the provided writer.
 *
 * This version is intentionally independent from the global sliding window.
 * Each block uses only bytes inside [start, end), which makes the block safe
 * to be compressed by a different thread.
 ***************************************************************************/
static int compress_block_with_writer(
    unsigned char *input,
    int start,
    int end,
    int la,
    int sb,
    token_writer writer,
    void *writer_data,
    int *output_size
)
{
    int LA_SIZE = (la == -1) ? DEFAULT_LA_SIZE : la; /* Lookahead efetivo. */
    int SB_SIZE = (sb == -1) ? DEFAULT_SB_SIZE : sb; /* Search-buffer efetivo. */
    int WINDOW_SIZE;              /* Tamanho total da janela local. */
    int root = -1;                /* Raiz da arvore local do bloco. */
    int eof;                      /* Indica fim do bloco de entrada. */
    int i;                        /* Percorre bytes consumidos por token. */
    int source_position;          /* Posicao absoluta no buffer de entrada. */
    int bytes_to_copy;            /* Bytes copiados para a janela. */
    int buff_size;                /* Bytes validos ainda na janela. */
    int sb_size = 0;              /* Tamanho atual do search-buffer local. */
    int sb_index = 0;             /* Inicio atual do search-buffer. */
    int la_index = 0;             /* Inicio atual do lookahead. */
    int la_size;                  /* Tamanho atual do lookahead. */
    struct node *tree = NULL;     /* Arvore usada para buscar matches. */
    struct token t;               /* Token gerado na iteracao atual. */
    unsigned char *window = NULL; /* Janela local do bloco. */

    *output_size = 0;

    if (input == NULL || writer == NULL || start < 0 || end <= start) {
        return 0;
    }

    if (LA_SIZE < 1) {
        LA_SIZE = DEFAULT_LA_SIZE;
    }

    if (SB_SIZE < 1) {
        SB_SIZE = DEFAULT_SB_SIZE;
    }

    WINDOW_SIZE = (SB_SIZE * N) + LA_SIZE;
    window = calloc((size_t) WINDOW_SIZE, sizeof(unsigned char));
    tree = createTree(SB_SIZE);
    if (window == NULL || tree == NULL) {
        free(window);
        destroyTree(tree);
        return -1;
    }

    source_position = start;
    bytes_to_copy = end - source_position;
    if (bytes_to_copy > WINDOW_SIZE) {
        bytes_to_copy = WINDOW_SIZE;
    }

    memcpy(window, &input[source_position], (size_t) bytes_to_copy);
    source_position += bytes_to_copy;
    buff_size = bytes_to_copy;
    eof = (source_position >= end);
    la_size = (buff_size > LA_SIZE) ? LA_SIZE : buff_size;

    while (buff_size > 0) {
        t = match(tree, root, window, la_index, la_size);
        if (writer(t, writer_data) != 0) {
            destroyTree(tree);
            free(window);
            return -1;
        }
        (*output_size)++;

        for (i = 0; i < t.len + 1; i++) {
            if (sb_size == SB_SIZE) {
                delete(tree, &root, window, sb_index, SB_SIZE);
                sb_index++;
            } else {
                sb_size++;
            }

            insert(tree, &root, window, la_index, la_size, SB_SIZE);
            la_index++;

            if (eof == 0 && sb_index == SB_SIZE * (N - 1)) {
                memmove(window, &window[sb_index], (size_t) (sb_size + la_size));
                updateOffset(tree, sb_index, SB_SIZE);

                sb_index = 0;
                la_index = sb_size;

                bytes_to_copy = WINDOW_SIZE - (sb_size + la_size);
                if (bytes_to_copy > end - source_position) {
                    bytes_to_copy = end - source_position;
                }

                if (bytes_to_copy > 0) {
                    memcpy(
                        &window[sb_size + la_size],
                        &input[source_position],
                        (size_t) bytes_to_copy
                    );
                    source_position += bytes_to_copy;
                    buff_size += bytes_to_copy;
                }

                eof = (source_position >= end);
            }

            buff_size--;
            la_size = (buff_size > LA_SIZE) ? LA_SIZE : buff_size;
        }
    }

    destroyTree(tree);
    free(window);
    return 0;
}

/***************************************************************************
 *                         COMPRESS BLOCK FUNCTION
 * Compresses a memory block into LZ77 tokens.
 ***************************************************************************/
void compress_block(
    unsigned char *input,
    int start,
    int end,
    int la,
    int sb,
    struct token **output,
    int *output_size
)
{
    int capacity = 0;              /* Capacidade alocada para o vetor. */
    int vector_size = 0;           /* Tamanho real usado pelo writer. */
    TokenVectorWriter vector_writer; /* Estado passado ao callback de vetor. */

    *output = NULL;
    *output_size = 0;

    vector_writer.tokens = output;
    vector_writer.size = &vector_size;
    vector_writer.capacity = &capacity;

    if (compress_block_with_writer(
            input,
            start,
            end,
            la,
            sb,
            write_token_to_vector,
            &vector_writer,
            output_size
        ) != 0) {
        free(*output);
        *output = NULL;
        *output_size = 0;
    }
}

/***************************************************************************
 *                      COMPRESS BLOCK TO FILE FUNCTION
 * Compresses a memory block and writes its tokens directly to a bit file.
 ***************************************************************************/
int compress_block_to_file(
    unsigned char *input,
    int start,
    int end,
    int la,
    int sb,
    const char *output_path,
    int *output_size
)
{
    struct bitFILE *output_file; /* Arquivo temporario de tokens. */
    TokenFileWriter file_writer; /* Estado passado ao callback de arquivo. */
    int LA_SIZE = (la == -1) ? DEFAULT_LA_SIZE : la; /* Lookahead efetivo. */
    int SB_SIZE = (sb == -1) ? DEFAULT_SB_SIZE : sb; /* Search-buffer efetivo. */
    int status; /* Resultado da compressao do bloco. */

    if (output_path == NULL || output_size == NULL) {
        return -1;
    }

    if (LA_SIZE < 1) {
        LA_SIZE = DEFAULT_LA_SIZE;
    }

    if (SB_SIZE < 1) {
        SB_SIZE = DEFAULT_SB_SIZE;
    }

    output_file = bitIO_open(output_path, BIT_IO_W);
    if (output_file == NULL) {
        return -1;
    }

    file_writer.output_file = output_file;
    file_writer.la_size = LA_SIZE;
    file_writer.sb_size = SB_SIZE;

    status = compress_block_with_writer(
        input,
        start,
        end,
        la,
        sb,
        write_token_to_file,
        &file_writer,
        output_size
    );

    if (bitIO_close(output_file) != 0) {
        status = -1;
    }

    return status;
}

/***************************************************************************
 *                           WRITECODE FUNCTION
 * Name         : writecode - write the token in the output file
 * Parameters   : t - token to be written
 *                out - output file
 * SB_SIZE = n  =>  ceil(log(n)/log(2)) bits for Offset representation
 * LA_SIZE = m  =>  ceil(log(m)/log(2)) bits for Length representation
 * Alway 8 bits for Next char representation
 *
 * DEFAULT (LA_SIZE = 15, SB_SIZE = 4095):
 * Offset : 12 bits representation => [0, 4095]
 * Length : 4 bits representation => [0, 15]
 * Next char requires 8 bits
 * Total token's size: 12 + 4 + 8 = 24 bits = 3 bytes
 *
 *     0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |         offset        |lenght |   next char   |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 ***************************************************************************/
void writecode(struct token t, struct bitFILE *out, int la_size, int sb_size)
{

    bitIO_write(out, &t.off, bitof(sb_size));
    bitIO_write(out, &t.len, bitof(la_size));
    bitIO_write(out, &t.next, 8);
}

/***************************************************************************
 *                          READCODE FUNCTION
 * Name         : readcode - read the token from the compressed file
 * Parameters   : file - compressed file
 * Returned     : t - reconstructed token
 ***************************************************************************/
struct token readcode(struct bitFILE *file, int la_size, int sb_size)
{
	/* variables */
	struct token t;
	int ret = 0;

	ret += bitIO_read(file, &t.off, sizeof(t.off), bitof(sb_size));
	ret += bitIO_read(file, &t.len, sizeof(t.len), bitof(la_size));
	ret += bitIO_read(file, &t.next, sizeof(t.next), 8);
		
	/* check for EOF or ERR */	
	if(ret < (bitof(sb_size) + bitof(la_size) + 8)){
		/* ERR */		
		if(bitIO_ferror(file) != 0)
		{
			perror("Error reading bits.\n");
			exit(EXIT_FAILURE);
		}
		/* EOF */
		t.off = -1;
	}

	return t;
}
