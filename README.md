# Compressão Paralela de Arquivos

Projeto implementando o algoritmo **LZ77** para compressão e descompressão de arquivos de forma paralela.

## Estrutura do Projeto

```
parallel-file-compression/
├── arquivos/                    # Pastas de trabalho (gitignored)
│   ├── originais/              # Arquivos originais a comprir
│   ├── comprimidos/            # Arquivos após compressão (.lz77)
│   └── descomprimidos/         # Arquivos após descompressão
├── lz77/                        # Código-fonte do algoritmo
│   ├── lz77.c / lz77.h         # Implementação do LZ77
│   ├── bitio.c / bitio.h       # Biblioteca para I/O de bits
│   ├── tree.c / tree.h         # Árvore binária para matching
│   ├── main.c                  # Programa principal
│   ├── Makefile                # Compilação
│   └── README.md               # Documentação técnica
└── README.md                    # Este arquivo
```

## Como Usar

### 1. Preparação Inicial

Primeiro, crie a estrutura de pastas necessária:

```bash
./setup-dirs.sh
```

Este script criará as seguintes pastas:
- `arquivos/originais/` - coloque seus arquivos aqui para comprir
- `arquivos/comprimidos/` - arquivos comprimidos serão salvos aqui
- `arquivos/descomprimidos/` - arquivos descomprimidos serão salvos aqui

### 2. Compilação

Entre na pasta `lz77` e compile o projeto:

```bash
cd lz77
make
```

Isso gerará o executável `lz77`.

### 3. Executar Compressão

Para comprir um arquivo, use:

```bash
./lz77 -c -i ../arquivos/originais/seu_arquivo.txt -o ../arquivos/comprimidos/seu_arquivo.lz77
```

**Exemplos práticos:**

```bash
# Comprimindo um arquivo de texto simples
./lz77 -c -i ../arquivos/originais/teste.txt -o ../arquivos/comprimidos/teste.lz77

# Comprimindo com tamanhos customizados de janela
# (lookahead = 31, searchbuffer = 8191)
./lz77 -c -i ../arquivos/originais/dados.txt -o ../arquivos/comprimidos/dados.lz77 -l 31 -s 8191
```

### 4. Executar Descompressão

Para descompir um arquivo:

```bash
./lz77 -d -i ../arquivos/comprimidos/seu_arquivo.lz77 -o ../arquivos/descomprimidos/seu_arquivo.txt
```

**Exemplo prático:**

```bash
./lz77 -d -i ../arquivos/comprimidos/teste.lz77 -o ../arquivos/descomprimidos/teste.txt
```

### 5. Opções do Programa

| Opção | Descrição |
|-------|-----------|
| `-c` | Modo compressão |
| `-d` | Modo descompressão |
| `-i <arquivo>` | Arquivo de entrada (obrigatório) |
| `-o <arquivo>` | Arquivo de saída (obrigatório) |
| `-l <valor>` | Tamanho do lookahead (padrão: 15) |
| `-s <valor>` | Tamanho do search buffer (padrão: 4095) |
| `-h` | Mostra ajuda |

## Exemplo Completo: Passo a Passo

Vamos comprir e depois descompir um arquivo chamado `exemplo.txt`:

```bash
# 1. Preparar o ambiente
./setup-dirs.sh

# 2. Copiar arquivo para ser comprimido
cp seu_arquivo.txt arquivos/originais/exemplo.txt

# 3. Entrar na pasta do projeto
cd lz77

# 4. Compilar
make

# 5. Comprimindo
./lz77 -c -i ../arquivos/originais/exemplo.txt -o ../arquivos/comprimidos/exemplo.lz77

# 6. Descomprimindo (para verificação)
./lz77 -d -i ../arquivos/comprimidos/exemplo.lz77 -o ../arquivos/descomprimidos/exemplo.txt

# 7. Verificar se os arquivos são idênticos
diff ../arquivos/originais/exemplo.txt ../arquivos/descomprimidos/exemplo.txt
# Se nada for exibido, os arquivos são idênticos ✓
```

## Dicas Úteis

- **Verificação de integridade**: Use `diff` para comparar arquivos originais com descomprimidos
- **Visualizar tamanhos**: Use `ls -lh` para comparar tamanhos de arquivo original vs comprimido
- **Limpar diretórios**: `rm -rf arquivos/comprimidos/* arquivos/descomprimidos/*` para limpar antes de novos testes

## Estrutura Detalhada da Pasta `arquivos/`

A pasta `arquivos/` é **ignorada pelo Git** (veja `.gitignore`), pois contém dados de teste que podem ser:
- Muito grandes
- Específicos de cada desenvolvimento local
- Regeneráveis pelo script `setup-dirs.sh`

Sempre recrie essa pasta em um novo clone do repositório usando:

```bash
./setup-dirs.sh
```

## Para Mais Informações

Consulte o [README técnico do LZ77](lz77/README.md) para detalhes sobre a implementação do algoritmo.