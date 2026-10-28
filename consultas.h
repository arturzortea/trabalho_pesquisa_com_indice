#ifndef CONSULTAS_H
#define CONSULTAS_H

/* As duas consultas ligam os arquivos de pessoas e de produtos pelo campo product_id. */

/* 1. Dada uma marca, lista os user_id das pessoas que acessaram produtos dela */
void consulta_pessoas_por_marca(const char *marca);

/* 2. Dado um id_consulta, informa o valor (preço) do item acessado */
void consulta_valor_do_item(unsigned long long id_consulta);

#endif
