/*
 * arvoreb.h - Índice em MEMÓRIA com árvore B+ sobre a chave de ordenação do arquivo.
 *
 * ORDEM DA ÁRVORE (definição de Bayer & McCreight)
 *   Árvore de ordem d:
 *     - cada nó tem no MÁXIMO 2d chaves (2d elementos numa folha, 2d+1 filhos num nó interno);
 *     - cada nó, exceto a raiz, tem no MÍNIMO d chaves.
 *   Ordens disponíveis (opção do menu de produtos):
 *     ordem 1 -> até 2 registros por nó
 *     ordem 2 -> até 4 registros por nó
 *     ordem 3 -> até 6 registros por nó
 *
 * ÁRVORE B+
 *   - Nós internos guardam só chaves separadoras e ponteiros para os filhos.
 *   - Os ENDEREÇOS dos registros ficam apenas nas folhas: cada elemento da folha é
 *     (chave, endereço), onde endereço = número do registro no arquivo .dat
 *     (o deslocamento em bytes é (endereço + 1) * tamanho_do_registro, por causa do cabeçalho).
 *   - As folhas são ligadas em lista (campo prox), em ordem crescente de chave.
 *   - Separador: a chave i de um nó interno é a MENOR chave da subárvore filhos[i+1];
 *     chaves iguais ao separador ficam à direita.
 *   - Quando um nó fica com 2d+1 chaves ele é dividido em dois nós com d chaves
 *     (mais uma chave que sobe para o pai). Se a raiz divide, a árvore ganha um nível.
 *
 * Cada nó é alocado com um único malloc: cabeçalho (struct NoBP) seguido dos vetores
 * de chaves e de filhos/endereços, dimensionados pela ordem.
 */
#ifndef ARVOREB_H
#define ARVOREB_H

#include <stddef.h>
#include <stdint.h>

#define BP_ORDEM_MIN    1
#define BP_ORDEM_MAX    3
#define BP_ALTURA_MAX   64

typedef struct NoBP {
    int          folha;         /* 1 = folha, 0 = nó interno                     */
    int          n;             /* quantidade de chaves em uso                   */
    struct NoBP *prox;          /* folhas: próxima folha (lista ligada)          */
    uint64_t    *chaves;        /* aponta para dentro do próprio bloco do nó     */
    union {
        struct NoBP **filhos;   /* nó interno: n+1 filhos                        */
        int64_t      *end;      /* folha: endereço do registro de cada chave     */
    } p;
} NoBP;

typedef struct ArvoreBP {
    NoBP    *raiz;
    int      ordem;             /* d                                             */
    int      max_chaves;        /* 2d: máximo de chaves por nó                   */
    int      min_chaves;        /* d : mínimo de chaves por nó (exceto a raiz)   */
    size_t   tam_no;            /* bytes alocados para cada nó                   */
    int      altura;            /* nº de níveis (1 = só a raiz-folha)            */
    long     n_chaves;
    long     n_nos;
    long     n_folhas;
    long     divisoes;          /* quantas vezes um nó foi dividido (split)      */

    /* vetores temporários usados na divisão de nós (2d + 2 posições) */
    uint64_t *tmp_ch;
    NoBP    **tmp_filhos;
    int64_t  *tmp_end;

    /* nós reservados antes de cada inserção, para que uma divisão nunca
     * fique pela metade por falta de memória (altura + 1 nós bastam) */
    NoBP    *reserva[BP_ALTURA_MAX + 1];
    int      n_reserva;
} ArvoreBP;

/* Estatísticas por nível (nível 0 = raiz) */
typedef struct {
    int    niveis;
    long   nos[BP_ALTURA_MAX];
    long   chaves[BP_ALTURA_MAX];
} BPStats;

ArvoreBP *bp_criar(int ordem);                           /* NULL se ordem inválida ou sem memória */
void      bp_destruir(ArvoreBP *a);

/* 0 = inserida, 1 = chave já existia (endereço atualizado), -1 = sem memória */
int       bp_inserir(ArvoreBP *a, uint64_t chave, int64_t endereco);

/* 1 = achou (preenche *endereco), 0 = não achou. *nos_visitados recebe quantos nós foram lidos */
int       bp_buscar(const ArvoreBP *a, uint64_t chave, int64_t *endereco, int *nos_visitados);

void      bp_estatisticas(const ArvoreBP *a, BPStats *s);
int       bp_verificar(const ArvoreBP *a);               /* 1 = estrutura consistente */
void      bp_imprimir(const ArvoreBP *a, int max_nos_por_nivel, int max_chaves_por_no);

#endif
