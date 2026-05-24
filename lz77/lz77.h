/***************************************************************************
 *          Lempel, Ziv Encoding and Decoding
 *
 *   File    : lz77.h
 *   Authors : David Costa and Pietro De Rosa
 *
 ***************************************************************************/
#ifndef lz77_h
#define lz77_h

#include <stdio.h>

struct bitFILE;

/* Marcador usado no cabecalho para indicar arquivo comprimido paralelo. */
#define LZ77_PARALLEL_MARKER 0

/* Assinatura curta "PL" usada para validar o novo cabecalho paralelo. */
#define LZ77_PARALLEL_MAGIC 0x504C

/***************************************************************************
 *                            TYPE DEFINITIONS
 * Each token is composed by a backward offset, the match's length and the
 * next character in the lookahead.
 * Offset : [0, SB_SIZE]            Length : [0, LA_SIZE]
 ***************************************************************************/
struct token{
    int off;   // Distância para trás onde começa a sequência repetida.
    int len;   // Quantidade de bytes copiados a partir do deslocamento.
    char next; // Próximo byte literal após a sequência copiada.
};

/*
 *  Estrutura que armazena os tempos medidos nas fases principais
 *  da compressão ou descompressão sequencial.
 */
typedef struct {
    double read_time;        // Tempo gasto lendo arquivo, cabeçalho, metadados ou tokens.
    double compression_time; // Tempo gasto comprimindo ou descomprimindo os dados.
    double write_time;       // Tempo gasto escrevendo a saída e fazendo flush final.
    double total_time;       // Tempo total medido dentro da função instrumentada.
} LZ77Timing;

/*
 *  Comprime um bloco em memória e devolve um vetor de tokens.
 *  A compressão considera apenas repetições dentro do próprio bloco.
 */
void compress_block(
    unsigned char *input,   // Ponteiro para o buffer completo de entrada.
    int start,              // Índice inicial do bloco que será comprimido.
    int end,                // Índice final exclusivo do bloco que será comprimido.
    int la,                 // Tamanho do lookahead; -1 usa o valor padrão.
    int sb,                 // Tamanho do search-buffer; -1 usa o valor padrão.
    struct token **output,  // Vetor onde serão armazenados os tokens gerados.
    int *output_size        // Quantidade de tokens gerados no vetor de saída.
);

/*
 *  Comprime um bloco em memória e grava os tokens diretamente em um arquivo.
 *  O arquivo gerado não recebe cabeçalho; ele contém apenas os tokens do bloco.
 */
int compress_block_to_file(
    unsigned char *input,    // Ponteiro para o buffer completo de entrada.
    int start,               // Índice inicial do bloco que será comprimido.
    int end,                 // Índice final exclusivo do bloco que será comprimido.
    int la,                  // Tamanho do lookahead; -1 usa o valor padrão.
    int sb,                  // Tamanho do search-buffer; -1 usa o valor padrão.
    const char *output_path, // Caminho do arquivo temporário comprimido.
    int *output_size         // Quantidade de tokens gravados no arquivo.
);

/*
 *  Grava um token no fluxo de bits conforme os tamanhos de janela usados.
 */
void writecode(
    struct token t,      // Token que será gravado.
    struct bitFILE *out, // Arquivo de saída com escrita em bits.
    int la_size,         // Tamanho do lookahead usado para codificar len.
    int sb_size          // Tamanho do search-buffer usado para codificar off.
);

/*
 *  Lê um token do fluxo comprimido. Quando encontra EOF, devolve off = -1.
 */
struct token readcode(
    struct bitFILE *file, // Arquivo comprimido com leitura em bits.
    int la_size,          // Tamanho do lookahead usado para ler len.
    int sb_size           // Tamanho do search-buffer usado para ler off.
);

/*
 *  Executa a compressão sequencial tradicional sem coletar estatísticas.
 */
void encode(
    FILE *file,         // Arquivo original aberto para leitura.
    struct bitFILE *out,// Arquivo comprimido aberto para escrita em bits.
    int la,             // Tamanho do lookahead; -1 usa o valor padrão.
    int sb              // Tamanho do search-buffer; -1 usa o valor padrão.
);

/*
 *  Executa a compressão sequencial e preenche tempos por fase.
 */
void encode_with_timing(
    FILE *file,          // Arquivo original aberto para leitura.
    struct bitFILE *out, // Arquivo comprimido aberto para escrita em bits.
    int la,              // Tamanho do lookahead; -1 usa o valor padrão.
    int sb,              // Tamanho do search-buffer; -1 usa o valor padrão.
    LZ77Timing *timing   // Estrutura que recebe os tempos medidos.
);

/*
 *  Executa a descompressão sequencial sem coletar estatísticas.
 */
void decode(
    struct bitFILE *file, // Arquivo comprimido aberto para leitura em bits.
    FILE *out             // Arquivo de saída descomprimido.
);

/*
 *  Executa a descompressão sequencial e coleta tempos por fase.
 */
void decode_with_timing(
    struct bitFILE *file, // Arquivo comprimido aberto para leitura em bits.
    FILE *out,            // Arquivo de saída descomprimido.
    LZ77Timing *timing    // Estrutura que recebe os tempos medidos.
);

/*
 *  Descomprime uma quantidade fixa de tokens a partir de uma posição em bits.
 *  Usada por cada thread da descompressão paralela.
 */
int decode_tokens_from_file(
    const char *input_path,  // Caminho do arquivo comprimido de entrada.
    const char *output_path, // Caminho do arquivo temporário descomprimido.
    long long start_bit,     // Posição inicial do bloco dentro do arquivo, em bits.
    int token_count,         // Quantidade de tokens que devem ser descomprimidos.
    int la_size,             // Tamanho do lookahead lido do cabeçalho.
    int sb_size              // Tamanho do search-buffer lido do cabeçalho.
);

#endif
