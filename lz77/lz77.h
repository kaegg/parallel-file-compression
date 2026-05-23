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
 * Comprime um bloco em memoria e devolve um vetor de tokens.
 * A compressao considera apenas repeticoes dentro do proprio bloco.
 * Isso permite que blocos diferentes sejam comprimidos em paralelo.
 */
void compress_block(
    unsigned char *input,
    int start,
    int end,
    int la,
    int sb,
    struct token **output,
    int *output_size
);

void writecode(struct token t, struct bitFILE *out, int la_size, int sb_size);
void encode(FILE *file, struct bitFILE *out, int la, int sb);
void decode(struct bitFILE *file, FILE *out);

#endif
