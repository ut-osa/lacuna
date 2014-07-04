SIZES=7gb 8gb 9gb 10gb
CC=gcc

all: $(SIZES)
%gb.orig: orig.c
	$(CC) -DNUM_GB=$(*) $^ -o $@

%gb.order: order.c
	$(CC) -DNUM_GB=$(*) $^ -o $@

%gb.rand: rand.c
	$(CC) -DNUM_GB=$(*) $^ -o $@

7gb: 7gb.orig 7gb.order 7gb.rand
8gb: 8gb.orig 8gb.order 8gb.rand
9gb: 9gb.orig 9gb.order 9gb.rand
10gb: 10gb.orig 10gb.order 10gb.rand

.PHONY: clean

clean:
	rm -f *.orig *.order *.rand
