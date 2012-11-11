#/bin/sh
mpicc -o main main.c
mpirun -np 6 main
