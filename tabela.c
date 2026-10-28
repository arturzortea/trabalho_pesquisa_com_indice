#include "tabela.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Posicionamento 64 bits. No Windows usa fsetpos/fgetpos (padrão C; fpos_t é inteiro de 64 bits
 * no MSVCRT), que funcionam até em MinGW antigo sem _fseeki64/_ftelli64. */
#ifdef _WIN32
static int seek64(FILE *f, long long o, int w)
{
    if (w == SEEK_SET) { fpos_t p = (fpos_t)o; return fsetpos(f, &p); }
    return fseek(f, (long)o, w);                    /* só usado com SEEK_END e deslocamento 0 */
}
static long long tell64(FILE *f)
{
    fpos_t p;
    if (fgetpos(f, &p) != 0) return -1;
    return (long long)p;
}
#define SEEK64(f, o, w) seek64((f), (long long)(o), (w))
#define TELL64(f)       tell64(f)
#else
#define SEEK64(f, o, w) fseeko((f), (off_t)(o), (w))
#define TELL64(f)       ((long long)ftello(f))
#endif

#define MAGIA       "IDXPAR01"
#define MAGIA_DADOS "AEDBIN01"
#define LINHA_MAX   8192

/* ======================================================================
 *  Utilitários básicos
 * ====================================================================== */

void tabela_preparar(Tabela *t)
{
    int p = 0;
    for (int i = 0; i < t->n_campos; i++) {
        t->off[i] = p;
        t->tam[i] = t->campos[i].tipo == T_TXT ? t->campos[i].largura : TAM_NUM;
        p += t->tam[i];
    }
    t->off_del = p;
    p += 1;
    t->off_elo = p;
    p += TAM_ELO;
    t->tam_reg = p + 1;                 /* + '\n' */
    if (t->k < 2) t->k = 2;
}

char *tabela_novo_buf(const Tabela *t)
{
    return (char *)calloc((size_t)t->tam_reg + 1, 1);
}

static void loc_init(Loc *L)
{
    memset(L, 0, sizeof *L);
    L->pos = -1;
    L->pred = -1;
    L->ant_ext = -1;
}

int tabela_ler_reg(Tabela *t, int64_t pos, char *buf)
{
    if (pos < 0) return -1;
    if (SEEK64(t->f, (long long)(pos + 1) * t->tam_reg, SEEK_SET) != 0) return -1;
    if (fread(buf, 1, (size_t)t->tam_reg, t->f) != (size_t)t->tam_reg) return -1;
    buf[t->tam_reg] = '\0';
    return 0;
}

static int escrever_bytes(Tabela *t, int64_t pos, int off, const char *d, int n)
{
    if (SEEK64(t->f, (long long)(pos + 1) * t->tam_reg + off, SEEK_SET) != 0) return -1;
    if (fwrite(d, 1, (size_t)n, t->f) != (size_t)n) return -1;
    fflush(t->f);
    return 0;
}

static int escrever_reg(Tabela *t, int64_t pos, const char *buf)
{
    return escrever_bytes(t, pos, 0, buf, t->tam_reg);
}

static int set_elo(Tabela *t, int64_t pos, int64_t valor)
{
    int64_t v = valor;
    return escrever_bytes(t, pos, t->off_elo, (const char *)&v, TAM_ELO);
}

static int set_delete(Tabela *t, int64_t pos, int valor)
{
    char c = (char)valor;
    return escrever_bytes(t, pos, t->off_del, &c, 1);
}

/* Converte o campo i do registro (binário) para texto, para exibir ou comparar */
void tabela_campo(const Tabela *t, const char *reg, int i, char *out)
{
    const char *p = reg + t->off[i];
    if (t->campos[i].tipo == T_NUM) {
        uint64_t v;
        memcpy(&v, p, TAM_NUM);
        snprintf(out, 32, "%llu", (unsigned long long)v);
    } else if (t->campos[i].tipo == T_DEC) {
        double d;
        memcpy(&d, p, TAM_NUM);
        snprintf(out, 32, "%.2f", d);
    } else {
        int n = t->campos[i].largura;
        memcpy(out, p, (size_t)n);
        while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\0')) n--;   /* tira o preenchimento */
        out[n] = '\0';
    }
}

uint64_t tabela_chave(const Tabela *t, const char *reg)
{
    uint64_t k;
    memcpy(&k, reg + t->off[t->idx_chave], TAM_NUM);
    return k;
}

int tabela_removido(const Tabela *t, const char *reg)
{
    return reg[t->off_del] == 1;
}

int64_t tabela_elo(const Tabela *t, const char *reg)
{
    int64_t e;
    memcpy(&e, reg + t->off_elo, TAM_ELO);
    return e;
}

/* ======================================================================
 *  Cabeçalho do arquivo de dados (registro 0)
 * ====================================================================== */

