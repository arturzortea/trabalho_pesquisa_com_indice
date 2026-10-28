#include "consultas.h"
#include "modelos.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. Marca -> pessoas que acessaram                                   */
/*                                                                     */
/* Percorre o arquivo de pessoas um registro por vez; para cada acesso */
/* busca o produto pelo índice do arquivo de produtos e confere a      */
/* marca. Nada é acumulado na memória: cada acesso encontrado é        */
/* impresso na hora.                                                   */
/* ------------------------------------------------------------------ */

static int igual_sem_caixa(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

typedef struct {
    const char *marca;
    char       *reg_produto;     /* 1 registro de produto, reaproveitado a cada busca */
    unsigned long acessos;
} CtxMarca;

static int cb_acessos_da_marca(Tabela *t, const char *reg, int64_t pos, void *vctx)
{
    CtxMarca *c = (CtxMarca *)vctx;
    char pid[64], brand[64], uid[64], id[64], tipo[64];
    Loc L;
    (void)pos;
    if (tabela_removido(t, reg)) return 0;

    tabela_campo(t, reg, E_PRODUCT_ID, pid);
    if (tabela_localizar(&produtos, (uint64_t)strtoull(pid, NULL, 10), &L) != 0 || !L.achou ||
        tabela_ler_reg(&produtos, L.pos, c->reg_produto) != 0 || tabela_removido(&produtos, c->reg_produto))
        return 0;                                   /* produto não cadastrado */
    tabela_campo(&produtos, c->reg_produto, P_BRAND, brand);
    if (!igual_sem_caixa(brand, c->marca)) return 0;

    if (c->acessos == 0) {
        printf("\n%-12s | %-12s | %-12s | %s\n", "user_id", "id_consulta", "product_id", "event_type");
        printf("-------------+--------------+--------------+-----------\n");
    }
    tabela_campo(t, reg, E_USER_ID, uid);
    tabela_campo(t, reg, E_ID_CONSULTA, id);
    tabela_campo(t, reg, E_EVENT_TYPE, tipo);
    printf("%-12s | %-12s | %-12s | %s\n", uid, id, pid, tipo);
    c->acessos++;
    return 0;
}

void consulta_pessoas_por_marca(const char *marca)
{
    CtxMarca c;
    c.marca = marca;
    c.acessos = 0;
    c.reg_produto = tabela_novo_buf(&produtos);
    if (!c.reg_produto) { printf("Memória insuficiente.\n"); return; }

    tabela_percorrer(&pessoas, cb_acessos_da_marca, &c);

    if (c.acessos == 0)
        printf("\nNenhuma pessoa acessou produtos da marca '%s' (ou a marca não existe).\n", marca);
    else
        printf("\n%lu acesso(s) a produtos da marca '%s'.\n", c.acessos, marca);
    free(c.reg_produto);
}

/* ------------------------------------------------------------------ */
/* 2. id_consulta -> valor do item                                     */
/* ------------------------------------------------------------------ */

void consulta_valor_do_item(unsigned long long id_consulta)
{
    Loc L;
    char *ev = tabela_novo_buf(&pessoas), *pr = tabela_novo_buf(&produtos);
    char uid[64], hora[64], tipo[64], pid[64], marca[64], preco[64];

    if (!ev || !pr) { printf("Memória insuficiente.\n"); goto fim; }

    /* busca o evento pelo índice do arquivo de pessoas */
    if (tabela_localizar(&pessoas, id_consulta, &L) != 0 || !L.achou ||
        tabela_ler_reg(&pessoas, L.pos, ev) != 0 || tabela_removido(&pessoas, ev)) {
        printf("\nNão existe consulta ativa com id_consulta %llu.\n", id_consulta);
        goto fim;
    }
    tabela_campo(&pessoas, ev, E_USER_ID, uid);
    tabela_campo(&pessoas, ev, E_EVENT_TIME, hora);
    tabela_campo(&pessoas, ev, E_EVENT_TYPE, tipo);
    tabela_campo(&pessoas, ev, E_PRODUCT_ID, pid);

    printf("\nid_consulta ... %llu\n", id_consulta);
    printf("user_id ....... %s\n", uid);
    printf("event_time .... %s\n", hora);
    printf("event_type .... %s\n", tipo);
    printf("product_id .... %s\n", pid);

    /* liga ao arquivo de produtos pelo product_id, usando o índice dele */
    if (tabela_localizar(&produtos, (uint64_t)strtoull(pid, NULL, 10), &L) != 0 || !L.achou ||
        tabela_ler_reg(&produtos, L.pos, pr) != 0 || tabela_removido(&produtos, pr)) {
        printf("\nO produto %s não está cadastrado no arquivo de produtos; não há valor para mostrar.\n", pid);
        goto fim;
    }
    tabela_campo(&produtos, pr, P_BRAND, marca);
    tabela_campo(&produtos, pr, P_PRICE, preco);
    printf("marca ......... %s\n", marca[0] ? marca : "(sem marca)");
    printf("\nVALOR DO ITEM . %s\n", preco);
fim:
    free(ev);
    free(pr);
}
