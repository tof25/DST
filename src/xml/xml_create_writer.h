#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/xmlwriter.h>

#ifndef XML_CREATE_WRITER_H
#define XML_CREATE_WRITER_H

xmlChar* itox(int arg);
void stageToXml(xmlTextWriterPtr writer, int stage_nr, int* row, int row_size);
void nodeToXml(xmlTextWriterPtr writer, int node_id, int **table, int *row_size, int height);
void xmlHeader(xmlTextWriterPtr writer, int a, int b, int height);
void xmlFooter(xmlTextWriterPtr writer);

#endif
