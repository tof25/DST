BIN_FILES = add_attribute add_keyword encoding_conversion keyword reader1 retrieving_attribute retrieving_attribute xml_parser xpath xml_to_array xml_create

.PHONY : all
all: $(BIN_FILES)

CC = gcc
WARNINGS =	-Wall -g
CFLAGS = `xml2-config --cflags`
LIBS = `xml2-config --libs`

%: %.o
	$(CC) $(WARNINGS) -o $@ $(CFLAGS) $(LIBS) $^

%.o: %.c
	$(CC) $(WARNINGS) -c $(CFLAGS) $^

.PHONY : clean
clean:
	rm -f $(BIN_FILES) *.o *~

.SUFFIXES :
.PRECIOUS : %.o
