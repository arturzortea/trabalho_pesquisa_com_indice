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

static void mostrar_loc(const Loc *L, int por_indice)
{
    if (por_indice)
        printf("Acessos ao disco: %ld no arquivo de índice e %ld no arquivo de dados\n", L->cmp_idx, L->cmp_dados + L->cmp_ext);
    else
        printf("Acessos ao disco no arquivo de dados: %ld\n", L->cmp_dados + L->cmp_ext);
}

enum { BUSCA_BINARIA = 0, BUSCA_INDICE = 1, BUSCA_ARVORE = 2 };

static void acao_busca(Tabela *t, int metodo)
{
    unsigned long long chave;
    char prompt[96];
    Loc L;
    char *buf;
    int por_indice = metodo == BUSCA_INDICE;

    snprintf(prompt, sizeof prompt, "Valor de %s: ", t->campos[t->idx_chave].nome);
    if (!ler_u64(prompt, 0, 0, &chave)) return;

    if (metodo == BUSCA_ARVORE) {
        if (!t->arvore) { printf("Árvore B+ indisponível.\n"); return; }
        tabela_busca_arvore(t, chave, &L);
        printf("\nMétodo: árvore B+ em memória (raiz até a folha) + 1 seek no arquivo de dados\n");
        printf("Nós visitados na memória: %ld (altura da árvore)\n", L.cmp_idx);
        if (!L.achou) { printf("Chave %llu não encontrada (nenhum acesso ao disco).\n", chave); return; }
        printf("Endereço na folha: registro %lld (byte %lld do arquivo)\n",
               (long long)L.pos, (long long)(L.pos + 1) * t->tam_reg);
        buf = tabela_novo_buf(t);
        if (!buf) return;
        if (tabela_ler_reg(t, L.pos, buf) == 0) {
            tabela_imprimir_cab(t);
            tabela_imprimir_reg(t, buf, L.pos);
            if (tabela_removido(t, buf)) printf("(este registro está marcado como removido)\n");
        }
        printf("Acessos ao disco: 1 (só a leitura do registro)\n");
        free(buf);
        return;
    }

    if (por_indice) {
        if (!t->idx_ok) { printf("Índice indisponível.\n"); return; }
        tabela_busca_indice(t, chave, &L);
    } else {
        tabela_busca_binaria(t, chave, &L);
    }
    printf("\nMétodo: %s\n", por_indice ? "índice parcial (2 níveis) + seek no arquivo de dados"
                                        : "pesquisa binária direto no arquivo de dados");
    if (!L.achou) {
        printf("Chave %llu não encontrada.\n", chave);
        mostrar_loc(&L, por_indice);
        return;
    }
    buf = tabela_novo_buf(t);
    if (!buf) return;
    if (tabela_ler_reg(t, L.pos, buf) == 0) {
        tabela_imprimir_cab(t);
        tabela_imprimir_reg(t, buf, L.pos);
        if (tabela_removido(t, buf)) printf("(este registro está marcado como removido)\n");
    }
    mostrar_loc(&L, por_indice);
    free(buf);
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
        if (r == 0) printf("A área de extensão agora tem %lld registro(s). Use 'Reorganizar' (opção 9) para incorporá-los ao arquivo principal.\n", (long long)t->n_ext);
    }
}

static void acao_remover(Tabela *t)
{
    unsigned long long chave;
    char prompt[96];
    snprintf(prompt, sizeof prompt, "Valor de %s a remover: ", t->campos[t->idx_chave].nome);
    if (!ler_u64(prompt, 0, 0, &chave)) return;
    if (tabela_remover(t, chave) == 0) printf("OK: %s.\n", t->erro);
    else printf("Não removido: %s\n", t->erro);
}

static void acao_reorganizar(Tabela *t)
{
    long inc = 0, desc = 0;
    if (tabela_reorganizar(t, &inc, &desc) != 0) { printf("Erro: %s\n", t->erro); return; }
    printf("Reorganizado: %lld registros na área principal, %ld vindos da extensão, %ld removidos descartados.\n",
           (long long)t->n_princ, inc, desc);
    printf("Índice parcial reconstruído.\n");
}

