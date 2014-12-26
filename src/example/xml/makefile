all: keyword

BIN_FILES = keyword
CC = gcc
WARNINGS =	-Wall
CFLAGS = `xml2-config --cflags`
LIBS = `xml2-config --libs`

%: %.c
	$(CC) $(WARNINGS) -o $@ $(CFLAGS) $(LIBS) $^

.PHONY : clean
clean:
	rm -f $(BIN_FILES) *.o *~

.SUFFIXES :
.PRECIOUS : %.o
