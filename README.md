Trabalho 1 - Arquivos de dados com índices parciais

Algoritmos e Estruturas de Dados II. Dataset: eCommerce behavior data from multi category store (Kaggle). Programa em C, com menu no terminal.

Compilar e executar

No VS Code, abra a pasta do projeto, compile com Ctrl+Shift+B e execute ./trabalho1 no terminal (no Windows, .\trabalho1.exe). Também funciona com make.

Pelo terminal:

gcc -Wall -Wextra -O2 main.c tabela.c modelos.c consultas.c -o trabalho1

Os CSVs ficam na mesma pasta do executável. No menu, a opção 1 de Produtos e a opção 1 de Pessoas importam o CSV e geram os arquivos .dat e .idx. Para usar os CSVs completos, basta informar o nome do arquivo na importação.

Arquivos do projeto
Arquivo	Função
tabela.h, tabela.c -	Motor genérico: registros fixos, índice parcial, busca binária, inserção, remoção, reorganização
modelos.h, modelos.c -	Definição dos dois arquivos (campos, tamanhos, chave) e do espaçamento do índice
consultas.h, consultas.c -	As duas consultas
main.c -	Menus
Makefile, compilar.bat	- Atalhos para compilar
produtos.dat, produtos.idx	- Dados e índice de produtos (gerados)
pessoas.dat, pessoas.idx -	Dados e índice de eventos (gerados)


Arquivos de dados

Os arquivos são binários: cada valor é gravado com fwrite do jeito que fica na memória, não como texto. Números são inteiros de 8 bytes (uint64) ou double. Textos são char[N] de tamanho fixo, completados com espaços no final. Todo registro termina com \n. Nos números de 8 bytes, os bytes mais altos ficam zerados (os zeros à esquerda). Os valores ficam em little-endian (Windows e Linux em PC).

O registro 0 é um cabeçalho, do mesmo tamanho dos demais: [registros da área principal: int64][registros da extensão: int64][início da cadeia (cab_elo): int64][assinatura "AEDBIN01"][zeros]['\n']. A assinatura permite reconhecer arquivos de outra versão. Nesse caso o programa renomeia o arquivo para .antigo e começa um novo.

Registro de produtos.dat, 54 bytes:

Campo	            Tipo            (bytes)	         Observação
product_id	      uint64            (8)	           chave primária, não repetida
brand	            char[20]          (20)	         valores repetidos
price           	double            (8)           	preço
category_id	      uint64            (8)           	valores repetidos
delete	          byte              (1)	           0 ativo, 1 removido
elo	              int64             (8)	           próximo registro da cadeia de extensão (0 = nenhum)

Registro de pessoas.dat, 67 bytes:

Campo	            Tipo            (bytes)	         Observação
id_consulta     	uint64            (8)            	chave primária, escolhida por quem insere
user_id	          uint64            (8)            	valores repetidos
event_time       	char[23]          (23)	          ex.: 2019-11-01 06:18:48 UTC
event_type       	char[10]          (10)	          view, cart, purchase...
product_id	      uint64            (8)           	valores repetidos, relaciona com produtos.dat
delete	          byte              (1)	
elo	              int64             (8)	

Organização física: a área principal (ordenada pela chave, vinda do CSV) vem primeiro, seguida da área de extensão (registros inseridos depois, na ordem de chegada).

Arquivos de índice

O índice é parcial e tem vários níveis. O arquivo .idx guarda um cabeçalho seguido dos níveis (nível 1, nível 2, ..., topo), com entradas de 16 bytes no formato (chave, posição).

Nível 1: uma entrada a cada K registros da área principal, com a posição do registro no .dat.
Nível 2: uma entrada a cada K entradas do nível 1, com o número da entrada no nível 1.
Nível N: sempre uma entrada a cada K entradas do nível anterior.

Os níveis são acrescentados até o topo caber em um bloco (K entradas ou menos). O espaçamento K é K_INDICE em modelos.c (padrão 100) e fica gravado no cabeçalho do .idx. Se K mudar, o índice é refeito na próxima abertura. Com K = 100, 100 mil registros dão 2 níveis (1.000 e 10 entradas) e 10 milhões dão 3 níveis. A opção 10 do menu mostra as informações do arquivo e as entradas de cada nível.

Consulta pelo índice: o programa lê o topo (1 leitura) e desce nível a nível. Em cada nível lê só o bloco de até K entradas apontado pelo nível de cima (1 leitura) e procura nele na memória, com busca binária. O nível 1 aponta para um bloco de até K registros do .dat, que é lido com 1 leitura e pesquisado na memória. Se a chave não está na área principal, o programa segue a cadeia de extensão. O total é um acesso por nível mais um no arquivo de dados. Com 100 mil registros são 3 acessos, contra cerca de 17 da pesquisa binária direto no arquivo.

O .idx é carregado ao abrir o programa. O programa confere o cabeçalho, o tamanho do arquivo e o número de registros, e refaz o índice se ele estiver ausente ou desatualizado. O índice é gravado em disco sempre que é refeito (na importação e na reorganização).

Inserção, remoção e reconstrução do índice:
Inserção: o registro novo vai para o fim do arquivo (extensão), sem reescrever nada. Ele é ligado pelo campo elo a uma cadeia ordenada por chave, que parte do registro da área principal de maior chave menor que a dele (ou do cab_elo do cabeçalho, se a chave for menor que todas). Assim a busca encontra o registro seguindo os elos. No menu, valor grande demais para o campo ou inválido gera erro e o programa pede o valor de novo. Chave já ativa é recusada. Chave que existia removida é reativada no mesmo lugar.
Remoção: só troca o campo delete de 0 para 1 (1 byte alterado). O registro continua no arquivo para manter a ordenação e as cadeias. As buscas o mostram como removido e as consultas o ignoram.
Reconstrução do índice: o índice cobre só a área principal, que não muda com inserções e remoções. Por isso ele não é refeito a cada operação. A reconstrução acontece na reorganização (opção 9 do menu): o programa percorre a ordem lógica (área principal e cadeias), grava um arquivo novo só com os registros ativos, já ordenados e sem elos, troca o arquivo e refaz o índice.
Importação: linhas fora de ordem, com chave repetida ou com valor inválido não entram. Elas são gravadas em <arquivo>.dat.rejeitados.csv e contadas no resumo.
Consultas

Estão no menu principal, opção 3. As duas ligam os arquivos pelo campo product_id.

Marca: informa uma marca e lista os acessos a produtos dela (user_id, id_consulta, product_id e tipo do evento). Percorre pessoas.dat um registro por vez e, para cada acesso, busca o produto pelo índice de produtos.dat e confere a marca. Cada acesso encontrado é impresso na hora.
id_consulta: informa um id_consulta, acha o evento pelo índice de pessoas, pega o product_id, acha o produto pelo índice de produtos e mostra o preço.
Uso de memória

Nenhuma operação carrega o arquivo de dados na memória. Tudo roda sobre os arquivos em disco, com fseek, fread e fwrite.

Operação -	O que fica na memória
Importar CSV -	1 linha do CSV e 1 registro por vez
Mostrar dados e reorganizar -	1 registro por vez (a reorganização grava o arquivo novo aos poucos)
Pesquisa binária	- 1 registro por leitura
Consulta pelo índice -	1 bloco de até K entradas por nível e 1 bloco de até K registros (K = 100)
Construir o índice - 1 registro por vez (lê só 1 a cada K)
Consultas -	1 registro de pessoas e 1 de produtos por vez