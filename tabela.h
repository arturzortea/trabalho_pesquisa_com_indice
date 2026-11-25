/*
 * tabela.h - Motor genérico de arquivos binários de registros fixos
 *            com organização sequencial-indexada (índice parcial em vários níveis),
 *            área de extensão encadeada por "elo" e remoção lógica por "delete".
 *
 * LAYOUT DO ARQUIVO DE DADOS (.dat) - BINÁRIO: os valores são gravados com fwrite, na
 * representação que têm na memória (não como texto):
 *
 *   registro 0            : CABEÇALHO (mesmo tamanho dos demais registros)
 *                           [n_princ:int64][n_ext:int64][cab_elo:int64][zeros...]['\n']
 *   registros 1..n_princ  : ÁREA PRINCIPAL, ordenada pela chave
 *   registros seguintes   : ÁREA DE EXTENSÃO (inserções, na ordem de chegada)
 *
 *   Cada registro: [campo1][campo2]...[campoN][delete:1 byte][elo:int64]['\n']
 *   - T_NUM : inteiro sem sinal de 8 bytes (uint64_t)
 *   - T_DEC : número decimal de 8 bytes (double)
 *   - T_TXT : texto em char[N] de tamanho fixo, completado com espaços no final
 *   - delete: 0 = ativo, 1 = removido logicamente
 *   - elo   : 0 = fim da cadeia; senão (posição do próximo registro + 1)
 *
 * ENCADEAMENTO DA EXTENSÃO:
 *   Um registro novo vai para o fim do arquivo (extensão). Ele é ligado, em
 *   ordem de chave, à cadeia que sai do registro da área principal que tem a
 *   maior chave menor que a dele (ou do "cab_elo" do cabeçalho, se a chave for
 *   menor que todas as da área principal).
 */
#ifndef TABELA_H
#define TABELA_H

/* MinGW (Windows): faz printf/snprintf aceitarem %lld, %llu, %zu. Deve vir antes de qualquer #include. */
#if defined(_WIN32) && !defined(__USE_MINGW_ANSI_STDIO)
#define __USE_MINGW_ANSI_STDIO 1
#endif

#ifndef _WIN32
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdint.h>

/* MinGW antigo: o snprintf padrão não entende %lld/%llu; usa a versão do MinGW. */
#if defined(_WIN32) && defined(__MINGW32__)
int __mingw_snprintf(char *, size_t, const char *, ...) __attribute__((format(gnu_printf, 3, 4)));
#define snprintf __mingw_snprintf
#endif

#define MAX_CAMPOS 8
#define TAM_NUM    8    /* bytes de um campo numérico (uint64_t ou double) */
#define TAM_ELO    8    /* bytes do campo elo (int64_t)                    */
#define TAM_CAB    8    /* bytes de cada contador do cabeçalho (int64_t)   */

typedef enum { T_NUM, T_DEC, T_TXT } TipoCampo;

typedef struct {
    const char *nome;
    int         largura;    /* texto: bytes gravados; numérico: máximo de caracteres digitados */
    TipoCampo   tipo;
} Campo;

/* Entrada do índice parcial (arquivo binário) */
typedef struct {
    uint64_t chave;
    int64_t  pos;     /* nível 1: posição do registro; demais: nº da entrada do nível de baixo */
} EntradaIdx;

#define MAX_NIVEIS 12

/* Cabeçalho do arquivo de índice (só campos de 8 bytes, sem padding) */
typedef struct {
    char     magia[8];
    uint64_t k;                 /* espaçamento: 1 entrada a cada k registros/entradas        */
    uint64_t n_princ;           /* registros da área principal quando o índice foi feito     */
    uint64_t n_niveis;          /* quantidade de níveis                                      */
    uint64_t n[MAX_NIVEIS];     /* entradas de cada nível (posição 0 = nível 1, o de baixo)  */
    uint64_t off[MAX_NIVEIS];   /* onde cada nível começa no arquivo de índice               */
} CabIdx;

