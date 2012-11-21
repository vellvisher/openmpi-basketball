#/bin/sh
mpicc -o training training.c
mpirun -np 6 training

mpicc -o match match.c
mpirun -np 12 match
#mpirun --hostfile hostfile -np 12 match
