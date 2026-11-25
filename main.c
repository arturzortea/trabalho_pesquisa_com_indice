/*
 * Trabalho 1 - Algoritmos e Estruturas de Dados II
 * Arquivos de dados binários com índice parcial (sequencial-indexado)
 */

#include "tabela.h"
#include "modelos.h"
#include "consultas.h"
#include "arvoreb.h"
#include "hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mudar_pasta _chdir
#else
#include <unistd.h>
#define mudar_pasta chdir
#endif

/*
 * Compilação em um arquivo só: o botão "play" do VS Code (e o Code Runner) compila
 * apenas o arquivo aberto. Incluindo os outros módulos aqui, basta compilar main.c:
 *     gcc main.c -o trabalho1
 * (NÃO passe tabela.c, modelos.c, consultas.c ou arvoreb.c junto, senão as funções
 *  ficam definidas duas vezes.)
 */
#include "tabela.c"
#include "modelos.c"
#include "consultas.c"
#include "arvoreb.c"
#include "hash.c"

static void encerrar(int cod)
{
    tabela_fechar(&produtos);
    tabela_fechar(&pessoas);
    exit(cod);
}

static void ler_linha(const char *prompt, char *buf, size_t n)
{
    if (prompt) { printf("%s", prompt); fflush(stdout); }
    if (!fgets(buf, (int)n, stdin)) { printf("\n"); encerrar(0); }
    if (!strchr(buf, '\n')) {                     /* linha maior que o buffer: descarta o resto */
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
    }
    buf[strcspn(buf, "\r\n")] = '\0';
}

static int ler_opcao(void)
{
    char b[64], *fim;
    long v;
    ler_linha("Opção: ", b, sizeof b);
    v = strtol(b, &fim, 10);
    return (fim == b || *fim != '\0') ? -1 : (int)v;
}

/* Lê um inteiro sem sinal. Linha vazia devolve o padrão. Devolve 0 se inválido. */
static int ler_u64(const char *prompt, unsigned long long padrao, int tem_padrao, unsigned long long *out)
{
    char b[64], *fim;
    ler_linha(prompt, b, sizeof b);
    if (b[0] == '\0' && tem_padrao) { *out = padrao; return 1; }
    if (b[0] < '0' || b[0] > '9') { printf("Valor inválido.\n"); return 0; }
    *out = strtoull(b, &fim, 10);
    if (*fim != '\0') { printf("Valor inválido.\n"); return 0; }
    return 1;
}

/* Espera o Enter e volta ao menu */
static void aguardar_enter(void)
{
    char b[16];
    ler_linha("\nPressione Enter para voltar ao menu...", b, sizeof b);
}

/* definidas mais abaixo, junto das consultas */
static void mostrar_tempos(Tabela *t);                  /* tempos de criação dos índices */
static const char *fmt_tempo(double ms, char *b);       /* ms -> "12.3 µs" / "4.56 ms" / "1.20 s" */

/* Ações do menu de tabela */

static void acao_importar(Tabela *t)
{
    char csv[260], b[16];
    ImportStats st;

    printf("Arquivo CSV [%s]: ", t->csv_padrao);
    ler_linha(NULL, csv, sizeof csv);
    if (csv[0] == '\0') snprintf(csv, sizeof csv, "%s", t->csv_padrao);

    if (t->n_princ + t->n_ext > 0) {
        printf("Isto SUBSTITUI os %lld registros atuais de %s. Continuar? (s/n) ",
               (long long)(t->n_princ + t->n_ext), t->arq_dados);
        ler_linha(NULL, b, sizeof b);
        if (b[0] != 's' && b[0] != 'S') { printf("Cancelado.\n"); return; }
    }
    if (tabela_importar_csv(t, csv, &st) != 0) {
        printf("Erro: %s\n", t->erro);
        return;
    }
    printf("\nLinhas lidas ........... %ld\n", st.lidos);
    printf("Registros gravados ..... %ld  (%s)\n", st.gravados, t->arq_dados);
    printf("Rejeitados fora de ordem/duplicados: %ld   inválidos: %ld\n", st.fora_ordem, st.invalidos);
    if (st.fora_ordem + st.invalidos > 0)
        printf("As linhas rejeitadas foram salvas em %s\n", st.arq_rejeitados);
    printf("Índice parcial reconstruído em %s (%llu nível(is), 1 entrada a cada %llu).\n",
           t->arq_indice, (unsigned long long)t->idx.n_niveis, (unsigned long long)t->k);
    printf("\nTempo para refazer os índices:\n");
    mostrar_tempos(t);
}

/* imprime os registros físicos [ini, fim) */
static void imprimir_faixa(Tabela *t, int64_t ini, int64_t fim)
{
    char *buf = tabela_novo_buf(t);
    if (!buf) return;
    for (int64_t p = ini; p < fim; p++) {
        if (tabela_ler_reg(t, p, buf) != 0) { printf("Erro de leitura na posição %lld\n", (long long)p); break; }
        tabela_imprimir_reg(t, buf, p);
    }
    free(buf);
}

