Trabalho 1 - Arquivos de dados com índices parciais

Algoritmos e Estruturas de Dados II. Dataset: eCommerce behavior data from multi category store (Kaggle). Programa em C, com menu no terminal.

Compilar e executar

No VS Code, abra a pasta do projeto, compile com Ctrl+Shift+B e execute ./trabalho1 no terminal (no Windows, .\trabalho1.exe). Também funciona com make.

Pelo terminal:

gcc -Wall -Wextra -O2 main.c -o trabalho1

O main.c inclui os outros arquivos .c (tabela.c, modelos.c, consultas.c, arvoreb.c), então basta compilar ele. Assim o botão "play" do VS Code funciona com o main.c aberto.

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

Índice árvore B+ em memória (arquivo de produtos)

O arquivo de produtos (o menor) tem também um índice em memória com árvore B+ sobre a chave de ordenação (product_id), em arvoreb.h e arvoreb.c. Ordem da árvore (definição de Bayer & McCreight): numa árvore de ordem d, cada nó tem no máximo 2d chaves e, exceto a raiz, no mínimo d. Quando um nó fica com 2d+1 chaves, ele é dividido em dois nós com d chaves e uma chave sobe para o pai. Se a raiz divide, a árvore ganha um nível. Ordens disponíveis:
ordem 1: até 2 registros por nó (com 67 mil produtos, 16 níveis);
ordem 2: até 4 registros por nó (10 níveis), que é a ordem inicial;
ordem 3: até 6 registros por nó (8 níveis).

A árvore é montada uma vez, quando o programa abre: percorre o arquivo e insere (product_id, endereço) de cada registro. Depois fica inteira na memória, e cada pesquisa só desce da raiz até a folha. Ela também é refeita na importação, na reorganização e quando a ordem é trocada.

Por ser B+, os nós internos só têm chaves separadoras; os endereços dos registros (número do registro no .dat; byte = (endereço + 1) x tamanho do registro) ficam nas folhas, que são ligadas em lista. A árvore guarda todos os registros do arquivo, inclusive os da extensão e os removidos (eles continuam no mesmo endereço). Uma inserção no menu também insere a chave na árvore.

Menu de produtos: 11 consulta pela árvore (nós visitados na memória + 1 leitura no disco), 12 mostra ordem, níveis, ocupação e o conteúdo dos nós, 13 troca a ordem da árvore (1, 2 ou 3) e a monta de novo. As consultas do menu principal usam a árvore para achar o produto.

Índice hash em memória (arquivo de pessoas, coluna product_id)

O arquivo de pessoas tem um índice em memória com tabela hash sobre product_id, que não é a chave de ordenação (a chave é id_consulta). Fica em hash.h e hash.c. A tabela é montada ao abrir o programa, lendo o pessoas.dat em lotes de 4096 registros, e refeita na importação e na reorganização. Uma inserção no menu também entra no hash.

Função hash: método da divisão, h(k) = k mod M, com M primo. A tabela começa com cerca de 1/8 do número de registros em baldes (131101 para 1.048.577 eventos).

Valores repetidos: o mesmo product_id aparece em muitos eventos (há 67.356 valores distintos; o mais repetido aparece 12.293 vezes). Cada valor distinto é um nó da tabela, e o nó guarda a lista dos endereços de todos os registros com aquele valor.

Resolução de colisões: encadeamento separado. Cada balde é uma lista ligada de nós; quando dois product_id diferentes caem no mesmo balde, o novo nó entra no início da lista. Na busca, o programa calcula h(k) e percorre só a lista daquele balde. Se o fator de carga (valores distintos / M) passar de 1, a tabela cresce para o primeiro primo maior ou igual a 2M e os nós são redistribuídos (rehash). Com os dados atuais: fator de carga 0,51, 11.430 baldes com colisão, maior cadeia com 5 nós e 1,22 comparações em média por busca.

Menu de pessoas: 11 consulta por product_id (mostra o balde, as comparações e os registros; 1 acesso ao disco por registro mostrado), 12 mostra a função, o fator de carga, as colisões e a distribuição dos baldes.

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