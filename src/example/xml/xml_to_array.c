#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

xmlDocPtr getdoc (char *docname) {

    xmlDocPtr doc;
    doc = xmlParseFile(docname);

    if (doc == NULL ) {
        fprintf(stderr,"Document not parsed successfully. \n");
        return NULL;
    }

    return doc;
}

/**
 * \brief return a node set for a given xpath expression and a context node
 * \param doc the document for the global context
 * \param ctxNode the context node (if NULL, evaluate xpath expression in the global context)
 * \param xpath the xpath expression
 * \return the result node set
 */
xmlNodeSetPtr getnodeset (xmlDocPtr doc, xmlNodePtr ctxNode, char *xpath){

    xmlXPathContextPtr context;
    xmlNodeSetPtr result;

    // build context
    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        printf("Error in xmlXPathNewContext\n");
        return NULL;
    }

    // build node set
    if (ctxNode != NULL) {

        result = (xmlXPathNodeEval(ctxNode, (xmlChar*)xpath, context))->nodesetval;
    } else {

        result = (xmlXPathEvalExpression((xmlChar*)xpath, context))->nodesetval;
    }
    xmlXPathFreeContext(context);

    // errors processing
    if (result == NULL) {

        printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }

    if (xmlXPathNodeSetIsEmpty(result)) {

        xmlXPathFreeNodeSet(result);
        printf("No result\n");
        return NULL;
    }

    return result;
}


char* getProp(xmlNodePtr cur, char *prop) {

    // args must not be NULL
    if (cur == NULL || prop == NULL) {

        printf("args in %s mustn't be NULL\n", __FUNCTION__);
        return NULL;
    }

    xmlChar *ret = xmlGetProp(cur, (xmlChar*)prop);
    if (ret == NULL) {

        // attribute not found
        printf("attribute '%s' not found\n", prop);
        return "";
    } else {

        // return attribute
        return (char*)ret;
    }
}

int getIntProp(xmlNodePtr cur, char *prop) {

    int ret = -1;

    // reads attribute
    char *buf = getProp(cur, prop);

    // converts it into integer
    if (buf != NULL) {

        ret = (!strcmp(buf, "") ? -1 : atoi(buf));
    }

    return ret;
}

int** getMembers(xmlDocPtr doc, char *xpath) {

    xmlNodeSetPtr nodeset, stageset, memberset;
    xmlNodePtr curNode;
    int node, stage, member, id;
    int **routing_table = NULL;

    // get nodes
    nodeset = getnodeset(doc, NULL, xpath);

    if (nodeset) {

        for (node = 0; node < nodeset->nodeNr; node++) {

            // next node
            curNode = nodeset->nodeTab[node];

            // print node's id
            id = getIntProp(curNode, "id");
            //printf("node id : %d\n", id);

            // get node's stages
            stageset = getnodeset(doc, curNode, "stage");

            if (stageset) {

                routing_table = malloc(stageset->nodeNr * sizeof(int*));

                for (stage = 0; stage < stageset->nodeNr; stage++) {

                    // get stage's members
                    memberset = getnodeset(doc, stageset->nodeTab[stage], "member");

                    if (memberset) {

                        routing_table[stage] = malloc(memberset->nodeNr * sizeof(int*));

                        for (member = 0; member < memberset->nodeNr; member++) {

                            routing_table[stage][member] = getIntProp(memberset->nodeTab[member], "value");
                        }
                        //printf("\n\n");

                        xmlXPathFreeNodeSet(memberset);
                    } else {

                        free (routing_table);
                        routing_table = NULL;
                    }
                }
                xmlXPathFreeNodeSet(stageset);
            }
            //printf("\n");
        } // next node
        xmlXPathFreeNodeSet (nodeset);
    }
    return(routing_table);
}

int main(int argc, char **argv) {

    char *docname;
    xmlDocPtr doc;
    int ret = 0;
    char *a, *b, *height, *id;
    xmlNodePtr cur;
    const int LenXPATH = 25;
    char xpath[LenXPATH] = "//node";
    int **routing_table = NULL;

    // usage warning
    if (argc <= 1) {
        printf("Usage: %s docname node_id\n", argv[0]);
        return(0);
    }

    if (argc == 3) {

        id = argv[2];
    } else {

        id = NULL;
    }

    // parse document
    docname = argv[1];
    doc = getdoc(docname);
    cur = xmlDocGetRootElement(doc);

    // error : empty document
    if (cur == NULL) {
        fprintf(stderr,"empty document\n");
        xmlFreeDoc(doc);
        return(0);
    }

    // error : wrong document
    if (xmlStrcmp(cur->name, (const xmlChar *) "dst")) {
        fprintf(stderr,"document of the wrong type, root node = %s", cur->name);
        xmlFreeDoc(doc);
        return(0);
    }

    // gets dst attributes
    a = getProp(cur, "a");
    b = getProp(cur, "b");
    height = getProp(cur, "height");

    // error : wrong dst attributes
    if (a == NULL || b == NULL || height == NULL) return(0);

    printf("DST attributes : a = %s - b = %s - height = %s\n\n", a, b, height);

    // print members
    if (id != NULL) {
        char buf[LenXPATH];
        sprintf(buf, "[@id=%s]", id);
        strncat(xpath, buf, LenXPATH - strlen(buf)) ;
    }

    routing_table = getMembers(doc, xpath);

    if (routing_table) {
        int i, j;
        printf("Node %s:\n", id);
        for (i = 0; i < atoi(height); i++) {
            for (j = 0; j < atoi(b); j++) {
                printf("\t%d", routing_table[i][j]);
            }
            printf("\n");
        }
    } else {
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();
    return (ret);
}