typedef struct Tabela {
    /* ---- definição (preenchida em modelos.c) ---- */
    const char *nome;
    const char *arq_dados;
    const char *arq_indice;
    const char *csv_padrao;
    Campo       campos[MAX_CAMPOS];
    int         n_campos;
    int         idx_chave;      /* índice do campo que é a chave primária     */
    uint64_t    k;              /* espaçamento do índice parcial (>= 2)       */
    int         usa_arvore;     /* 1 = mantém também um índice árvore B+ em memória */
    int         ordem_arvore;   /* ordem d da árvore B+ (1, 2 ou 3): até 2d chaves por nó */
    int         usa_hash;       /* 1 = mantém um índice hash em memória sobre campo_hash */
    int         campo_hash;     /* coluna (não chave) indexada pela tabela hash          */

    /* ---- derivados (tabela_preparar) ---- */
    int         tam_reg;        /* bytes por registro, contando delete, elo e '\n' */
    int         off[MAX_CAMPOS];
    int         tam[MAX_CAMPOS];    /* bytes de cada campo no arquivo */
    int         off_del;
    int         off_elo;

    /* ---- estado em tempo de execução ---- */
    FILE       *f;              /* arquivo de dados  */
    FILE       *fi;             /* arquivo de índice */
    int64_t     n_princ;
    int64_t     n_ext;
    int64_t     cab_elo;
    int         cifrado;        /* 1 = registros gravados cifrados com XOR (flag no cabeçalho) */
    CabIdx      idx;
    int         idx_ok;
    EntradaIdx *idx_mem;        /* cópia do índice parcial em memória (NULL = não carregado) */
    uint64_t    idx_mem_n;      /* entradas na cópia em memória (todos os níveis)            */

    /* ---- tempos de criação dos índices, em ms (medidos a cada vez que são feitos) ---- */
    int         idx_refeito;    /* 1 = .idx foi (re)construído; 0 = só lido do disco       */
    double      ms_indice;      /* construir (ou validar/abrir) o .idx                     */
    double      ms_indice_mem;  /* carregar os níveis do .idx para a memória               */
    double      ms_arvore;      /* montar a árvore B+                                      */
    double      ms_hash;        /* montar a tabela hash                                    */
    struct ArvoreBP *arvore;    /* índice árvore B+ em memória (NULL se não usa)  */
    struct TabelaHash *hash;    /* índice hash em memória (NULL se não usa)       */
    char        erro[200];
} Tabela;

/* Resultado de uma localização por chave */
typedef struct {
    int     achou;      /* existe registro (ativo ou removido) com a chave */
    int64_t pos;        /* posição absoluta, se achou                       */
    int64_t pred;       /* maior registro da área principal com chave menor (-1 = nenhum) */
    int64_t ant_ext;    /* registro anterior da cadeia de extensão (-1 = nenhum)          */
    int64_t prox;       /* valor de elo que um novo registro deve receber                 */
    long    cmp_idx;    /* leituras/comparações no arquivo de índice   */
    long    cmp_dados;  /* leituras/comparações na área principal      */
    long    cmp_ext;    /* leituras/comparações na extensão (cadeia)   */
} Loc;

typedef struct {
    long lidos, gravados, fora_ordem, invalidos;
    char arq_rejeitados[300];
} ImportStats;

/* Callback do percurso em ordem lógica: devolver != 0 para parar */
typedef int (*CbReg)(Tabela *t, const char *reg, int64_t pos, void *ctx);

/* ciclo de vida */
void     tabela_preparar(Tabela *t);
int      tabela_abrir(Tabela *t);
void     tabela_fechar(Tabela *t);