/* Opção 2: primeiros 50 registros da área principal + toda a área de extensão */
static void acao_primeiros(Tabela *t)
{
    int64_t mostrar = t->n_princ < 50 ? t->n_princ : 50;
    int64_t total = t->n_princ + t->n_ext;
    if (total == 0) { printf("O arquivo está vazio.\n"); return; }
    tabela_imprimir_cab(t);
    imprimir_faixa(t, 0, mostrar);
    if (t->n_princ > mostrar)
        printf("... (mais %lld registros na área principal não mostrados)\n", (long long)(t->n_princ - mostrar));
    if (t->n_ext > 0) {
        printf("--- área de extensão: %lld registro(s) ---\n", (long long)t->n_ext);
        imprimir_faixa(t, t->n_princ, total);
    } else {
        printf("--- área de extensão vazia ---\n");
    }
    aguardar_enter();
}

/* Opção 3: intervalo na ordem física (como está gravado, inclusive removidos) */
static void acao_intervalo(Tabela *t)
{
    unsigned long long ini, qtd;
    int64_t total = t->n_princ + t->n_ext;
    if (!ler_u64("Posição inicial [0]: ", 0, 1, &ini)) return;
    if (!ler_u64("Quantidade [10]: ", 10, 1, &qtd)) return;
    if ((int64_t)ini >= total) { printf("Posição fora do arquivo (total: %lld registros).\n", (long long)total); return; }
    if ((int64_t)(ini + qtd) > total) qtd = (unsigned long long)(total - (int64_t)ini);
    tabela_imprimir_cab(t);
    imprimir_faixa(t, (int64_t)ini, (int64_t)(ini + qtd));
    aguardar_enter();
}

/* Opção 4: intervalo na ordem lógica (ordem da chave, sem removidos, extensão no lugar certo) */
typedef struct { int64_t ini, qtd, visto, impressos; } CtxLogico;

static int cb_logico(Tabela *t, const char *reg, int64_t pos, void *vctx)
{
    CtxLogico *c = (CtxLogico *)vctx;
    if (tabela_removido(t, reg)) return 0;
    if (c->visto++ < c->ini) return 0;
    tabela_imprimir_reg(t, reg, pos);
    return ++c->impressos >= c->qtd;            /* para o percurso quando completar o intervalo */
}

static void acao_intervalo_logico(Tabela *t)
{
    unsigned long long ini, qtd;
    CtxLogico c;
    if (!ler_u64("Posição inicial entre os registros ativos [0]: ", 0, 1, &ini)) return;
    if (!ler_u64("Quantidade [10]: ", 10, 1, &qtd)) return;
    if (qtd == 0) { printf("Quantidade deve ser maior que zero.\n"); return; }
    c.ini = (int64_t)ini; c.qtd = (int64_t)qtd; c.visto = 0; c.impressos = 0;
    tabela_imprimir_cab(t);
    tabela_percorrer(t, cb_logico, &c);
    if (c.impressos == 0) printf("Nenhum registro ativo nessa posição.\n");
    else printf("%lld registro(s) listado(s).\n", (long long)c.impressos);
    aguardar_enter();
}

/* 1 se já existe um registro ATIVO com essa chave */
static int chave_ativa_existe(Tabela *t, const char *v)
{
    Loc L;
    char *buf;
    int r = 0;
    if (tabela_localizar(t, (uint64_t)strtoull(v, NULL, 10), &L) != 0 || !L.achou) return 0;
    buf = tabela_novo_buf(t);
    if (!buf) return 0;
    if (tabela_ler_reg(t, L.pos, buf) == 0 && !tabela_removido(t, buf)) r = 1;
    free(buf);
    return r;
}

static void acao_inserir(Tabela *t)
{
    char val[MAX_CAMPOS][128];
    const char *vp[MAX_CAMPOS];
    char prompt[96];
    int r;

    for (int i = 0; i < t->n_campos; i++) {
        snprintf(prompt, sizeof prompt, "%s: ", t->campos[i].nome);
        for (;;) {
            ler_linha(prompt, val[i], sizeof val[i]);
            if (tabela_validar_campo(t, i, val[i]) != 0) {
                printf("Erro: %s. Digite novamente.\n", t->erro);
                continue;
            }
            if (i == t->idx_chave && chave_ativa_existe(t, val[i])) {
                printf("Erro: já existe um registro ativo com %s %s. Digite novamente.\n", t->campos[i].nome, val[i]);
                continue;
            }
            break;
        }
        vp[i] = val[i];
    }
    r = tabela_inserir(t, vp);
    if (r < 0) printf("Não inserido: %s\n", t->erro);
    else {
        printf("OK: %s.\n", t->erro);
        if (r == 0) printf("A área de extensão agora tem %lld registro(s). Use 'Reorganizar' (opção 8) para incorporá-los ao arquivo principal.\n", (long long)t->n_ext);
        printf("\nTempo para refazer os índices:\n");
        mostrar_tempos(t);
    }
}

static void acao_remover(Tabela *t)
{
    unsigned long long chave;
    char prompt[96];
    snprintf(prompt, sizeof prompt, "Valor de %s a remover: ", t->campos[t->idx_chave].nome);
    if (!ler_u64(prompt, 0, 0, &chave)) return;
    if (tabela_remover(t, chave) == 0) {
        printf("OK: %s. O registro continua no arquivo, mas sai dos índices em memória.\n", t->erro);
        printf("\nTempo para refazer os índices:\n");
        mostrar_tempos(t);
    } else {
        printf("Não removido: %s\n", t->erro);
    }
}

