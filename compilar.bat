@echo off
chcp 65001 >nul
gcc -Wall -Wextra -O2 main.c tabela.c modelos.c consultas.c -o trabalho1.exe
if errorlevel 1 (echo ERRO na compilacao) else (echo Compilado: trabalho1.exe)
pause