static void cab_montar(const Tabela *t, char *b, int64_t np, int64_t ne, int64_t ce)
{
    memset(b, 0, (size_t)t->tam_reg);
    memcpy(b, &np, TAM_CAB);
    memcpy(b + TAM_CAB, &ne, TAM_CAB);
    memcpy(b + 2 * TAM_CAB, &ce, TAM_CAB);
    memcpy(b + 3 * TAM_CAB, MAGIA_DADOS, 8);
    b[t->tam_reg - 1] = '\n';
    b[t->tam_reg] = '\0';
}

static int cab_escrever(Tabela *t)
{
    char *b = tabela_novo_buf(t);
    if (!b) return -1;
    cab_montar(t, b, t->n_princ, t->n_ext, t->cab_elo);
    int r = -1;
    if (SEEK64(t->f, 0, SEEK_SET) == 0 &&
        fwrite(b, 1, (size_t)t->tam_reg, t->f) == (size_t)t->tam_reg) {
        fflush(t->f);
        r = 0;
    }
    free(b);
    return r;
}

static int cab_ler(Tabela *t)
{
    char *b = tabela_novo_buf(t);
    if (!b) return -1;
    if (SEEK64(t->f, 0, SEEK_SET) != 0 ||
        fread(b, 1, (size_t)t->tam_reg, t->f) != (size_t)t->tam_reg) {
        free(b);
        return -1;
    }
    if (memcmp(b + 3 * TAM_CAB, MAGIA_DADOS, 8) != 0) { free(b); return -1; }   /* não é deste formato */
    memcpy(&t->n_princ, b, TAM_CAB);
    memcpy(&t->n_ext, b + TAM_CAB, TAM_CAB);
    memcpy(&t->cab_elo, b + 2 * TAM_CAB, TAM_CAB);
    free(b);
    return 0;
}

/* ======================================================================
 *  Validação do que o usuário digita e montagem do registro binário
 * ====================================================================== */

static int valida_campo(Tabela *t, int i, const char *v)
{
    const Campo *c = &t->campos[i];
    int len = (int)strlen(v);
    int pontos = 0, digitos = 0;

    if (len > c->largura) {
        snprintf(t->erro, sizeof t->erro,
                 "valor muito grande para o campo '%s': máximo de %d caracteres (você digitou %d)", c->nome, c->largura, len);
        return -1;
    }
    if (i == t->idx_chave && len == 0) {
        snprintf(t->erro, sizeof t->erro, "a chave '%s' não pode ser vazia", c->nome);
        return -1;
    }
    for (int k = 0; k < len; k++) {
        unsigned char ch = (unsigned char)v[k];
        if (c->tipo == T_NUM && !isdigit(ch)) {
            snprintf(t->erro, sizeof t->erro, "o campo '%s' deve conter apenas dígitos (valor: '%s')", c->nome, v);
            return -1;
        }
        if (c->tipo == T_DEC) {
            if (isdigit(ch)) digitos++;
            else if (ch == '.' && ++pontos == 1) { /* ok */ }
            else {
                snprintf(t->erro, sizeof t->erro, "o campo '%s' deve ser um número decimal, como 12.50 (valor: '%s')", c->nome, v);
                return -1;
            }
        }
        if (c->tipo == T_TXT && (ch == ';' || ch == '\n' || ch == '\r')) {
            snprintf(t->erro, sizeof t->erro, "o campo '%s' contém caractere inválido", c->nome);
            return -1;
        }
    }
    if (c->tipo == T_DEC && len > 0 && digitos == 0) {
        snprintf(t->erro, sizeof t->erro, "o campo '%s' deve ser um número decimal, como 12.50 (valor: '%s')", c->nome, v);
        return -1;
    }
    return 0;
}

int tabela_validar_campo(Tabela *t, int i, const char *v)
{
    return valida_campo(t, i, v);
}

/* Monta em 'buf' o registro binário a partir dos textos digitados/lidos do CSV */
static int montar_registro(Tabela *t, const char *const vals[], int del, int64_t elo, char *buf)
{
    memset(buf, 0, (size_t)t->tam_reg + 1);
    for (int i = 0; i < t->n_campos; i++) {
        const char *v = vals[i];
        int len;
        if (valida_campo(t, i, v) != 0) return -1;
        len = (int)strlen(v);
        if (t->campos[i].tipo == T_NUM) {
            uint64_t n = len ? (uint64_t)strtoull(v, NULL, 10) : 0;
            memcpy(buf + t->off[i], &n, TAM_NUM);
        } else if (t->campos[i].tipo == T_DEC) {
            double d = len ? strtod(v, NULL) : 0.0;
            memcpy(buf + t->off[i], &d, TAM_NUM);
        } else {
            memset(buf + t->off[i], ' ', (size_t)t->tam[i]);    /* espaços no final */
            memcpy(buf + t->off[i], v, (size_t)len);
        }
    }
    buf[t->off_del] = (char)del;
    {
        int64_t e = elo;
        memcpy(buf + t->off_elo, &e, TAM_ELO);
    }
    buf[t->tam_reg - 1] = '\n';
    buf[t->tam_reg] = '\0';
    return 0;
}