static void acao_reorganizar(Tabela *t)
{
    long inc = 0, desc = 0;
    if (tabela_reorganizar(t, &inc, &desc) != 0) { printf("Erro: %s\n", t->erro); return; }
    printf("Reorganizado: %lld registros na área principal, %ld vindos da extensão, %ld removidos descartados.\n",
           (long long)t->n_princ, inc, desc);
    printf("\nTempo para refazer os índices:\n");
    mostrar_tempos(t);
}

static void acao_info(Tabela *t)
{
    tabela_info(t);
    printf("\nTempo de criação dos índices (última vez que foram feitos):\n");
    mostrar_tempos(t);
    tabela_indice_listar(t, 20);
}

/* Ações da árvore B+ */

static void arvore_info(Tabela *t)
{
    ArvoreBP *a = t->arvore;
    BPStats s;
    double ocup_f, ocup_i = 0;
    long nos_int = 0, ch_int = 0;

    bp_estatisticas(a, &s);
    for (int i = 0; i < s.niveis - 1; i++) { nos_int += s.nos[i]; ch_int += s.chaves[i]; }
    ocup_f = s.nos[s.niveis - 1] ? 100.0 * s.chaves[s.niveis - 1] / ((double)s.nos[s.niveis - 1] * a->max_chaves) : 0;
    if (nos_int) ocup_i = 100.0 * ch_int / ((double)nos_int * a->max_chaves);

    printf("\n--- Árvore B+ em memória sobre %s (%s) ---\n", t->campos[t->idx_chave].nome, t->arq_dados);
    printf("Ordem ...................... %d  => até %d registros por nó (mínimo %d, exceto na raiz)\n",
           a->ordem, a->max_chaves, a->min_chaves);
    printf("Nó interno ................. até %d chaves separadoras e %d filhos\n", a->max_chaves, a->max_chaves + 1);
    printf("Folha ...................... até %d elementos (chave, endereço do registro)\n", a->max_chaves);
    printf("Chaves indexadas ........... %ld (registros ativos do arquivo, inclusive os da extensão)\n", a->n_chaves);
    printf("Altura ..................... %d nível(is)  => %d nó(s) visitado(s) por busca\n", a->altura, a->altura);
    printf("Nós ........................ %ld (%ld folhas, %ld internos), %lu bytes cada = %lu KiB de memória\n",
           a->n_nos, a->n_folhas, a->n_nos - a->n_folhas, (unsigned long)a->tam_no,
           (unsigned long)(a->n_nos * a->tam_no / 1024));
    printf("Ocupação média ............. folhas %.1f%%, internos %.1f%%\n", ocup_f, ocup_i);
    printf("Divisões de nós (splits) ... %ld\n", a->divisoes);
    printf("Estrutura válida ........... %s\n", bp_verificar(a) ? "sim (ordem, limites, ocupação e folhas no mesmo nível)" : "NÃO");
    for (int i = 0; i < s.niveis; i++)
        printf("  nível %2d%-9s %7ld nó(s), %8ld chave(s)\n", i,
               i == 0 ? " (raiz)" : i == s.niveis - 1 ? " (folhas)" : "", s.nos[i], s.chaves[i]);
}

static void acao_arvore_estrutura(Tabela *t)
{
    if (!t->arvore) { printf("Árvore B+ indisponível.\n"); return; }
    arvore_info(t);
    printf("\nConteúdo por nível (primeiros nós de cada nível):\n");
    bp_imprimir(t->arvore, 4, 2 * BP_ORDEM_MAX);
    aguardar_enter();
}

static void acao_arvore_ordem(Tabela *t)
{
    unsigned long long ordem;
    char prompt[128];
    snprintf(prompt, sizeof prompt, "Ordem da árvore (1 = 2 por nó, 2 = 4 por nó, 3 = 6 por nó) [atual %d]: ",
             t->ordem_arvore);
    if (!ler_u64(prompt, (unsigned long long)t->ordem_arvore, 1, &ordem)) return;
    if (ordem < BP_ORDEM_MIN || ordem > BP_ORDEM_MAX) {
        printf("Ordem inválida: escolha %d, 2 ou %d.\n", BP_ORDEM_MIN, BP_ORDEM_MAX);
        return;
    }
    printf("Montando a árvore de ordem %llu com todos os registros do arquivo...\n", ordem);
    if (tabela_arvore_construir(t, (int)ordem) != 0) { printf("Erro: %s\n", t->erro); return; }
    {
        char b[32];
        printf("Árvore montada em %s.\n", fmt_tempo(t->ms_arvore, b));
    }
    arvore_info(t);
}

/* Ações da tabela hash */

static void imprimir_balde(const TabelaHash *h, size_t b, uint64_t destaque)
{
    int i = 0;
    printf("Balde %lu:", (unsigned long)b);
    if (!h->baldes[b]) printf(" (vazio)");
    for (const NoHash *no = h->baldes[b]; no; no = no->prox) {
        if (i++ >= 8) { printf(" -> ..."); break; }
        printf("%s[%llu: %d reg.]%s", i > 1 ? " -> " : " ", (unsigned long long)no->chave, no->n,
               no->chave == destaque ? "*" : "");
    }
    printf("\n");
}

