#!/bin/bash

# Script para criar a estrutura de pastas do projeto
# Uso: ./setup-dirs.sh

# Cores para output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Criando estrutura de pastas do projeto ===${NC}\n"

# Criar diretórios
mkdir -p arquivos/originais
mkdir -p arquivos/comprimidos
mkdir -p arquivos/descomprimidos

echo -e "${GREEN}✓ Pasta 'arquivos/originais' criada${NC}"
echo -e "${GREEN}✓ Pasta 'arquivos/comprimidos' criada${NC}"
echo -e "${GREEN}✓ Pasta 'arquivos/descomprimidos' criada${NC}"

echo -e "\n${GREEN}=== Estrutura de pastas criada com sucesso ===${NC}\n"

# Listar estrutura criada
echo "Estrutura de pastas:"
tree -L 2 arquivos 2>/dev/null || find arquivos -type d | sort