/* ======================================================================
 *  Índice parcial em vários níveis (arquivo binário)
 *
 *   [CabIdx][nível 1][nível 2]...[nível do topo]        entradas de 16 bytes: (chave, pos)
 *
 *   nível 1 : 1 entrada a cada K registros da área principal -> (chave, posição do registro)
 *   nível 2 : 1 entrada a cada K entradas do nível 1         -> (chave, nº da entrada no nível 1)
 *   nível N : sempre 1 entrada a cada K entradas do nível anterior
 *
 *   Níveis são acrescentados até o topo caber num bloco (<= K entradas). Cada bloco
 *   (de entradas ou de registros) é lido com UMA leitura e pesquisado na memória,
 *   então uma consulta custa 1 leitura por nível + 1 leitura do bloco de dados.
 * ====================================================================== */

static int ler_entradas(Tabela *t, int nivel, uint64_t ini, uint64_t qtd, EntradaIdx *dst)
{
    long long off = (long long)t->idx.off[nivel] + (long long)ini * (long long)sizeof(EntradaIdx);
    if (qtd == 0) return 0;
    if (SEEK64(t->fi, off, SEEK_SET) != 0) return -1;
    return fread(dst, sizeof *dst, (size_t)qtd, t->fi) == (size_t)qtd ? 0 : -1;
}

/* índice (dentro de v) da maior chave <= 'chave'; -1 se todas forem maiores */
static int64_t ultimo_menor_igual(const EntradaIdx *v, int64_t n, uint64_t chave)
{
    int64_t lo = 0, hi = n - 1, r = -1;
    while (lo <= hi) {
        int64_t mid = lo + (hi - lo) / 2;
        if (v[mid].chave <= chave) { r = mid; lo = mid + 1; } else hi = mid - 1;
    }
    return r;
}

int tabela_indice_construir(Tabela *t)
{
    CabIdx c;
    char *buf;
    EntradaIdx e;
    uint64_t k = t->k < 2 ? 2 : t->k;

    if (t->fi) { fclose(t->fi); t->fi = NULL; }
    t->idx_ok = 0;

    memset(&c, 0, sizeof c);
    memcpy(c.magia, MAGIA, 8);
    c.k = k;
    c.n_princ = (uint64_t)t->n_princ;
    if (c.n_princ > 0) {
        uint64_t n = (c.n_princ + k - 1) / k;
        c.n[0] = n;
        c.n_niveis = 1;
        while (n > k) {
            if (c.n_niveis >= MAX_NIVEIS) { snprintf(t->erro, sizeof t->erro, "arquivo grande demais para o índice"); return -1; }
            n = (n + k - 1) / k;
            c.n[c.n_niveis++] = n;
        }
    }
    c.off[0] = sizeof c;
    for (uint64_t i = 1; i < c.n_niveis; i++)
        c.off[i] = c.off[i - 1] + c.n[i - 1] * (uint64_t)sizeof(EntradaIdx);

    t->fi = fopen(t->arq_indice, "w+b");
    if (!t->fi) {
        snprintf(t->erro, sizeof t->erro, "não foi possível criar '%s'", t->arq_indice);
        return -1;
    }
    t->idx = c;
    if (fwrite(&c, sizeof c, 1, t->fi) != 1) return -1;

    buf = tabela_novo_buf(t);
    if (!buf) return -1;

    /* nível 1: lê só 1 registro a cada k (o arquivo de dados não vai para a memória) */
    for (uint64_t i = 0; i < c.n[0]; i++) {
        if (tabela_ler_reg(t, (int64_t)(i * k), buf) != 0) { free(buf); return -1; }
        e.chave = tabela_chave(t, buf);
        e.pos = (int64_t)(i * k);
        if (fwrite(&e, sizeof e, 1, t->fi) != 1) { free(buf); return -1; }
    }
    free(buf);

    /* níveis seguintes: 1 entrada a cada k entradas do nível de baixo */
    for (uint64_t nv = 1; nv < c.n_niveis; nv++) {
        for (uint64_t i = 0; i < c.n[nv]; i++) {
            EntradaIdx baixo;
            if (ler_entradas(t, (int)nv - 1, i * k, 1, &baixo) != 0) return -1;
            e.chave = baixo.chave;
            e.pos = (int64_t)(i * k);
            if (SEEK64(t->fi, (long long)c.off[nv] + (long long)i * (long long)sizeof e, SEEK_SET) != 0) return -1;
            if (fwrite(&e, sizeof e, 1, t->fi) != 1) return -1;
        }
    }
    fflush(t->fi);
    t->idx_ok = 1;
    return 0;
}

