override CFLAGS := -Wall -Werror -std=gnu99 -O0 -g $(CFLAGS) -I.
override LDLIBS := -pthread $(LDLIBS)

tls.o: tls.c tls.h
