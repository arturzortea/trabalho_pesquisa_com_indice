/*
 * arvoreb.c - Árvore B+ em memória (ver arvoreb.h).
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(_WIN32) && !defined(__USE_MINGW_ANSI_STDIO)
#define __USE_MINGW_ANSI_STDIO 1
#endif

#include "arvoreb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RES_ERRO = -1, RES_OK = 0, RES_DUP = 1, RES_DIVIDIU = 2 };

/* cabeçalho do nó arredondado para múltiplo de 8 (alinhamento das chaves de 8 bytes) */
#define CAB_NO ((sizeof(NoBP) + 7u) & ~(size_t)7u)

/* ======================================================================
 *  Nós
 * ====================================================================== */

/* Organiza um bloco cru como nó: cabeçalho + vetor de chaves + vetor de filhos/endereços */
static NoBP *formatar(const ArvoreBP *a, NoBP *no, int folha)
{
    char *base = (char *)no + CAB_NO;
    memset(no, 0, a->tam_no);
    no->folha = folha;
    no->chaves = (uint64_t *)base;
    if (folha) no->p.end    = (int64_t *)(base + (size_t)a->max_chaves * sizeof(uint64_t));
    else       no->p.filhos = (NoBP **)(base + (size_t)a->max_chaves * sizeof(uint64_t));
    return no;
}

/* Pega um nó da reserva (sempre existe: garantida por reservar()) */
static NoBP *novo_no(ArvoreBP *a, int folha)
{
    NoBP *no = a->reserva[--a->n_reserva];
    a->n_nos++;
    if (folha) a->n_folhas++;
    return formatar(a, no, folha);
}

static int reservar(ArvoreBP *a)
{
    int precisa = a->altura + 1;
    if (precisa > BP_ALTURA_MAX + 1) return -1;
    while (a->n_reserva < precisa) {
        NoBP *p = (NoBP *)malloc(a->tam_no);
        if (!p) return -1;
        a->reserva[a->n_reserva++] = p;
    }
    return 0;
}

/* primeira posição com chaves[i] >= k */
static int menor_maior_igual(const uint64_t *v, int n, uint64_t k)
{
    int lo = 0, hi = n;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (v[m] < k) lo = m + 1; else hi = m;
    }
    return lo;
}

/* primeira posição com chaves[i] > k  (= índice do filho que contém k) */
static int menor_maior(const uint64_t *v, int n, uint64_t k)
{
    int lo = 0, hi = n;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (v[m] <= k) lo = m + 1; else hi = m;
    }
    return lo;
}

/* ======================================================================
 *  Criação / destruição
 * ====================================================================== */

ArvoreBP *bp_criar(int ordem)
{
    ArvoreBP *a;

    if (ordem < BP_ORDEM_MIN || ordem > BP_ORDEM_MAX) return NULL;
    a = (ArvoreBP *)calloc(1, sizeof *a);
    if (!a) return NULL;

    /* ordem d: no máximo 2d chaves por nó e, exceto na raiz, no mínimo d */
    a->ordem = ordem;
    a->max_chaves = 2 * ordem;
    a->min_chaves = ordem;

    /* bloco do nó: cabeçalho + 2d chaves + (2d+1) ponteiros (o que for maior: filhos ou endereços) */
    a->tam_no = CAB_NO + (size_t)a->max_chaves * sizeof(uint64_t)
              + (size_t)(a->max_chaves + 1) * (sizeof(NoBP *) > sizeof(int64_t) ? sizeof(NoBP *) : sizeof(int64_t));

    a->tmp_ch     = (uint64_t *)malloc((size_t)(a->max_chaves + 2) * sizeof(uint64_t));
    a->tmp_filhos = (NoBP **)malloc((size_t)(a->max_chaves + 2) * sizeof(NoBP *));
    a->tmp_end    = (int64_t *)malloc((size_t)(a->max_chaves + 2) * sizeof(int64_t));
    a->altura = 0;
    if (!a->tmp_ch || !a->tmp_filhos || !a->tmp_end || reservar(a) != 0) { bp_destruir(a); return NULL; }

    a->raiz = novo_no(a, 1);                    /* árvore vazia: raiz é uma folha sem chaves */
    a->altura = 1;
    return a;
}

