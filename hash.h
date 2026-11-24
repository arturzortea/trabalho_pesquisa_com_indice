/*
 * hash.h - Índice em MEMÓRIA com tabela hash sobre uma coluna que NÃO é a chave
 *          de ordenação (no projeto: product_id do arquivo de pessoas/eventos).
 *
 * FUNÇÃO HASH: método da divisão
 *     h(k) = k mod M,   M = número de baldes, sempre um número PRIMO
 *   (primo para espalhar bem chaves com padrões, como product_id que terminam em 0).
 *
 * VALORES REPETIDOS
 *   A coluna não é chave: o mesmo product_id aparece em muitos eventos. Cada valor
 *   distinto vira UM nó na tabela, e esse nó guarda a LISTA de endereços (posição do
 *   registro no .dat) de todos os registros que têm aquele valor.
 *
 * RESOLUÇÃO DE COLISÕES: encadeamento separado (separate chaining)
 *   Cada balde aponta para uma lista ligada de nós. Quando dois product_id diferentes
 *   caem no mesmo balde (colisão), o novo nó é colocado no INÍCIO da lista do balde.
 *   A busca calcula h(k) e percorre só a lista daquele balde, comparando as chaves.
 *
 * REDIMENSIONAMENTO (rehash)
 *   Fator de carga = valores distintos / M. Quando passa de TH_CARGA_MAX, a tabela
 *   cresce para o primeiro primo >= 2*M e todos os nós são redistribuídos
 *   (só os ponteiros mudam; as listas de endereços não são copiadas). Assim as
 *   listas dos baldes continuam curtas (em média até 1 nó).
 */
#ifndef HASH_H
#define HASH_H

#include <stddef.h>
#include <stdint.h>

#define TH_M_INICIAL   1009     /* primo */
#define TH_CARGA_MAX   1.0

typedef struct NoHash {
    uint64_t        chave;      /* valor da coluna (ex.: product_id)               */
    int64_t        *end;        /* endereços dos registros com esse valor           */
    int             n, cap;     /* quantidade usada / capacidade do vetor end       */
    struct NoHash  *prox;       /* próximo nó do mesmo balde (colisão)              */
} NoHash;

typedef struct TabelaHash {
    NoHash  **baldes;
    size_t    m;                /* nº de baldes (primo)                             */
    long      n_chaves;         /* valores distintos                                */
    long      n_enderecos;      /* registros indexados                              */
    long      colisoes;         /* inserções de valor novo em balde já ocupado      */
    long      rehashes;         /* quantas vezes a tabela cresceu                   */
} TabelaHash;

typedef struct {
    size_t m;
    long   vazios;              /* baldes sem nenhum nó                             */
    long   com_um;              /* baldes com exatamente 1 nó                       */
    long   com_colisao;         /* baldes com 2 ou mais nós                         */
    int    maior_cadeia;
    long   dist[6];             /* baldes com 0,1,2,3,4 e 5+ nós                    */
    double carga;               /* n_chaves / m                                     */
    double media_comp;          /* comparações médias numa busca bem-sucedida       */
    long   maior_lista;         /* maior nº de endereços para um mesmo valor        */
    uint64_t chave_maior_lista;
} THStats;

TabelaHash   *th_criar(size_t m_inicial);               /* NULL se faltar memória */
void          th_destruir(TabelaHash *h);
size_t        th_funcao(const TabelaHash *h, uint64_t chave);   /* h(k) = k mod M */

/* acrescenta o endereço à lista do valor 'chave'. 0 = ok, -1 = sem memória */
int           th_inserir(TabelaHash *h, uint64_t chave, int64_t endereco);

/* tira um endereço da lista do valor (usado quando um registro muda de valor) */
void          th_remover(TabelaHash *h, uint64_t chave, int64_t endereco);

/* devolve o nó do valor (NULL se não existe). *comparacoes = nós visitados no balde */
const NoHash *th_buscar(const TabelaHash *h, uint64_t chave, int *comparacoes);

void          th_estatisticas(const TabelaHash *h, THStats *s);

#endif
