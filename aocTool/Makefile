CC=gcc
C_FLAGS=-g
I_FLAGS=-I./
I_FLAGS+=-I/usr/include/libxml2
L_FLAGS=
L_FLAGS+=-lcurl
L_FLAGS+=-xml2

SRCS=main.c
SRCS+=src/string+.c
SRCS+=src/number.c
SRCS+=src/hash.c
SRCS+=src/data.c
SRCS+=src/matrix.c
SRCS+=src/linalg.c
SRCS+=src/flood_fill.c

.PHONY : main

build: main 

main : $(SRCS:.c=.o)
	@$(CC) $(L_FLAGS) $^ -o $@

%.o : %.c
	@$(CC) $(C_FLAGS) $(I_FLAGS) -c $^ -o $@

mem : main
	valgrind -s --leak-check=full --show-leak-kinds=all --track-origins=yes ./$< input.txt

clear :
	@rm -vf *.o 
	@rm -vf src/*.o 
	@rm -vf main 