static void acao_hash_info(Tabela *t)
{
    TabelaHash *h = t->hash;
    THStats s;
    long mostrados = 0;

    if (!h) { printf("Tabela hash indisponível.\n"); return; }
    th_estatisticas(h, &s);
    printf("\n--- Tabela hash em memória sobre %s (%s) ---\n", t->campos[t->campo_hash].nome, t->arq_dados);
    printf("Função hash ................ h(k) = k mod M (método da divisão, M primo)\n");
    printf("Colisões ................... encadeamento separado: cada balde é uma lista ligada de nós\n");
    printf("Valores repetidos .......... cada nó guarda a lista de endereços dos registros com aquele valor\n");
    printf("Baldes (M) ................. %lu\n", (unsigned long)s.m);
    printf("Valores distintos .......... %ld\n", h->n_chaves);
    printf("Registros indexados ........ %ld\n", h->n_enderecos);
    printf("Fator de carga ............. %.3f (valores distintos / M; rehash acima de %.1f)\n", s.carga, TH_CARGA_MAX);
    printf("Redimensionamentos ......... %ld\n", h->rehashes);
    printf("Colisões na inserção ....... %ld (valor novo caiu em balde já ocupado)\n", h->colisoes);
    printf("Baldes vazios .............. %ld (%.1f%%)\n", s.vazios, 100.0 * s.vazios / (double)s.m);
    printf("Baldes com 1 valor ......... %ld\n", s.com_um);
    printf("Baldes com colisão (2+) .... %ld\n", s.com_colisao);
    printf("Maior cadeia ............... %d nó(s)\n", s.maior_cadeia);
    printf("Comparações médias ......... %.2f por busca bem-sucedida\n", s.media_comp);
    printf("Valor mais repetido ........ %llu (%ld registros)\n", (unsigned long long)s.chave_maior_lista, s.maior_lista);
    printf("Distribuição: 0 nós: %ld | 1: %ld | 2: %ld | 3: %ld | 4: %ld | 5+: %ld baldes\n",
           s.dist[0], s.dist[1], s.dist[2], s.dist[3], s.dist[4], s.dist[5]);

    printf("\nExemplos de baldes com colisão:\n");
    for (size_t b = 0; b < h->m && mostrados < 5; b++)
        if (h->baldes[b] && h->baldes[b]->prox) { imprimir_balde(h, b, 0); mostrados++; }
    if (mostrados == 0) printf("(nenhum)\n");
    aguardar_enter();
}

/* ======================================================================
 *  Consultas (submenu de cada tabela), sempre com o tempo da pesquisa
 * ====================================================================== */

/* relógio de alta resolução, em milissegundos */
static double agora_ms(void)
{
    return tabela_relogio_ms();
}

static const char *fmt_tempo(double ms, char *b)
{
    if (ms < 1.0)         snprintf(b, 32, "%.1f µs", ms * 1000.0);
    else if (ms < 1000.0) snprintf(b, 32, "%.2f ms", ms);
    else                  snprintf(b, 32, "%.2f s", ms / 1000.0);
    return b;
}

static void mostrar_tempos(Tabela *t)
{
    char b[32];
    printf("  %s:\n", t->nome);
    printf("    Índice parcial (%s) %s %s (%llu nível(is))\n", t->arq_indice,
           t->idx_refeito ? "refeito em" : "lido do disco em", fmt_tempo(t->ms_indice, b),
           (unsigned long long)t->idx.n_niveis);
    if (t->idx_mem)
        printf("    Índice parcial carregado em memória: %s (%llu entradas, %llu KiB)\n", fmt_tempo(t->ms_indice_mem, b),
               (unsigned long long)t->idx_mem_n, (unsigned long long)(t->idx_mem_n * sizeof(EntradaIdx) / 1024));
    if (t->arvore)
        printf("    Árvore B+ ordem %d: %s (%ld chaves)\n", t->ordem_arvore, fmt_tempo(t->ms_arvore, b), t->arvore->n_chaves);
    if (t->hash)
        printf("    Tabela hash sobre %s: %s (%ld registros, %ld valores distintos, %lu baldes)\n",
               t->campos[t->campo_hash].nome, fmt_tempo(t->ms_hash, b), t->hash->n_enderecos,
               t->hash->n_chaves, (unsigned long)t->hash->m);
}

/* imprime 's' numa coluna de 'largura' letras (conta letras acentuadas em UTF-8 como 1) */
static void coluna(const char *s, int largura, int a_direita)
{
    int n = 0;
    for (const char *c = s; *c; c++)
        if (((unsigned char)*c & 0xC0) != 0x80) n++;
    if (a_direita) printf("%*s%s", largura > n ? largura - n : 0, "", s);
    else           printf("%s%*s", s, largura > n ? largura - n : 0, "");
}

/* ---------- pela chave (product_id em produtos, id_consulta em pessoas) ---------- */

enum { BUSCA_BINARIA = 0, BUSCA_INDICE = 1, BUSCA_INDICE_MEM = 2, BUSCA_ARVORE = 3 };
static const char *NOME_BUSCA[] = { "Pesquisa binária (arquivo)", "Índice parcial (arquivo .idx)",
                                    "Índice parcial (memória)", "Árvore B+ (memória)" };