/* Carrega o índice gravado em disco; devolve 1 se está válido, 0 se precisa reconstruir */
static int indice_carregar(Tabela *t)
{
    CabIdx c;
    uint64_t total = 0;
    FILE *fi = fopen(t->arq_indice, "r+b");
    if (!fi) return 0;
    if (fread(&c, sizeof c, 1, fi) != 1 || memcmp(c.magia, MAGIA, 8) != 0 ||
        c.k != t->k || c.n_princ != (uint64_t)t->n_princ || c.n_niveis > MAX_NIVEIS ||
        (c.n_princ > 0 && (c.n_niveis == 0 || c.n[0] != (c.n_princ + c.k - 1) / c.k))) {
        fclose(fi);
        return 0;
    }
    for (uint64_t i = 0; i < c.n_niveis; i++) total += c.n[i];
    SEEK64(fi, 0, SEEK_END);
    if (TELL64(fi) != (long long)sizeof c + (long long)total * (long long)sizeof(EntradaIdx)) {
        fclose(fi);
        return 0;
    }
    t->fi = fi;
    t->idx = c;
    t->idx_ok = 1;
    return 1;
}

/* ======================================================================
 *  Abrir / fechar
 * ====================================================================== */

int tabela_abrir(Tabela *t)
{
    tabela_preparar(t);
    if (t->tam_reg < 4 * TAM_CAB + 1) {
        snprintf(t->erro, sizeof t->erro, "registro pequeno demais para o cabeçalho");
        return -1;
    }
    t->f = fopen(t->arq_dados, "r+b");
    if (t->f) {
        long long tam;
        SEEK64(t->f, 0, SEEK_END);
        tam = TELL64(t->f);
        if (tam > 0 && (cab_ler(t) != 0 || tam != (long long)(1 + t->n_princ + t->n_ext) * t->tam_reg)) {
            /* arquivo de outra versão do programa (ex.: em texto): guarda como .antigo e começa um novo */
            char antigo[300];
            fclose(t->f);
            t->f = NULL;
            snprintf(antigo, sizeof antigo, "%s.antigo", t->arq_dados);
            remove(antigo);
            if (rename(t->arq_dados, antigo) != 0) {
                snprintf(t->erro, sizeof t->erro, "'%s' está em formato diferente e não pôde ser renomeado", t->arq_dados);
                return -1;
            }
            printf("  [%s] '%s' estava em outro formato; foi renomeado para '%s'. Importe o CSV novamente.\n",
                   t->nome, t->arq_dados, antigo);
        } else if (tam == 0) {
            t->n_princ = t->n_ext = t->cab_elo = 0;
            cab_escrever(t);
        }
    }
    if (!t->f) {
        t->f = fopen(t->arq_dados, "w+b");
        if (!t->f) {
            snprintf(t->erro, sizeof t->erro, "não foi possível abrir/criar '%s'", t->arq_dados);
            return -1;
        }
        t->n_princ = t->n_ext = t->cab_elo = 0;
        cab_escrever(t);
    }

    if (!indice_carregar(t)) {
        printf("  [%s] índice ausente ou desatualizado: reconstruindo '%s'...\n", t->nome, t->arq_indice);
        if (tabela_indice_construir(t) != 0) return -1;
    }
    return 0;
}

void tabela_fechar(Tabela *t)
{
    /* o índice é gravado sempre que muda; aqui só garantimos que existe e está coerente */
    if (t->f && !t->idx_ok) tabela_indice_construir(t);
    if (t->fi) { fclose(t->fi); t->fi = NULL; }
    if (t->f)  { fclose(t->f);  t->f  = NULL; }
}

/* ======================================================================
 *  Buscas
 * ====================================================================== */

/* Percorre a cadeia de extensão a partir do registro 'pred' (ou do cabeçalho) */
static void seguir_cadeia(Tabela *t, uint64_t chave, Loc *L, char *buf)
{
    int64_t elo;
    int64_t guarda = t->n_ext + 1;

    if (L->pred >= 0) {
        if (tabela_ler_reg(t, L->pred, buf) != 0) return;
        elo = tabela_elo(t, buf);
    } else {
        elo = t->cab_elo;
    }
    L->ant_ext = -1;
    while (elo > 0 && guarda-- > 0) {
        int64_t p = elo - 1;
        uint64_t k;
        if (tabela_ler_reg(t, p, buf) != 0) break;
        L->cmp_ext++;
        k = tabela_chave(t, buf);
        if (k == chave) { L->achou = 1; L->pos = p; return; }
        if (k > chave) break;
        L->ant_ext = p;
        elo = tabela_elo(t, buf);
    }
    L->prox = elo;
}

