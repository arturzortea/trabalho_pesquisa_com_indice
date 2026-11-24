/*
 * hash.c - Tabela hash em memória com encadeamento separado (ver hash.h).
 */
#include "hash.h"

#include <stdlib.h>
#include <string.h>

static int eh_primo(size_t n)
{
    if (n < 2) return 0;
    if (n % 2 == 0) return n == 2;
    for (size_t d = 3; d * d <= n; d += 2)
        if (n % d == 0) return 0;
    return 1;
}

static size_t proximo_primo(size_t n)
{
    while (!eh_primo(n)) n++;
    return n;
}

TabelaHash *th_criar(size_t m_inicial)
{
    TabelaHash *h = (TabelaHash *)calloc(1, sizeof *h);
    if (!h) return NULL;
    h->m = proximo_primo(m_inicial < 3 ? 3 : m_inicial);
    h->baldes = (NoHash **)calloc(h->m, sizeof(NoHash *));
    if (!h->baldes) { free(h); return NULL; }
    return h;
}

void th_destruir(TabelaHash *h)
{
    if (!h) return;
    for (size_t i = 0; i < h->m; i++) {
        NoHash *no = h->baldes[i];
        while (no) {
            NoHash *prox = no->prox;
            free(no->end);
            free(no);
            no = prox;
        }
    }
    free(h->baldes);
    free(h);
}

/* Método da divisão: h(k) = k mod M, com M primo */
size_t th_funcao(const TabelaHash *h, uint64_t chave)
{
    return (size_t)(chave % (uint64_t)h->m);
}

/* Cresce para o primeiro primo >= 2M e redistribui os nós (rehash) */
static int redimensionar(TabelaHash *h)
{
    size_t novo_m = proximo_primo(h->m * 2);
    NoHash **novos = (NoHash **)calloc(novo_m, sizeof(NoHash *));
    if (!novos) return -1;                      /* sem memória: continua com a tabela atual */

    for (size_t i = 0; i < h->m; i++) {
        NoHash *no = h->baldes[i];
        while (no) {
            NoHash *prox = no->prox;
            size_t b = (size_t)(no->chave % (uint64_t)novo_m);
            no->prox = novos[b];
            novos[b] = no;
            no = prox;
        }
    }
    free(h->baldes);
    h->baldes = novos;
    h->m = novo_m;
    h->rehashes++;
    return 0;
}

static NoHash *procurar(const TabelaHash *h, uint64_t chave, int *comp)
{
    NoHash *no = h->baldes[th_funcao(h, chave)];
    int c = 0;
    while (no) {
        c++;
        if (no->chave == chave) break;
        no = no->prox;
    }
    if (comp) *comp = c;
    return no;
}

int th_inserir(TabelaHash *h, uint64_t chave, int64_t endereco)
{
    NoHash *no = procurar(h, chave, NULL);

    if (!no) {                                  /* valor novo: cria o nó no início do balde */
        size_t b;
        if ((double)(h->n_chaves + 1) / (double)h->m > TH_CARGA_MAX) redimensionar(h);
        no = (NoHash *)calloc(1, sizeof *no);
        if (!no) return -1;
        no->chave = chave;
        b = th_funcao(h, chave);
        if (h->baldes[b]) h->colisoes++;        /* balde ocupado: colisão, entra na cadeia */
        no->prox = h->baldes[b];
        h->baldes[b] = no;
        h->n_chaves++;
    }
    if (no->n == no->cap) {                     /* lista de endereços cheia: dobra */
        int nc = no->cap ? no->cap * 2 : 4;
        int64_t *v = (int64_t *)realloc(no->end, (size_t)nc * sizeof(int64_t));
        if (!v) return -1;
        no->end = v;
        no->cap = nc;
    }
    no->end[no->n++] = endereco;
    h->n_enderecos++;
    return 0;
}

void th_remover(TabelaHash *h, uint64_t chave, int64_t endereco)
{
    size_t b = th_funcao(h, chave);
    NoHash **pp = &h->baldes[b], *no;

    while ((no = *pp) && no->chave != chave) pp = &no->prox;
    if (!no) return;
    for (int i = 0; i < no->n; i++)
        if (no->end[i] == endereco) {
            no->end[i] = no->end[--no->n];      /* a ordem da lista não importa */
            h->n_enderecos--;
            break;
        }
    if (no->n == 0) {                           /* valor sem registros: tira o nó da cadeia */
        *pp = no->prox;
        free(no->end);
        free(no);
        h->n_chaves--;
    }
}

const NoHash *th_buscar(const TabelaHash *h, uint64_t chave, int *comparacoes)
{
    return procurar(h, chave, comparacoes);
}

void th_estatisticas(const TabelaHash *h, THStats *s)
{
    double soma_comp = 0;
    memset(s, 0, sizeof *s);
    s->m = h->m;
    s->carga = (double)h->n_chaves / (double)h->m;
    for (size_t i = 0; i < h->m; i++) {
        int L = 0;
        for (const NoHash *no = h->baldes[i]; no; no = no->prox) {
            L++;
            soma_comp += L;                     /* o L-ésimo nó da cadeia custa L comparações */
            if (no->n > s->maior_lista) { s->maior_lista = no->n; s->chave_maior_lista = no->chave; }
        }
        if (L == 0) s->vazios++;
        else if (L == 1) s->com_um++;
        else s->com_colisao++;
        if (L > s->maior_cadeia) s->maior_cadeia = L;
        s->dist[L < 5 ? L : 5]++;
    }
    s->media_comp = h->n_chaves ? soma_comp / (double)h->n_chaves : 0;
}
