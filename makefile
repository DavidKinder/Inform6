all: inform6

inform6: 
	$(CC) -O2 --std=c11 -DUNIX -o inform6 *.c