/* 3) Pesquisa binária direto no arquivo de dados (área principal) + cadeia */
int tabela_busca_binaria(Tabela *t, uint64_t chave, Loc *L)
{
    int64_t lo = 0, hi = t->n_princ - 1;
    char *buf = tabela_novo_buf(t);
    if (!buf) return -1;
    loc_init(L);

    while (lo <= hi) {
        int64_t mid = lo + (hi - lo) / 2;
        uint64_t k;
        if (tabela_ler_reg(t, mid, buf) != 0) { free(buf); return -1; }
        L->cmp_dados++;
        k = tabela_chave(t, buf);
        if (k == chave) { L->achou = 1; L->pos = mid; free(buf); return 0; }
        if (k < chave) lo = mid + 1; else hi = mid - 1;
    }
    L->pred = hi;                      /* maior chave menor que a procurada */
    seguir_cadeia(t, chave, L, buf);
    free(buf);
    return 0;
}

/* 4) Consulta pelo índice parcial (vários níveis).
 *    Desce do topo até o nível 1, lendo 1 bloco de até k entradas por nível; depois lê
 *    1 bloco de até k registros do arquivo de dados e procura nele; se não achar, segue a
 *    cadeia de extensão. */
int tabela_busca_indice(Tabela *t, uint64_t chave, Loc *L)
{
    uint64_t k;
    int nivel;
    int64_t e = -1;
    uint64_t ini, qtd;
    EntradaIdx *blk = NULL;
    char *dblk = NULL, *buf = NULL;
    int rc = -1;

    if (!t->idx_ok) return tabela_busca_binaria(t, chave, L);
    k = t->idx.k;
    loc_init(L);
    blk = (EntradaIdx *)malloc((size_t)k * sizeof *blk);
    dblk = (char *)malloc((size_t)k * (size_t)t->tam_reg);
    buf = tabela_novo_buf(t);
    if (!blk || !dblk || !buf) goto fim;

    if (t->idx.n_niveis > 0) {
        nivel = (int)t->idx.n_niveis - 1;
        ini = 0;
        qtd = t->idx.n[nivel];                         /* o topo cabe inteiro num bloco */
        for (;;) {
            if (ler_entradas(t, nivel, ini, qtd, blk) != 0) goto fim;
            L->cmp_idx++;
            e = ultimo_menor_igual(blk, (int64_t)qtd, chave);
            if (e < 0 || nivel == 0) break;            /* menor que todas, ou chegou ao nível 1 */
            ini = (uint64_t)blk[e].pos;                /* bloco do nível de baixo */
            qtd = t->idx.n[nivel - 1] - ini;
            if (qtd > k) qtd = k;
            nivel--;
        }
        if (e >= 0) {
            int64_t r0 = blk[e].pos;
            int64_t n_reg = t->n_princ - r0;
            int64_t lo = 0, hi;
            if (n_reg > (int64_t)k) n_reg = (int64_t)k;
            if (SEEK64(t->f, (long long)(r0 + 1) * t->tam_reg, SEEK_SET) != 0) goto fim;
            if (fread(dblk, (size_t)t->tam_reg, (size_t)n_reg, t->f) != (size_t)n_reg) goto fim;
            L->cmp_dados++;                            /* 1 leitura traz o bloco todo */
            hi = n_reg - 1;
            while (lo <= hi) {                         /* busca binária dentro do bloco, na memória */
                int64_t mid = lo + (hi - lo) / 2;
                uint64_t km = tabela_chave(t, dblk + (size_t)mid * (size_t)t->tam_reg);
                if (km == chave) { L->achou = 1; L->pos = r0 + mid; rc = 0; goto fim; }
                if (km < chave) lo = mid + 1; else hi = mid - 1;
            }
            L->pred = r0 + hi;                         /* maior chave menor que a procurada */
        }
    }
    /* e < 0: chave menor que todas da área principal (pred = -1) */

    seguir_cadeia(t, chave, L, buf);
    rc = 0;
fim:
    free(blk);
    free(dblk);
    free(buf);
    return rc;
}

int tabela_localizar(Tabela *t, uint64_t chave, Loc *L)
{
    return t->idx_ok ? tabela_busca_indice(t, chave, L) : tabela_busca_binaria(t, chave, L);
}

/* ======================================================================
 *  5) Inserção  -> área de extensão, ligada pelo campo elo
 *  6) Remoção   -> marca delete = 1 (nada é reescrito)
 * ====================================================================== */

