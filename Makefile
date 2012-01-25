CC=gcc
CFLAGS=-Wall -O2 $(MYCFLAGS)
CPPFLAGS=
LDFLAGS=
LDLIBS=
AR=ar
ARFLAGS=cru
RM=rm -f

MYCFLAGS=

GCOV=gcov -b -f

target_lib=libgar.a
target_cmd=gardump
target=$(target_lib) $(target_cmd)
lib_source=garlib.c gfile.c gfilecrt.c garerror.c garalloc.c ginflate.c
lib_object=$(patsubst %.c,%.o,$(lib_source))
cmd_source=$(addsuffix .c,$(target_cmd))
cmd_object=$(patsubst %.c,%.o,$(cmd_source))
output=$(target) $(lib_object) $(cmd_object)\
			 $(patsubst %.c,%.gcno,$(lib_source) $(cmd_source))\
			 $(patsubst %.c,%.gcda,$(lib_source) $(cmd_source))\
			 $(addsuffix .gcov,$(lib_source))

all: $(target)

clean:
	$(RM) $(output)

test: gardump
	./gardump test.zip | diff - test.zip.lst
	./gardump test.zip pangram.txt | diff - pangram.txt
	./gardump test.zip pangramx.txt | diff - pangramx.txt
	./gardump test.zip alice.txt | diff - alice.txt

gcov:
	$(MAKE) clean
	$(MAKE) MYCFLAGS="-fprofile-arcs -ftest-coverage" test
	$(GCOV) $(lib_source)

.PHONY: all clean test gcov

libgar.a: $(lib_object)
gardump: gardump.o $(lib_object)

%.a:
	$(RM) $@
	$(AR) $(ARFLAGS) $@ $^

%: %.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