/* faz a pesquisa e traz o registro encontrado; devolve o tempo em ms */
static double buscar_chave(Tabela *t, int metodo, uint64_t chave, Loc *L, char *buf)
{
    double t0 = agora_ms();
    if (metodo == BUSCA_BINARIA)     tabela_busca_binaria(t, chave, L);
    else if (metodo == BUSCA_INDICE) tabela_busca_indice(t, chave, L);
    else if (metodo == BUSCA_INDICE_MEM) tabela_busca_indice_mem(t, chave, L);
    else                             tabela_busca_arvore(t, chave, L);
    if (L->achou && tabela_ler_reg(t, L->pos, buf) == 0 && tabela_removido(t, buf))
        L->achou = 0;                    /* está no arquivo com delete = 1: não conta como achado (pos fica >= 0) */
    return agora_ms() - t0;
}

static void descrever_acessos(int metodo, const Loc *L, char *out, size_t n)
{
    long reg = L->pos >= 0 ? 1 : 0;     /* leitura do registro (também quando está removido) */
    if (metodo == BUSCA_BINARIA)
        snprintf(out, n, "%ld leitura(s) no arquivo", L->cmp_dados + L->cmp_ext + reg);
    else if (metodo == BUSCA_INDICE)
        snprintf(out, n, "%ld leitura(s) no .idx + %ld no arquivo", L->cmp_idx, L->cmp_dados + L->cmp_ext + reg);
    else if (metodo == BUSCA_INDICE_MEM)
        snprintf(out, n, "%ld nível(is) na memória + %ld leitura(s) no arquivo", L->cmp_idx, L->cmp_dados + L->cmp_ext + reg);
    else
        snprintf(out, n, "%ld nó(s) na memória + %ld leitura no arquivo", L->cmp_idx, reg);
}

static int metodo_disponivel(Tabela *t, int metodo)
{
    if (metodo == BUSCA_INDICE && !t->idx_ok) { printf("Índice parcial indisponível.\n"); return 0; }
    if (metodo == BUSCA_ARVORE && !t->arvore) { printf("Árvore B+ indisponível.\n"); return 0; }
    if (metodo == BUSCA_INDICE_MEM && (!t->idx_ok || tabela_indice_mem_carregar(t) != 0)) {
        printf("Não foi possível carregar o índice parcial em memória.\n");
        return 0;
    }
    return 1;
}

static void acao_busca(Tabela *t, int metodo)
{
    unsigned long long chave;
    char prompt[96], tempo[32], acessos[96], *buf;
    Loc L;
    double ms;

    if (!metodo_disponivel(t, metodo)) return;
    snprintf(prompt, sizeof prompt, "%s: ", t->campos[t->idx_chave].nome);
    if (!ler_u64(prompt, 0, 0, &chave)) return;
    buf = tabela_novo_buf(t);
    if (!buf) return;

    ms = buscar_chave(t, metodo, chave, &L, buf);
    descrever_acessos(metodo, &L, acessos, sizeof acessos);
    printf("\n[%s]\n", NOME_BUSCA[metodo]);
    if (L.achou) {
        tabela_imprimir_cab(t);
        tabela_imprimir_reg(t, buf, L.pos);
    } else if (L.pos >= 0) {
        printf("%s %llu não encontrado (foi removido: continua no arquivo com delete = 1).\n",
               t->campos[t->idx_chave].nome, chave);
    } else {
        printf("%s %llu não encontrado.\n", t->campos[t->idx_chave].nome, chave);
    }
    printf("Tempo: %s | %s\n", fmt_tempo(ms, tempo), acessos);
    free(buf);
}

/* mesma chave nos métodos disponíveis, lado a lado */
static void acao_comparar_chave(Tabela *t)
{
    unsigned long long chave;
    char prompt[96], tempo[32], acessos[96], *buf;
    int achou = 0;

    snprintf(prompt, sizeof prompt, "%s: ", t->campos[t->idx_chave].nome);
    if (!ler_u64(prompt, 0, 0, &chave)) return;
    buf = tabela_novo_buf(t);
    if (!buf) return;

    printf("\n");
    coluna("Método", 32, 0); coluna("Tempo", 12, 1); printf("   Acessos\n");
    for (int m = BUSCA_BINARIA; m <= BUSCA_ARVORE; m++) {
        Loc L;
        double ms;
        if ((m == BUSCA_INDICE && !t->idx_ok) || (m == BUSCA_ARVORE && !t->arvore)) continue;
        if (m == BUSCA_INDICE_MEM && (!t->idx_ok || tabela_indice_mem_carregar(t) != 0)) continue;
        ms = buscar_chave(t, m, chave, &L, buf);
        descrever_acessos(m, &L, acessos, sizeof acessos);
        coluna(NOME_BUSCA[m], 32, 0); coluna(fmt_tempo(ms, tempo), 12, 1); printf("   %s\n", acessos);
        achou = L.achou;
    }
    printf("%s\n", achou ? "" : "(chave não encontrada)");
    if (achou) {
        tabela_imprimir_cab(t);
        tabela_imprimir_reg(t, buf, -1);
    }
    free(buf);
}

/* ---------- por product_id em pessoas (coluna que não é a chave) ---------- */

enum { PID_ARQUIVO = 0, PID_HASH = 1 };
static const char *NOME_PID[] = { "Leitura do arquivo inteiro (sem índice)", "Tabela hash (memória)" };

#define PID_MOSTRAR 10          /* registros exibidos por pesquisa */
#define PID_LOTE    4096        /* registros por fread na leitura do arquivo */

typedef struct {
    long    achados;
    int64_t pos[PID_MOSTRAR];   /* primeiros endereços encontrados */
    int     comparacoes;        /* hash: nós visitados no balde */
    size_t  balde;
} ResPid;

