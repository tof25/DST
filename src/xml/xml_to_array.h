#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#ifndef XML_TO_ARRAY_H
#define XML_TO_ARRAY_H

/**
 *  structure to receive a node's table
 */
typedef struct node_id {
    int id;                 // Node's id
    int **routing_table;    // Node's routing table
    int *sizes;             // number of members per stage
} s_node_id_t, *node_id_t;

/**
 *  parse xml document
 */
xmlDocPtr getdoc (const char *docname);

/**
 *  return a node set for a given xpath expression and a context node
 */
xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlNodePtr ctxNode, char *xpath);

/**
 *  reads a node's given attribute and returns it as a string
 */
char* getProp(xmlNodePtr cur, char *prop);

/**
 *  same as getProp but return attribute as an integer
 */
int getIntProp(xmlNodePtr cur, char *prop);

/**
 *  read node->stages->members and return a pointer to a structure containing them
 */
node_id_t getMembers(xmlDocPtr doc, char *xpath);

#endif
