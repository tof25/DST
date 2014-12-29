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

xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath){

    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
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

int getMembers(xmlDocPtr doc, char *xpath) {

    xmlNodeSetPtr nodeset;
    xmlNodePtr curNode, curStage, curBrother;
    int node, stage, brother, id;

    // get nodes
    xmlXPathObjectPtr result = getnodeset(doc, (xmlChar*)xpath);

    if (result) {

        nodeset = result->nodesetval;
        for (node = 0; node < nodeset->nodeNr; node++) {

            // next node
            curNode = nodeset->nodeTab[node];

            // print node's id
            if (curNode != NULL) {
                if (!xmlStrcmp(curNode->name, (const xmlChar*)"node")) {

                    id = getIntProp(curNode, "id");
                    printf("node id : %d\n", id);
                } else {

                    // error : wrong node name
                    printf("[%s:%d] node name should have been 'node' here (%d) but it is '%s'",
                            __FUNCTION__,
                            __LINE__,
                            node,
                            curNode->name);
                    return (0);
                }
            } else {

                // error : curNode is NULL
                printf("[%s:%d] curNode shouldn't be NULL in row %d",
                        __FUNCTION__,
                        __LINE__,
                        node);
                return (0);
            }

            // parse node's stages
            curStage = curNode->xmlChildrenNode;

            do {

                // only children named "stage" will be parsed
                while (curStage != NULL && xmlStrcmp(curStage->name, (const xmlChar*) "stage")) {
                    curStage = curStage->next;
                }

                if (curStage != NULL) {

                    // read stage number
                    stage = getIntProp(curStage, "value");
                    printf("stage %d: ", stage);

                    // parse stage's children
                    curBrother = curStage->xmlChildrenNode;
                    brother = 0;

                    // parse brothers
                    do {

                        // only children named "member" will be parsed
                        while (curBrother != NULL && xmlStrcmp(curBrother->name, (const xmlChar*) "member")) {
                            curBrother = curBrother->next;
                        }

                        if (curBrother != NULL) {

                            printf("\t%d", getIntProp(curBrother, "value"));

                            // next brother
                            brother++;
                            curBrother = curBrother->next;
                        }
                    } while (curBrother != NULL);  // next brother

                    // next stage
                    printf("\n");
                    curStage = curStage->next;
                }
            } while (curStage != NULL);  // next stage
            printf("\n");
        } // next node
        xmlXPathFreeObject (result);
    }
    return (1);
}

int main(int argc, char **argv) {

    char *docname;
    xmlDocPtr doc;
    int ret = 0;
    char *a, *b, *height;
    xmlNodePtr cur;
    //xmlChar *keyword;

    // usage warning
    if (argc <= 1) {
        printf("Usage: %s docname\n", argv[0]);
        return(0);
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
    ret = getMembers(doc, "//node");
    if (ret == 1) {

        printf("dst parse succeeded\n");
    } else {

        printf("dst parse failed\n");
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();
    return (ret);
}