static void liberar_no(NoBP *no)
{
    if (!no) return;
    if (!no->folha)
        for (int i = 0; i <= no->n; i++) liberar_no(no->p.filhos[i]);
    free(no);
}

void bp_destruir(ArvoreBP *a)
{
    if (!a) return;
    liberar_no(a->raiz);
    while (a->n_reserva > 0) free(a->reserva[--a->n_reserva]);
    free(a->tmp_ch);
    free(a->tmp_filhos);
    free(a->tmp_end);
    free(a);
}

/* ======================================================================
 *  Inserção (recursiva, com divisão de nós de baixo para cima)
 * ====================================================================== */

static int inserir_folha(ArvoreBP *a, NoBP *no, uint64_t k, int64_t e, uint64_t *k_sobe, NoBP **n_sobe)
{
    int pos = menor_maior_igual(no->chaves, no->n, k);
    int total, esq;
    NoBP *dir;

    if (pos < no->n && no->chaves[pos] == k) {      /* chave repetida: só atualiza o endereço */
        no->p.end[pos] = e;
        return RES_DUP;
    }
    if (no->n < a->max_chaves) {                     /* cabe na folha */
        memmove(no->chaves + pos + 1, no->chaves + pos, (size_t)(no->n - pos) * sizeof(uint64_t));
        memmove(no->p.end + pos + 1, no->p.end + pos, (size_t)(no->n - pos) * sizeof(int64_t));
        no->chaves[pos] = k;
        no->p.end[pos] = e;
        no->n++;
        return RES_OK;
    }

    /* folha cheia (2d): monta a sequência de 2d+1 elementos e divide: d+1 ficam, d vão para a nova */
    total = no->n + 1;
    memcpy(a->tmp_ch, no->chaves, (size_t)pos * sizeof(uint64_t));
    memcpy(a->tmp_end, no->p.end, (size_t)pos * sizeof(int64_t));
    a->tmp_ch[pos] = k;
    a->tmp_end[pos] = e;
    memcpy(a->tmp_ch + pos + 1, no->chaves + pos, (size_t)(no->n - pos) * sizeof(uint64_t));
    memcpy(a->tmp_end + pos + 1, no->p.end + pos, (size_t)(no->n - pos) * sizeof(int64_t));

    esq = (total + 1) / 2;
    dir = novo_no(a, 1);
    memcpy(no->chaves, a->tmp_ch, (size_t)esq * sizeof(uint64_t));
    memcpy(no->p.end, a->tmp_end, (size_t)esq * sizeof(int64_t));
    no->n = esq;
    memcpy(dir->chaves, a->tmp_ch + esq, (size_t)(total - esq) * sizeof(uint64_t));
    memcpy(dir->p.end, a->tmp_end + esq, (size_t)(total - esq) * sizeof(int64_t));
    dir->n = total - esq;

    dir->prox = no->prox;                           /* mantém a lista ligada de folhas */
    no->prox = dir;
    *k_sobe = dir->chaves[0];                       /* B+: a chave é COPIADA para cima */
    *n_sobe = dir;
    a->divisoes++;
    return RES_DIVIDIU;
}

