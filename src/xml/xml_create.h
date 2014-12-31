#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>

#ifndef XML_CREATE_H
#define XML_CREATE_H

/**
 * add a new text node as a child of given parent
 */
xmlNodePtr addTextChild(xmlNodePtr parent, char *text);

 /**
  * add a new text node as a sibling of given parent
  */
xmlNodePtr addTextSibling(xmlNodePtr parent, char *text);

 /**
  * convert an integer to an xmlChar. If the integer is negative, return an empty string
  */
xmlChar* itox(int arg);

 /**
  * create a <stage> xml node with its members as a child of given parent xml node
  */
void stageToXml(int stage, int *row, int size, xmlNodePtr parent, int last);

/**
 * \brief create an xml <node> node with all its stages and members, as a child of given parent root node
 */
void nodeToXml(int node_id, xmlNodePtr parent, int last, int **routing_table, int height, int row_size);

/**
 * \brief create a root xml node into a given xml document
 */
xmlNodePtr rootToXml(int a, int b, int height, xmlDocPtr doc);

#endif
