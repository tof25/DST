BIN_FILES = add_attribute add_keyword encoding_conversion keyword reader1 retrieving_attribute retrieving_attribute xml_parser xpath xml_to_array

all: $(BIN_FILES)
.PHONY : all

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
