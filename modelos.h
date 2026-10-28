#ifndef MODELOS_H
#define MODELOS_H

#include "tabela.h"

/* Tabelas do projeto (definidas em modelos.c) */
extern Tabela produtos;
extern Tabela pessoas;

/* Posição de cada campo no schema */
enum { P_PRODUCT_ID = 0, P_BRAND, P_PRICE, P_CATEGORY_ID };
enum { E_ID_CONSULTA = 0, E_USER_ID, E_EVENT_TIME, E_EVENT_TYPE, E_PRODUCT_ID };

#endif