static void guardar_pos(ResPid *r, int64_t pos)
{
    if (r->achados < PID_MOSTRAR) r->pos[r->achados] = pos;
    r->achados++;
}

/* acha todos os registros com o valor e traz cada um do disco; devolve o tempo em ms */
static double buscar_pid(Tabela *t, int metodo, uint64_t valor, ResPid *r, char *buf)
{
    double t0;
    memset(r, 0, sizeof *r);

    if (metodo == PID_HASH) {
        const NoHash *no;
        t0 = agora_ms();
        r->balde = th_funcao(t->hash, valor);
        no = th_buscar(t->hash, valor, &r->comparacoes);
        if (no)
            for (int i = 0; i < no->n; i++)
                if (tabela_ler_reg(t, no->end[i], buf) == 0) guardar_pos(r, no->end[i]);
        return agora_ms() - t0;
    } else {
        /* product_id não é a chave de ordenação: sem índice, é preciso ler o arquivo todo */
        int64_t total = t->n_princ + t->n_ext;
        char *lote = (char *)malloc((size_t)PID_LOTE * (size_t)t->tam_reg);
        if (!lote) return 0;
        t0 = agora_ms();
        if (total > 0 && tabela_ler_reg(t, 0, buf) == 0) {         /* lê o 1º e fica no 2º */
            if (!tabela_removido(t, buf) && tabela_campo_u64(t, buf, t->campo_hash) == valor) guardar_pos(r, 0);
            for (int64_t pos = 1; pos < total; ) {
                int64_t q = total - pos < PID_LOTE ? total - pos : PID_LOTE;
                if (fread(lote, (size_t)t->tam_reg, (size_t)q, t->f) != (size_t)q) break;
                for (int64_t i = 0; i < q; i++) {
                    char *reg = lote + i * t->tam_reg;
                    tabela_decifrar_campo(t, pos + i, reg, t->campo_hash);   /* só product_id e delete */
                    if (!tabela_removido(t, reg) && tabela_campo_u64(t, reg, t->campo_hash) == valor) guardar_pos(r, pos + i);
                }
                pos += q;
            }
        }
        t0 = agora_ms() - t0;
        free(lote);
        return t0;
    }
}

static void descrever_pid(Tabela *t, int metodo, const ResPid *r, char *out, size_t n)
{
    if (metodo == PID_HASH)
        snprintf(out, n, "balde %lu, %d comparação(ões) + %ld leitura(s) no arquivo",
                 (unsigned long)r->balde, r->comparacoes, r->achados);
    else
        snprintf(out, n, "%lld registros lidos", (long long)(t->n_princ + t->n_ext));
}

static void mostrar_achados_pid(Tabela *t, const ResPid *r, char *buf)
{
    if (r->achados == 0) return;
    tabela_imprimir_cab(t);
    for (long i = 0; i < r->achados && i < PID_MOSTRAR; i++)
        if (tabela_ler_reg(t, r->pos[i], buf) == 0) tabela_imprimir_reg(t, buf, r->pos[i]);
    if (r->achados > PID_MOSTRAR) printf("... (mais %ld)\n", r->achados - PID_MOSTRAR);
}

static int ler_pid(Tabela *t, unsigned long long *valor)
{
    char prompt[96];
    snprintf(prompt, sizeof prompt, "%s: ", t->campos[t->campo_hash].nome);
    return ler_u64(prompt, 0, 0, valor);
}

static void acao_busca_pid(Tabela *t, int metodo)
{
    unsigned long long valor;
    char tempo[32], acessos[128], *buf;
    ResPid r;
    double ms;

    if (metodo == PID_HASH && !t->hash) { printf("Tabela hash indisponível.\n"); return; }
    if (!ler_pid(t, &valor)) return;
    buf = tabela_novo_buf(t);
    if (!buf) return;

    ms = buscar_pid(t, metodo, valor, &r, buf);
    descrever_pid(t, metodo, &r, acessos, sizeof acessos);
    printf("\n[%s]\n", NOME_PID[metodo]);
    mostrar_achados_pid(t, &r, buf);
    printf("%ld registro(s) com %s = %llu\n", r.achados, t->campos[t->campo_hash].nome, valor);
    printf("Tempo: %s | %s\n", fmt_tempo(ms, tempo), acessos);
    free(buf);
}

static void acao_comparar_pid(Tabela *t)
{
    unsigned long long valor;
    char tempo[32], acessos[128], *buf;
    double ms[2];
    ResPid r;

    if (!t->hash) { printf("Tabela hash indisponível.\n"); return; }
    if (!ler_pid(t, &valor)) return;
    buf = tabela_novo_buf(t);
    if (!buf) return;

    printf("\n");
    coluna("Método", 42, 0); coluna("Tempo", 12, 1); printf("   Acessos\n");
    for (int m = PID_ARQUIVO; m <= PID_HASH; m++) {
        ms[m] = buscar_pid(t, m, valor, &r, buf);
        descrever_pid(t, m, &r, acessos, sizeof acessos);
        coluna(NOME_PID[m], 42, 0); coluna(fmt_tempo(ms[m], tempo), 12, 1); printf("   %s\n", acessos);
    }
    printf("%ld registro(s) com %s = %llu", r.achados, t->campos[t->campo_hash].nome, valor);
    if (ms[PID_HASH] > 0 && ms[PID_ARQUIVO] >= ms[PID_HASH])
        printf(" | hash %.0fx mais rápida\n", ms[PID_ARQUIVO] / ms[PID_HASH]);
    else if (ms[PID_ARQUIVO] > 0)
        printf(" | hash %.1fx mais lenta (muitos registros espalhados pelo arquivo)\n", ms[PID_HASH] / ms[PID_ARQUIVO]);
    else
        printf("\n");
    free(buf);
}

