#/bin/sh
mpicc -o training training.c
mpirun -np 6 training