static void acao_info(Tabela *t)
{
    tabela_info(t);
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
    printf("Chaves indexadas ........... %ld (registros do arquivo, inclusive extensão e removidos)\n", a->n_chaves);
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

static void acao_hash_busca(Tabela *t)
{
    TabelaHash *h = t->hash;
    const NoHash *no;
    unsigned long long valor, lim;
    char prompt[96], *buf;
    int comp;
    long mostrados = 0, removidos = 0;
    const char *col = t->campos[t->campo_hash].nome;

    if (!h) { printf("Tabela hash indisponível.\n"); return; }
    snprintf(prompt, sizeof prompt, "Valor de %s: ", col);
    if (!ler_u64(prompt, 0, 0, &valor)) return;

    no = th_buscar(h, valor, &comp);
    printf("\nMétodo: tabela hash em memória sobre %s (encadeamento separado)\n", col);
    printf("h(%llu) = %llu mod %lu = balde %lu\n", valor, valor, (unsigned long)h->m,
           (unsigned long)th_funcao(h, valor));
    imprimir_balde(h, th_funcao(h, valor), valor);
    printf("Comparações na cadeia do balde: %d\n", comp);
    if (!no) { printf("Nenhum registro com %s = %llu (nenhum acesso ao disco).\n", col, valor); return; }

    printf("%d registro(s) com %s = %llu.\n", no->n, col, valor);
    if (!ler_u64("Quantos mostrar [20]: ", 20, 1, &lim)) return;
    buf = tabela_novo_buf(t);
    if (!buf) return;
    tabela_imprimir_cab(t);
    for (int i = 0; i < no->n && mostrados < (long)lim; i++) {
        if (tabela_ler_reg(t, no->end[i], buf) != 0) continue;
        tabela_imprimir_reg(t, buf, no->end[i]);
        if (tabela_removido(t, buf)) removidos++;
        mostrados++;
    }
    if (no->n > mostrados) printf("... (mais %ld registro(s) não mostrados)\n", (long)no->n - mostrados);
    printf("Acessos ao disco: %ld (1 por registro mostrado; o endereço vem da lista do nó)\n", mostrados);
    if (removidos) printf("%ld dos registros mostrados estão marcados como removidos.\n", removidos);
    free(buf);
    aguardar_enter();
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

static void menu_tabela(Tabela *t)
{
    int op;
    for (;;) {
        printf("\n=========== %s  [%s] ===========\n", t->nome, t->arq_dados);
        printf("Área principal: %lld registros | Extensão: %lld registros | Índice: %llu nível(is), 1 entrada a cada %llu\n",
               (long long)t->n_princ, (long long)t->n_ext, (unsigned long long)t->idx.n_niveis, (unsigned long long)t->k);
        puts(" 1 - Importar dados iniciais de um CSV (gera o .dat binário e o índice)");
        puts(" 2 - Mostrar dados: primeiros 50 registros + área de extensão");
        puts(" 3 - Mostrar dados: intervalo na ordem física (como está gravado, com removidos)");
        puts(" 4 - Mostrar dados: intervalo na ordem lógica (sem removidos, extensão no lugar pelo elo)");
        puts(" 5 - Pesquisa binária direto no arquivo de dados");
        puts(" 6 - Consulta pelo índice parcial");
        puts(" 7 - Inserir registro (vai para a extensão, ligado pelo elo)");
        puts(" 8 - Remover registro (marca o campo delete)");
        puts(" 9 - Reorganizar arquivo (aplica extensão, descarta removidos, refaz índice)");
        puts("10 - Informações do arquivo e como o índice foi construído");
        if (t->usa_arvore) {
            puts("11 - Consulta pela árvore B+ (índice em memória)");
            puts("12 - Árvore B+: ordem, níveis e conteúdo dos nós");
            puts("13 - Árvore B+: trocar a ordem (1, 2 ou 3)");
        } else if (t->usa_hash) {
            printf("11 - Consulta por %s pela tabela hash (índice em memória)\n", t->campos[t->campo_hash].nome);
            puts("12 - Tabela hash: função, colisões e distribuição dos baldes");
        }
        puts(" 0 - Voltar");
        op = ler_opcao();
        if (op >= 11 && op <= 13 && !t->usa_arvore)                 /* opções do hash: 111 e 112 */
            op = (t->usa_hash && op <= 12) ? 100 + op : -1;
        switch (op) {
            case 111: acao_hash_busca(t); break;
            case 112: acao_hash_info(t); break;
            case 1:  acao_importar(t); break;
            case 2:  acao_primeiros(t); break;
            case 3:  acao_intervalo(t); break;
            case 4:  acao_intervalo_logico(t); break;
            case 5:  acao_busca(t, BUSCA_BINARIA); break;
            case 6:  acao_busca(t, BUSCA_INDICE); break;
            case 11: acao_busca(t, BUSCA_ARVORE); break;
            case 12: acao_arvore_estrutura(t); break;
            case 13: acao_arvore_ordem(t); break;
            case 7:  acao_inserir(t); break;
            case 8:  acao_remover(t); break;
            case 9:  acao_reorganizar(t); break;
            case 10: acao_info(t); break;
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

int main(void)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);        /* acentos corretos no cmd/terminal do VS Code */
#endif
    ir_para_pasta_do_projeto();
    printf("Trabalho 1 - Arquivos binários com índice parcial\n");
    if (tabela_abrir(&produtos) != 0) { printf("Erro ao abrir produtos: %s\n", produtos.erro); return 1; }
    if (tabela_abrir(&pessoas)  != 0) { printf("Erro ao abrir pessoas: %s\n",  pessoas.erro);  return 1; }

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