/* ---------- submenu ---------- */

static void menu_pesquisas(Tabela *t)
{
    const char *ch = t->campos[t->idx_chave].nome;
    int op;
    for (;;) {
        printf("\n----------- Consultas: %s -----------\n", t->nome);
        printf(" 1 - %s: pesquisa binária no arquivo de dados\n", ch);
        printf(" 2 - %s: índice parcial (arquivo .idx)\n", ch);
        if (t->usa_arvore) {
            printf(" 3 - %s: índice parcial carregado em memória\n", ch);
            printf(" 4 - %s: árvore B+ (índice em memória)\n", ch);
            printf(" 5 - Comparar os quatro métodos para o mesmo %s\n", ch);
        } else if (t->usa_hash) {
            const char *col = t->campos[t->campo_hash].nome;
            printf(" 3 - %s: leitura do arquivo inteiro (sem índice)\n", col);
            printf(" 4 - %s: tabela hash (índice em memória)\n", col);
            printf(" 5 - Comparar arquivo x hash para o mesmo %s\n", col);
        }
        puts(" 0 - Voltar");
        op = ler_opcao();
        if (op == 0) return;
        if (op == 1)      acao_busca(t, BUSCA_BINARIA);
        else if (op == 2) acao_busca(t, BUSCA_INDICE);
        else if (t->usa_arvore && op == 3) acao_busca(t, BUSCA_INDICE_MEM);
        else if (t->usa_arvore && op == 4) acao_busca(t, BUSCA_ARVORE);
        else if (t->usa_arvore && op == 5) acao_comparar_chave(t);
        else if (t->usa_hash && op == 3)   acao_busca_pid(t, PID_ARQUIVO);
        else if (t->usa_hash && op == 4)   acao_busca_pid(t, PID_HASH);
        else if (t->usa_hash && op == 5)   acao_comparar_pid(t);
        else printf("Opção inválida.\n");
    }
}

static void menu_tabela(Tabela *t)
{
    int op;
    for (;;) {
        printf("\n=========== %s  [%s] ===========\n", t->nome, t->arq_dados);
        printf("Área principal: %lld registros | Extensão: %lld registros | Índice: %llu nível(is), 1 entrada a cada %llu\n",
               (long long)t->n_princ, (long long)t->n_ext, (unsigned long long)t->idx.n_niveis, (unsigned long long)t->k);
        printf("Arquivo de dados: %s\n", t->cifrado ? "CRIPTOGRAFADO (XOR)" : "em claro (sem criptografia)");
        puts(" 1 - Importar dados iniciais de um CSV (gera o .dat binário e o índice)");
        puts(" 2 - Mostrar dados: primeiros 50 registros + área de extensão");
        puts(" 3 - Mostrar dados: intervalo na ordem física (como está gravado, com removidos)");
        puts(" 4 - Mostrar dados: intervalo na ordem lógica (sem removidos, extensão no lugar pelo elo)");
        if (t->usa_arvore)
            puts(" 5 - Consultas (pesquisa binária, índice em arquivo, índice em memória e árvore B+)");
        else if (t->usa_hash)
            puts(" 5 - Consultas (pesquisa binária, índice em arquivo e tabela hash em memória)");
        else
            puts(" 5 - Consultas (pesquisa binária e índice em arquivo)");
        puts(" 6 - Inserir registro (vai para a extensão, ligado pelo elo)");
        puts(" 7 - Remover registro (marca o campo delete)");
        puts(" 8 - Reorganizar arquivo (aplica extensão, descarta removidos, refaz índice)");
        puts(" 9 - Informações do arquivo e como o índice foi construído");
        if (t->usa_arvore) {
            puts("10 - Árvore B+: ordem, níveis e conteúdo dos nós");
            puts("11 - Árvore B+: trocar a ordem (1, 2 ou 3)");
        } else if (t->usa_hash) {
            puts("10 - Tabela hash: função, colisões e distribuição dos baldes");
        }
        puts(" 0 - Voltar");
        op = ler_opcao();
        switch (op) {
            case 1:  acao_importar(t); break;
            case 2:  acao_primeiros(t); break;
            case 3:  acao_intervalo(t); break;
            case 4:  acao_intervalo_logico(t); break;
            case 5:  menu_pesquisas(t); break;
            case 6:  acao_inserir(t); break;
            case 7:  acao_remover(t); break;
            case 8:  acao_reorganizar(t); break;
            case 9:  acao_info(t); break;
            case 10:
                if (t->usa_arvore)    acao_arvore_estrutura(t);
                else if (t->usa_hash) acao_hash_info(t);
                else printf("Opção inválida.\n");
                break;
            case 11:
                if (t->usa_arvore) acao_arvore_ordem(t);
                else printf("Opção inválida.\n");
                break;
            case 0:  return;
            default: printf("Opção inválida.\n");
        }
    }
}

