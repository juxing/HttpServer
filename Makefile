EXECUTABLES = mzihttpd attacker
CFLAGS = -g -Wall

all : mzihttpd attacker
.PHONY : all

mzihttpd : mzihttpd.c
	gcc $(CFLAGS) -o mzihttpd mzihttpd.c -lpthread -ldl

attacker : attacker.c
	gcc $(CFLAGS) -o attacker attacker.c

clean :
	rm $(EXECUTABLES) 