int tabela_inserir(Tabela *t, const char *const vals[])
{
    char *buf = tabela_novo_buf(t), *old = tabela_novo_buf(t);
    Loc L;
    uint64_t chave;
    int64_t novo;
    int r = -1;

    if (!buf || !old) goto fim;
    if (montar_registro(t, vals, 0, 0, buf) != 0) goto fim;
    chave = tabela_chave(t, buf);

    if (tabela_localizar(t, chave, &L) != 0) {
        snprintf(t->erro, sizeof t->erro, "falha ao ler o arquivo");
        goto fim;
    }

    if (L.achou) {
        if (tabela_ler_reg(t, L.pos, old) != 0) goto fim;
        if (!tabela_removido(t, old)) {
            snprintf(t->erro, sizeof t->erro, "já existe um registro ativo com a chave %llu", (unsigned long long)chave);
            goto fim;
        }
        /* chave existia mas estava removida: reativa no mesmo lugar, preservando o elo */
        montar_registro(t, vals, 0, tabela_elo(t, old), buf);
        if (escrever_reg(t, L.pos, buf) != 0) goto fim;
        snprintf(t->erro, sizeof t->erro, "esse registro já existia como removido e foi reativado");
        r = 1;
        goto fim;
    }

    novo = t->n_princ + t->n_ext;                     /* fim do arquivo */
    montar_registro(t, vals, 0, L.prox, buf);       /* novo aponta para o sucessor da cadeia */
    if (escrever_reg(t, novo, buf) != 0) goto fim;

    if (L.ant_ext >= 0)      r = set_elo(t, L.ant_ext, novo + 1);   /* liga ao anterior na cadeia   */
    else if (L.pred >= 0)    r = set_elo(t, L.pred, novo + 1);      /* liga ao registro principal   */
    else { t->cab_elo = novo + 1; r = 0; }                           /* menor chave do arquivo       */
    if (r != 0) goto fim;

    t->n_ext++;
    r = cab_escrever(t);
    if (r == 0)
        snprintf(t->erro, sizeof t->erro, "registro inserido na área de extensão e ligado pelo campo elo");
fim:
    free(buf);
    free(old);
    return r;
}

int tabela_remover(Tabela *t, uint64_t chave)
{
    Loc L;
    char *buf = tabela_novo_buf(t);
    int r = -1;
    if (!buf) return -1;
    if (tabela_localizar(t, chave, &L) != 0) goto fim;
    if (!L.achou) {
        snprintf(t->erro, sizeof t->erro, "chave %llu não encontrada", (unsigned long long)chave);
        goto fim;
    }
    if (tabela_ler_reg(t, L.pos, buf) != 0) goto fim;
    if (tabela_removido(t, buf)) {
        snprintf(t->erro, sizeof t->erro, "o registro já estava removido");
        goto fim;
    }
    r = set_delete(t, L.pos, 1);
    if (r == 0)
        snprintf(t->erro, sizeof t->erro, "registro removido (campo delete marcado como 1)");
fim:
    free(buf);
    return r;
}

/* ======================================================================
 *  Percurso em ordem lógica (principal + cadeias de extensão intercaladas)
 * ====================================================================== */

static int percorrer_cadeia(Tabela *t, int64_t elo, CbReg cb, void *ctx, char *buf)
{
    int64_t guarda = t->n_ext + 1;
    while (elo > 0 && guarda-- > 0) {
        int64_t p = elo - 1;
        if (tabela_ler_reg(t, p, buf) != 0) return -1;
        if (cb(t, buf, p, ctx)) return 1;
        elo = tabela_elo(t, buf);
    }
    return 0;
}

int tabela_percorrer(Tabela *t, CbReg cb, void *ctx)
{
    char *bp = tabela_novo_buf(t), *be = tabela_novo_buf(t);
    int r = 0;
    if (!bp || !be) { free(bp); free(be); return -1; }

    r = percorrer_cadeia(t, t->cab_elo, cb, ctx, be);          /* chaves menores que a 1ª */
    for (int64_t i = 0; r == 0 && i < t->n_princ; i++) {
        int64_t elo;
        if (tabela_ler_reg(t, i, bp) != 0) { r = -1; break; }
        elo = tabela_elo(t, bp);
        if (cb(t, bp, i, ctx)) { r = 1; break; }
        r = percorrer_cadeia(t, elo, cb, ctx, be);
    }
    free(bp);
    free(be);
    return r;
}

/* ======================================================================
 *  Reorganização: funde extensão na área principal, descarta removidos,
 *  grava arquivo novo (streaming) e reconstrói o índice
 * ====================================================================== */

typedef struct {
    FILE *out;
    long  gravados, descartados, da_extensao;
    int   ok;
} CtxReorg;

static int cb_reorg(Tabela *t, const char *reg, int64_t pos, void *vctx)
{
    CtxReorg *c = (CtxReorg *)vctx;
    char *tmp;
    if (tabela_removido(t, reg)) { c->descartados++; return 0; }
    tmp = tabela_novo_buf(t);
    if (!tmp) { c->ok = 0; return 1; }
    memcpy(tmp, reg, (size_t)t->tam_reg);
    tmp[t->off_del] = 0;
    memset(tmp + t->off_elo, 0, TAM_ELO);                      /* elo zerado */
    if (fwrite(tmp, 1, (size_t)t->tam_reg, c->out) != (size_t)t->tam_reg) c->ok = 0;
    free(tmp);
    c->gravados++;
    if (pos >= t->n_princ) c->da_extensao++;
    return c->ok ? 0 : 1;
}