/* acesso a registros e campos */
char    *tabela_novo_buf(const Tabela *t);
int      tabela_ler_reg(Tabela *t, int64_t pos, char *buf);
void     tabela_campo(const Tabela *t, const char *reg, int i, char *out);
uint64_t tabela_chave(const Tabela *t, const char *reg);
int      tabela_removido(const Tabela *t, const char *reg);
int64_t  tabela_elo(const Tabela *t, const char *reg);

/* operações pedidas no trabalho */
int      tabela_importar_csv(Tabela *t, const char *csv, ImportStats *st);   /* 1) dados iniciais */
void     tabela_imprimir_cab(const Tabela *t);                               /* 2) mostrar        */
void     tabela_imprimir_reg(const Tabela *t, const char *reg, int64_t pos);
int      tabela_busca_binaria(Tabela *t, uint64_t chave, Loc *L);            /* 3) pesquisa binária */
int      tabela_busca_indice(Tabela *t, uint64_t chave, Loc *L);             /* 4) via índice parcial */
int      tabela_localizar(Tabela *t, uint64_t chave, Loc *L);                /* usa índice se houver */
int      tabela_indice_mem_carregar(Tabela *t);   /* lê todos os níveis do .idx para a memória */
int      tabela_busca_indice_mem(Tabela *t, uint64_t chave, Loc *L);         /* índice parcial já em memória */
int      tabela_inserir(Tabela *t, const char *const vals[]);                /* 5) inserir        */
int      tabela_remover(Tabela *t, uint64_t chave);                          /* 6) remover        */
int      tabela_reorganizar(Tabela *t, long *incorporados, long *descartados);
int      tabela_percorrer(Tabela *t, CbReg cb, void *ctx);

/* índice */
int      tabela_indice_construir(Tabela *t);

/* índice árvore B+ em memória (só para tabelas com usa_arvore = 1).
 * A árvore guarda (chave, endereço) dos registros ATIVOS do arquivo (área principal e
 * extensão); os marcados como removidos ficam fora, por isso não são mais achados. */
int      tabela_arvore_construir(Tabela *t, int ordem);           /* ordem 1, 2 ou 3 */
int      tabela_busca_arvore(Tabela *t, uint64_t chave, Loc *L);  /* L->cmp_idx = nós visitados */

/* índice hash em memória sobre a coluna campo_hash (só para tabelas com usa_hash = 1).
 * Guarda os endereços dos registros ATIVOS do arquivo (área principal e extensão). */
int      tabela_hash_construir(Tabela *t);
uint64_t tabela_campo_u64(const Tabela *t, const char *reg, int i);     /* campo numérico */
int      tabela_localizar_rapido(Tabela *t, uint64_t chave, Loc *L); /* árvore se houver; senão índice */

/* Refaz o .idx (e a cópia em memória), a árvore B+ e a tabela hash, medindo cada tempo.
 * Chamada depois de inserir ou remover um registro. */
int      tabela_refazer_indices(Tabela *t);

/* Criptografia XOR dos registros (o cabeçalho fica sempre em claro, com a flag 'cifrado').
 * Toda leitura decifra só o registro lido, na memória; toda gravação cifra antes de gravar.
 * Os registros continuam com o mesmo tamanho, então posições, índices e elos não mudam. */
int      tabela_alternar_cifra(Tabela *t);                      /* cifra se está em claro, e vice-versa */
void     tabela_decifrar_reg(const Tabela *t, int64_t pos, char *reg);   /* para quem lê com fread */
void     tabela_decifrar_campo(const Tabela *t, int64_t pos, char *reg, int i); /* só o campo i + delete */
int      tabela_ler_bruto(Tabela *t, int64_t pos, char *buf);   /* bytes como estão no disco */

/* relógio de alta resolução (ms), usado para medir a criação dos índices e as buscas */
double   tabela_relogio_ms(void);

/* informações */
void     tabela_info(Tabela *t);
void     tabela_indice_listar(Tabela *t, int max_n1);
int      tabela_validar_campo(Tabela *t, int i, const char *v);

#endif
