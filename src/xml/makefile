BIN_FILES = xml_to_array_run xml_create_run

.PHONY : all
all: $(BIN_FILES)

CC = gcc
WARNINGS =	-Wall -g
CFLAGS = `xml2-config --cflags`
LIBS = `xml2-config --libs`

%: %.o xml_to_array.o xml_create.o
	$(CC) $(WARNINGS) -o $@ $(CFLAGS) $^ $(LIBS)

%.o: %.c %.h
	$(CC) $(WARNINGS) -c $(CFLAGS) $<

%.o: %.c
	$(CC) $(WARNINGS) -c $(CFLAGS) $<

.PHONY : clean
clean:
	rm -f $(BIN_FILES) *.o *~

.SUFFIXES :
.PRECIOUS : %.o