static void menu_consultas(void)
{
    for (;;) {
        puts("\n=========== CONSULTAS (ligam pessoas e produtos pelo product_id) ===========");
        puts(" 1 - Informar uma marca: quais pessoas (user_id) acessaram produtos dela?");
        puts(" 2 - Informar um id_consulta: qual o valor do item acessado?");
        puts(" 0 - Voltar");
        switch (ler_opcao()) {
            case 1: {
                char marca[64], *ini = marca, *fim;
                ler_linha("Marca: ", marca, sizeof marca);
                while (*ini == ' ') ini++;
                fim = ini + strlen(ini);
                while (fim > ini && fim[-1] == ' ') *--fim = '\0';
                if (*ini == '\0') { printf("Informe uma marca.\n"); break; }
                consulta_pessoas_por_marca(ini);
                aguardar_enter();
                break;
            }
            case 2: {
                unsigned long long id;
                if (ler_u64("id_consulta: ", 0, 0, &id)) consulta_valor_do_item(id);
                break;
            }
            case 0: return;
            default: printf("Opção inválida.\n");
        }
    }
}

static int existe(const char *nome)
{
    FILE *f = fopen(nome, "rb");
    if (f) fclose(f);
    return f != NULL;
}

/* O botão "play" do VS Code (extensão C/C++ Compile Run) gera o executável na subpasta
 * output/ e o roda de lá. Nesse caso os arquivos .dat e .csv não estão na pasta atual:
 * o programa volta para a pasta do projeto (onde está o main.c) antes de abrir os dados. */
static void ir_para_pasta_do_projeto(void)
{
    char pasta[1024], *barra;
    if (existe("main.c")) return;                                  /* já está na pasta do projeto */
    if (existe("../main.c")) { mudar_pasta(".."); return; }       /* rodando de output/ (mesmo que
                                                                      ali tenha sobrado um .dat vazio) */
    if (existe(produtos.arq_dados)) return;                       /* executável copiado junto dos dados */

    /* __FILE__ tem o caminho do main.c usado na compilação */
    snprintf(pasta, sizeof pasta, "%s", __FILE__);
    barra = strrchr(pasta, '/');
    if (!barra || (strrchr(pasta, '\\') && strrchr(pasta, '\\') > barra)) barra = strrchr(pasta, '\\');
    if (barra) {
        *barra = '\0';
        mudar_pasta(pasta);
    }
}

/* ---------- criptografia ---------- */

/* mostra o 1º registro como está gravado no disco (só caracteres visíveis; o resto vira '.') */
static void mostrar_bruto(Tabela *t)
{
    char *buf = tabela_novo_buf(t);
    if (!buf) return;
    if (t->n_princ + t->n_ext > 0 && tabela_ler_bruto(t, 0, buf) == 0) {
        printf("    1º registro no disco: [");
        for (int i = 0; i < t->tam_reg - 1; i++) {
            unsigned char c = (unsigned char)buf[i];
            putchar(c >= 32 && c < 127 ? c : '.');
        }
        printf("]\n");
    }
    free(buf);
}

/* Os arquivos de dados ficam sempre criptografados: se ao abrir algum estiver em claro
 * (ex.: arquivo antigo), é cifrado aqui, uma vez só. */
static int garantir_cifrado(Tabela *t)
{
    char b[32];
    double t0;
    if (t->cifrado) return 0;
    printf("\n%s está em claro: criptografando com XOR (chave 'A')...\n", t->arq_dados);
    mostrar_bruto(t);
    t0 = agora_ms();
    if (tabela_alternar_cifra(t) != 0) { printf("    Erro: %s\n", t->erro); return -1; }
    printf("    Criptografado em %s (%lld registros)\n", fmt_tempo(agora_ms() - t0, b),
           (long long)(t->n_princ + t->n_ext));
    mostrar_bruto(t);
    return 0;
}

int main(void)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);        /* acentos corretos no cmd/terminal do VS Code */
#endif
    ir_para_pasta_do_projeto();
    printf("Trabalho 1 - Arquivos binários com índice parcial\n");
    if (tabela_abrir(&produtos) != 0) { printf("Erro ao abrir produtos: %s\n", produtos.erro); return 1; }
    if (tabela_abrir(&pessoas)  != 0) { printf("Erro ao abrir pessoas: %s\n",  pessoas.erro);  return 1; }
    if (garantir_cifrado(&produtos) != 0 || garantir_cifrado(&pessoas) != 0) return 1;
    printf("\nArquivos de dados criptografados com XOR (chave 'A'): cada registro é decifrado na memória\n"
           "quando é lido e cifrado de novo quando é gravado.\n");
    printf("\nÍndices carregados em memória:\n");
    mostrar_tempos(&produtos);
    mostrar_tempos(&pessoas);

    for (;;) {
        puts("\n================ MENU PRINCIPAL ================");
        puts(" 1 - Arquivo de PRODUTOS");
        puts(" 2 - Arquivo de PESSOAS (eventos de acesso)");
        puts(" 3 - Consultas (perguntas sobre os dados)");
        puts(" 0 - Sair");
        switch (ler_opcao()) {
            case 1: menu_tabela(&produtos); break;
            case 2: menu_tabela(&pessoas); break;
            case 3: menu_consultas(); break;
            case 0: encerrar(0); break;
            default: printf("Opção inválida.\n");
        }
    }
    return 0;
}
