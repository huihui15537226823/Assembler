# Assembler
a simple assembler (riscv64) 

g++ -std=c++17 Assembler.cpp -o myasstest
./myasstest test.s 
(output.o)

riscv64-linux-gnu-readelf -a output.o 
can see it

hu1@localhost:~/Assembler/Assembler$ file output.o
output.o: ELF 64-bit LSB relocatable, UCB RISC-V, soft-float ABI, version 1 (SYSV), not stripped

it is an exercise and there are still many deficiencies to be improved