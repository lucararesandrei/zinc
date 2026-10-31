zinc: zinc.c config.c config.h
	$(CC) zinc.c config.c -o zinc -Wall -Wextra -pedantic -std=c99

tests: tests.c zinc.c config.c config.h
	$(CC) tests.c config.c -o tests -Wall -Wextra -pedantic -std=c99

test: tests
	./tests