static int inserir_rec(ArvoreBP *a, NoBP *no, uint64_t k, int64_t e, uint64_t *k_sobe, NoBP **n_sobe)
{
    int i, r, total, meio;
    uint64_t kf;
    NoBP *nf, *dir;

    if (no->folha) return inserir_folha(a, no, k, e, k_sobe, n_sobe);

    i = menor_maior(no->chaves, no->n, k);
    r = inserir_rec(a, no->p.filhos[i], k, e, &kf, &nf);
    if (r != RES_DIVIDIU) return r;

    /* o filho i foi dividido: entra a chave kf com o novo filho nf à direita dela */
    if (no->n < a->max_chaves) {
        memmove(no->chaves + i + 1, no->chaves + i, (size_t)(no->n - i) * sizeof(uint64_t));
        memmove(no->p.filhos + i + 2, no->p.filhos + i + 1, (size_t)(no->n - i) * sizeof(NoBP *));
        no->chaves[i] = kf;
        no->p.filhos[i + 1] = nf;
        no->n++;
        return RES_OK;
    }

    /* nó interno cheio: 2d+1 chaves e 2d+2 filhos -> d chaves ficam, a do meio SOBE, d vão para o novo nó */
    total = no->n + 1;
    memcpy(a->tmp_ch, no->chaves, (size_t)i * sizeof(uint64_t));
    a->tmp_ch[i] = kf;
    memcpy(a->tmp_ch + i + 1, no->chaves + i, (size_t)(no->n - i) * sizeof(uint64_t));
    memcpy(a->tmp_filhos, no->p.filhos, (size_t)(i + 1) * sizeof(NoBP *));
    a->tmp_filhos[i + 1] = nf;
    memcpy(a->tmp_filhos + i + 2, no->p.filhos + i + 1, (size_t)(no->n - i) * sizeof(NoBP *));

    meio = total / 2;
    dir = novo_no(a, 0);
    memcpy(no->chaves, a->tmp_ch, (size_t)meio * sizeof(uint64_t));
    memcpy(no->p.filhos, a->tmp_filhos, (size_t)(meio + 1) * sizeof(NoBP *));
    no->n = meio;
    memcpy(dir->chaves, a->tmp_ch + meio + 1, (size_t)(total - meio - 1) * sizeof(uint64_t));
    memcpy(dir->p.filhos, a->tmp_filhos + meio + 1, (size_t)(total - meio) * sizeof(NoBP *));
    dir->n = total - meio - 1;

    *k_sobe = a->tmp_ch[meio];
    *n_sobe = dir;
    a->divisoes++;
    return RES_DIVIDIU;
}

int bp_inserir(ArvoreBP *a, uint64_t chave, int64_t endereco)
{
    uint64_t ks;
    NoBP *ns;
    int r;

    if (reservar(a) != 0) return -1;            /* nós para o pior caso já separados */
    r = inserir_rec(a, a->raiz, chave, endereco, &ks, &ns);
    if (r == RES_DUP) return 1;
    if (r == RES_DIVIDIU) {                       /* a raiz dividiu: árvore cresce um nível */
        NoBP *nova = novo_no(a, 0);
        nova->n = 1;
        nova->chaves[0] = ks;
        nova->p.filhos[0] = a->raiz;
        nova->p.filhos[1] = ns;
        a->raiz = nova;
        a->altura++;
    }
    a->n_chaves++;
    return 0;
}

/* ======================================================================
 *  Busca
 * ====================================================================== */

static const NoBP *descer(const ArvoreBP *a, uint64_t chave, int *visitados)
{
    const NoBP *no = a->raiz;
    int v = 1;
    while (!no->folha) {
        no = no->p.filhos[menor_maior(no->chaves, no->n, chave)];
        v++;
    }
    if (visitados) *visitados = v;
    return no;
}

int bp_buscar(const ArvoreBP *a, uint64_t chave, int64_t *endereco, int *nos_visitados)
{
    const NoBP *f = descer(a, chave, nos_visitados);
    int pos = menor_maior_igual(f->chaves, f->n, chave);
    if (pos < f->n && f->chaves[pos] == chave) {
        if (endereco) *endereco = f->p.end[pos];
        return 1;
    }
    return 0;
}

/* ======================================================================
 *  Estatísticas, verificação e impressão
 * ====================================================================== */

static void estat_rec(const NoBP *no, int nivel, BPStats *s)
{
    if (nivel >= BP_ALTURA_MAX) return;
    if (nivel + 1 > s->niveis) s->niveis = nivel + 1;
    s->nos[nivel]++;
    s->chaves[nivel] += no->n;
    if (!no->folha)
        for (int i = 0; i <= no->n; i++) estat_rec(no->p.filhos[i], nivel + 1, s);
}

void bp_estatisticas(const ArvoreBP *a, BPStats *s)
{
    memset(s, 0, sizeof *s);
    estat_rec(a->raiz, 0, s);
}

