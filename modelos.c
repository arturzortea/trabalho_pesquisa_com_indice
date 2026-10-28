#include "modelos.h"

/* Espaçamento do índice parcial: 1 entrada a cada K_INDICE registros (nível 1) e a cada
 * K_INDICE entradas nos níveis seguintes. O nº de níveis é calculado pelo tamanho do arquivo. */
#ifndef K_INDICE
#define K_INDICE 100
#endif

/*
 * ARQUIVO DE PRODUTOS  (produtos.dat) - registro BINÁRIO de 54 bytes
 *   product_id   8 bytes  uint64   chave primária (única)
 *   brand       20 bytes  char[20] texto, completado com espaços no final
 *   price        8 bytes  double
 *   category_id  8 bytes  uint64
 *   delete       1 byte   0 ativo / 1 removido
 *   elo          8 bytes  int64    próximo da cadeia de extensão (0 = nenhum)
 *   '\n'         1 byte
 */
Tabela produtos = {
    .nome       = "Produtos",
    .arq_dados  = "produtos.dat",
    .arq_indice = "produtos.idx",
    .csv_padrao = "dados-produto2.csv",
    .campos     = {
        { "product_id",  12, T_NUM },
        { "brand",       20, T_TXT },
        { "price",       10, T_DEC },
        { "category_id", 19, T_NUM },
    },
    .n_campos   = 4,
    .idx_chave  = P_PRODUCT_ID,
    .k          = K_INDICE,
};

/*
 * ARQUIVO DE PESSOAS / EVENTOS  (pessoas.dat) - registro BINÁRIO de 67 bytes
 *   id_consulta  8 bytes  uint64   chave primária (escolhida por quem insere)
 *   user_id      8 bytes  uint64   (repetido: um usuário gera vários eventos)
 *   event_time  23 bytes  char[23] "2019-11-01 06:18:48 UTC"
 *   event_type  10 bytes  char[10] view, cart, purchase, remove_from_cart
 *   product_id   8 bytes  uint64   (repetido) -> relaciona com produtos.dat
 *   delete       1 byte
 *   elo          8 bytes  int64
 *   '\n'         1 byte
 */
Tabela pessoas = {
    .nome       = "Pessoas (eventos)",
    .arq_dados  = "pessoas.dat",
    .arq_indice = "pessoas.idx",
    .csv_padrao = "dados-pessoa2.csv",
    .campos     = {
        { "id_consulta", 10, T_NUM },
        { "user_id",    10, T_NUM },
        { "event_time", 23, T_TXT },
        { "event_type", 10, T_TXT },
        { "product_id", 12, T_NUM },
    },
    .n_campos   = 5,
    .idx_chave  = E_ID_CONSULTA,
    .k          = K_INDICE,
};
