/***************************************************************************
 *          Lempel, Ziv Encoding and Decoding
 *
 *   File    : lz77.h
 *   Authors : David Costa and Pietro De Rosa
 *
 ***************************************************************************/

/***************************************************************************
 *                         FUNCTIONS DECLARATION
 ***************************************************************************/
#ifndef lz77_h
#define lz77_h

struct bitFILE;

/***************************************************************************
 *                            TYPE DEFINITIONS
 * Each token is composed by a backward offset, the match's length and the
 * next character in the lookahead.
 * Offset : [0, SB_SIZE]            Length : [0, LA_SIZE]
 ***************************************************************************/
struct token{
    int off, len;
    char next;
};

/*
 * Struct do bloco que sera comprimido
 */
void compress_block(
    unsigned char *input,  // Ponteiro para o bloco de bytes a ser comprimido
    int start,             // Índice do primeiro byte do bloco
    int end,               // Índice do último byte do bloco
    struct token **output, // Vetor de tokens onde serao armazenados os tokens gerados a partir do bloco
    int *output_size       // Número de tokens gerados a partir do bloco
);

void encode(FILE *file, struct bitFILE *out, int la, int sb);
void decode(struct bitFILE *file, FILE *out);
#endif
