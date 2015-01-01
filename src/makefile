all: dst

BIN_FILES = dst
INSTALL_PATH = /usr/local
CC = gcc
PEDANTIC_PARANOID_FREAK =       -O0 -Wshadow -Wcast-align \
				-Waggregate-return -Wmissing-prototypes -Wmissing-declarations \
				-Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations \
				-Wmissing-noreturn -Wredundant-decls -Wnested-externs \
				-Wpointer-arith -Wwrite-strings -finline-functions
REASONABLY_CAREFUL_DUDE =	-Wall
NO_PRAYER_FOR_THE_WICKED =	-w -O2
FOR_VALGRIND = -g -O0 -Wno-switch
#WARNINGS = 			$(PEDANTIC_PARANOID_FREAK)
#WARNINGS = 			$(REASONABLY_CAREFUL_DUDE)
#WARNINGS = 			$(NO_PRAYER_FOR_THE_WICKED)
WARNINGS = 				$(FOR_VALGRIND)
CFLAGS = $(WARNINGS)

INCLUDES = -I$(INSTALL_PATH)/include `xml2-config --cflags`
DEFS = -L$(INSTALL_PATH)/lib/
LDADD = -lm -lsimgrid `xml2-config --libs`
LIBS =

vpath %.h xml
vpath %.c xml

%: %.o xml_create.o
	$(CC) $(INCLUDES) $(DEFS) $(CFLAGS) $^ $(LIBS) $(LDADD) -o $@

%.o: %.c %.h
	$(CC) $(INCLUDES) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(INCLUDES) $(CFLAGS) -c -o $@ $<

.PHONY : clean
clean:
	rm -f $(BIN_FILES) *.o *~

.SUFFIXES :
.PRECIOUS : %.o