/* confere ordem das chaves, limites [lo, hi), ocupação mínima e profundidade igual das folhas */
static int verif_rec(const ArvoreBP *a, const NoBP *no, int nivel, int tem_lo, uint64_t lo,
                     int tem_hi, uint64_t hi, int *prof_folha, long *conta)
{
    if (no->n > a->max_chaves) return 0;
    if (no != a->raiz && no->n < a->min_chaves) return 0;
    for (int i = 0; i < no->n; i++) {
        if (i > 0 && no->chaves[i - 1] >= no->chaves[i]) return 0;
        if (tem_lo && no->chaves[i] < lo) return 0;
        if (tem_hi && no->chaves[i] >= hi) return 0;
    }
    if (no->folha) {
        if (*prof_folha < 0) *prof_folha = nivel;
        else if (*prof_folha != nivel) return 0;
        *conta += no->n;
        return 1;
    }
    for (int i = 0; i <= no->n; i++) {
        int  l = i > 0 ? 1 : tem_lo, h = i < no->n ? 1 : tem_hi;
        uint64_t vl = i > 0 ? no->chaves[i - 1] : lo, vh = i < no->n ? no->chaves[i] : hi;
        if (!verif_rec(a, no->p.filhos[i], nivel + 1, l, vl, h, vh, prof_folha, conta)) return 0;
    }
    return 1;
}

int bp_verificar(const ArvoreBP *a)
{
    int prof = -1;
    long conta = 0, pela_lista = 0;
    const NoBP *f;
    int tem_ult = 0;
    uint64_t ult = 0;

    if (!verif_rec(a, a->raiz, 0, 0, 0, 0, 0, &prof, &conta)) return 0;
    if (conta != a->n_chaves || prof + 1 != a->altura) return 0;

    f = descer(a, 0, NULL);                     /* folha mais à esquerda */
    for (; f; f = f->prox)
        for (int i = 0; i < f->n; i++) {
            if (tem_ult && f->chaves[i] <= ult) return 0;
            ult = f->chaves[i];
            tem_ult = 1;
            pela_lista++;
        }
    return pela_lista == a->n_chaves;
}

static void imprimir_no(const NoBP *no, int max_ch)
{
    int mostrar = no->n <= max_ch ? no->n : max_ch;
    printf("    [%d chave%s:", no->n, no->n == 1 ? "" : "s");
    for (int i = 0; i < mostrar; i++) {
        if (no->folha) printf(" %llu->%lld", (unsigned long long)no->chaves[i], (long long)no->p.end[i]);
        else           printf(" %llu", (unsigned long long)no->chaves[i]);
    }
    if (no->n > mostrar) {
        int u = no->n - 1;
        if (no->folha) printf(" ... %llu->%lld", (unsigned long long)no->chaves[u], (long long)no->p.end[u]);
        else           printf(" ... %llu", (unsigned long long)no->chaves[u]);
    }
    printf(" ]\n");
}

/* Impressão por níveis (busca em largura), mostrando os primeiros nós de cada nível */
void bp_imprimir(const ArvoreBP *a, int max_nos, int max_ch)
{
    const NoBP **fila = (const NoBP **)malloc((size_t)(a->n_nos + 1) * sizeof(NoBP *));
    long ini = 0, fimq = 0;
    int nivel = 0;

    if (!fila) { printf("Memória insuficiente para imprimir.\n"); return; }
    fila[fimq++] = a->raiz;
    while (ini < fimq) {
        long fim_nivel = fimq, no_nivel = fimq - ini, impressos = 0;
        printf("\nNível %d%s - %ld nó(s)%s\n", nivel, nivel == 0 ? " (raiz)" : "", no_nivel,
               fila[ini]->folha ? "  [FOLHAS: chave->endereço do registro]" : "  [internos: só chaves separadoras]");
        for (; ini < fim_nivel; ini++) {
            const NoBP *no = fila[ini];
            if (impressos < max_nos) { imprimir_no(no, max_ch); impressos++; }
            if (!no->folha)
                for (int i = 0; i <= no->n; i++) fila[fimq++] = no->p.filhos[i];
        }
        if (no_nivel > impressos) printf("    ... (mais %ld nó(s) neste nível)\n", no_nivel - impressos);
        nivel++;
    }
    free(fila);
}