int tabela_reorganizar(Tabela *t, long *incorporados, long *descartados)
{
    char tmpnome[400];
    CtxReorg c;
    char *cab;
    int pr;

    snprintf(tmpnome, sizeof tmpnome, "%s.tmp", t->arq_dados);
    memset(&c, 0, sizeof c);
    c.ok = 1;
    c.out = fopen(tmpnome, "wb");
    if (!c.out) {
        snprintf(t->erro, sizeof t->erro, "não foi possível criar o arquivo temporário da reorganização");
        return -1;
    }
    cab = tabela_novo_buf(t);
    if (!cab) { fclose(c.out); return -1; }
    cab_montar(t, cab, 0, 0, 0);
    fwrite(cab, 1, (size_t)t->tam_reg, c.out);                  /* reserva o cabeçalho */

    pr = tabela_percorrer(t, cb_reorg, &c);
    if (pr < 0 || !c.ok) {
        fclose(c.out); remove(tmpnome); free(cab);
        snprintf(t->erro, sizeof t->erro, "falha durante a reorganização (arquivo original mantido)");
        return -1;
    }
    cab_montar(t, cab, c.gravados, 0, 0);
    fseek(c.out, 0, SEEK_SET);
    fwrite(cab, 1, (size_t)t->tam_reg, c.out);
    fclose(c.out);
    free(cab);

    fclose(t->f);
    t->f = NULL;
    remove(t->arq_dados);                                       /* necessário no Windows */
    if (rename(tmpnome, t->arq_dados) != 0) {
        snprintf(t->erro, sizeof t->erro, "não foi possível renomear o arquivo temporário");
        return -1;
    }
    t->f = fopen(t->arq_dados, "r+b");
    if (!t->f) return -1;
    t->n_princ = c.gravados;
    t->n_ext = 0;
    t->cab_elo = 0;

    if (incorporados) *incorporados = c.da_extensao;
    if (descartados)  *descartados = c.descartados;
    return tabela_indice_construir(t);
}

/* ======================================================================
 *  1) Importação inicial a partir do CSV (streaming linha a linha)
 * ====================================================================== */

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
    return s;
}

static int dividir(char *linha, char delim, char *cols[], int max)
{
    int n = 0;
    cols[n++] = linha;
    for (char *p = linha; *p; p++) {
        if (*p == delim && n < max) { *p = '\0'; cols[n++] = p + 1; }
    }
    return n;
}

int tabela_importar_csv(Tabela *t, const char *csv, ImportStats *st)
{
    FILE *in, *rej = NULL;
    char *linha, *orig, *buf;
    char delim;
    int tem_ult = 0;
    uint64_t ult = 0;

    memset(st, 0, sizeof *st);
    in = fopen(csv, "rb");
    if (!in) {
        snprintf(t->erro, sizeof t->erro, "não foi possível abrir '%s'", csv);
        return -1;
    }
    linha = (char *)malloc(LINHA_MAX);
    orig = (char *)malloc(LINHA_MAX);
    buf = tabela_novo_buf(t);
    if (!linha || !orig || !buf) { fclose(in); free(linha); free(orig); free(buf); return -1; }

    if (!fgets(linha, LINHA_MAX, in)) {
        snprintf(t->erro, sizeof t->erro, "o CSV está vazio");
        fclose(in); free(linha); free(orig); free(buf);
        return -1;
    }
    delim = strchr(linha, ';') ? ';' : ',';                     /* 1ª linha = cabeçalho */

    /* recria o arquivo de dados */
    fclose(t->f);
    t->f = fopen(t->arq_dados, "w+b");
    if (!t->f) { fclose(in); free(linha); free(orig); free(buf); return -1; }
    t->n_princ = t->n_ext = t->cab_elo = 0;
    cab_escrever(t);                                            /* deixa o cursor após o cabeçalho */

    snprintf(st->arq_rejeitados, sizeof st->arq_rejeitados, "%s.rejeitados.csv", t->arq_dados);

    while (fgets(linha, LINHA_MAX, in)) {
        char *cols[MAX_CAMPOS + 4];
        const char *vals[MAX_CAMPOS];
        int n;
        int del = 0;
        size_t len = strcspn(linha, "\r\n");
        linha[len] = '\0';
        if (len == 0) continue;
        st->lidos++;
        strcpy(orig, linha);

        n = dividir(linha, delim, cols, MAX_CAMPOS + 4);
        if (n < t->n_campos) { st->invalidos++; goto rejeita; }
        for (int i = 0; i < t->n_campos; i++) vals[i] = trim(cols[i]);
        if (n > t->n_campos && trim(cols[t->n_campos])[0] == '1') del = 1;

        if (montar_registro(t, vals, del, 0, buf) != 0) { st->invalidos++; goto rejeita; }
        {
            uint64_t k = tabela_chave(t, buf);
            if (tem_ult && k <= ult) { st->fora_ordem++; goto rejeita; }
            ult = k;
            tem_ult = 1;
        }
        if (fwrite(buf, 1, (size_t)t->tam_reg, t->f) != (size_t)t->tam_reg) {
            snprintf(t->erro, sizeof t->erro, "erro de escrita em '%s'", t->arq_dados);
            fclose(in); free(linha); free(orig); free(buf);
            return -1;
        }
        st->gravados++;
        continue;
rejeita:
        if (!rej) rej = fopen(st->arq_rejeitados, "wb");
        if (rej) fprintf(rej, "%s\n", orig);
    }
    fclose(in);
    if (rej) fclose(rej);
    free(linha); free(orig); free(buf);

    t->n_princ = st->gravados;
    if (cab_escrever(t) != 0) return -1;
    return tabela_indice_construir(t);
}

/* ======================================================================
 *  Exibição
 * ====================================================================== */

static int w_col(const Tabela *t, int i)
{
    int w = t->campos[i].largura;
    int l = (int)strlen(t->campos[i].nome);
    return w > l ? w : l;
}

void tabela_imprimir_cab(const Tabela *t)
{
    int total = 0;
    for (int i = 0; i < t->n_campos; i++) {
        printf("%s%-*s", i ? " | " : "", w_col(t, i), t->campos[i].nome);
        total += (i ? 3 : 0) + w_col(t, i);
    }
    printf(" | delete | %10s\n", "elo");
    total += 3 + 6 + 3 + 10;
    for (int i = 0; i < total; i++) putchar('-');
    putchar('\n');
}

void tabela_imprimir_reg(const Tabela *t, const char *reg, int64_t pos)
{
    char v[64];
    (void)pos;
    for (int i = 0; i < t->n_campos; i++) {
        tabela_campo(t, reg, i, v);
        if (i) printf(" | ");
        if (t->campos[i].tipo != T_TXT) printf("%*s", w_col(t, i), v);
        else                            printf("%-*s", w_col(t, i), v);
    }
    printf(" | %-6d | %10lld\n", (int)reg[t->off_del], (long long)tabela_elo(t, reg));
}

void tabela_info(Tabela *t)
{
    long long tam;
    SEEK64(t->f, 0, SEEK_END);
    tam = TELL64(t->f);
    printf("\n--- Arquivo de dados: %s ---\n", t->arq_dados);
    printf("Tamanho do registro ........ %d bytes\n", t->tam_reg);
    printf("Tamanho do arquivo ......... %lld bytes (%lld registros, contando o cabeçalho)\n", tam, tam / t->tam_reg);
    printf("Área principal ............. %lld registros\n", (long long)t->n_princ);
    printf("Área de extensão ........... %lld registros\n", (long long)t->n_ext);
    printf("Início da cadeia (cab_elo) . %lld\n", (long long)t->cab_elo);
    printf("Chave primária ............. %s\n", t->campos[t->idx_chave].nome);
    printf("--- Índice parcial: %s ---\n", t->arq_indice);
    if (t->idx_ok) {
        printf("Espaçamento ................ 1 entrada a cada %llu (registros no nível 1; entradas do nível anterior nos demais)\n",
               (unsigned long long)t->idx.k);
        printf("Níveis ..................... %llu\n", (unsigned long long)t->idx.n_niveis);
        for (uint64_t i = 0; i < t->idx.n_niveis; i++) {
            char rot[32];
            snprintf(rot, sizeof rot, "nível %llu%s", (unsigned long long)(i + 1), i + 1 == t->idx.n_niveis ? " (topo)" : "");
            printf("  %-17s %llu entradas\n", rot, (unsigned long long)t->idx.n[i]);
        }
        printf("Cobre a área principal ..... %llu registros (a extensão é alcançada pelos elos)\n", (unsigned long long)t->idx.n_princ);
    } else {
        printf("Índice indisponível.\n");
    }
}

/* Lista as primeiras 'max_n' entradas de cada nível, do topo até o nível 1 */
void tabela_indice_listar(Tabela *t, int max_n)
{
    EntradaIdx *v;
    if (!t->idx_ok || t->idx.n_niveis == 0) return;
    v = (EntradaIdx *)malloc((size_t)max_n * sizeof *v);
    if (!v) return;
    for (int nv = (int)t->idx.n_niveis - 1; nv >= 0; nv--) {
        uint64_t n = t->idx.n[nv];
        uint64_t mostrar = n < (uint64_t)max_n ? n : (uint64_t)max_n;
        printf("\nNível %d%s (%llu entradas):  nº  chave  -> %s\n", nv + 1,
               nv + 1 == (int)t->idx.n_niveis ? " (topo)" : "", (unsigned long long)n,
               nv == 0 ? "posição no arquivo de dados" : "entrada do nível de baixo");
        if (ler_entradas(t, nv, 0, mostrar, v) != 0) break;
        for (uint64_t i = 0; i < mostrar; i++)
            printf("  %4llu  %14llu -> %lld\n", (unsigned long long)i, (unsigned long long)v[i].chave, (long long)v[i].pos);
        if (n > mostrar) printf("  ... (mostrando %llu de %llu)\n", (unsigned long long)mostrar, (unsigned long long)n);
    }
    free(v);
}
