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

int main(int argc, char **argv) {

    char *docname;
    xmlDocPtr doc;
    xmlChar *xpath;
    xmlNodeSetPtr nodeset;
    xmlNodePtr cur;
    xmlXPathObjectPtr result;
    int a, b, height, node, id, brother;
    xmlChar *keyword;

    if (argc <= 1) {
        printf("Usage: %s docname\n", argv[0]);
        return(0);
    }

    docname = argv[1];
    doc = getdoc(docname);
    cur = xmlDocGetRootElement(doc);

    if (cur == NULL) {
        fprintf(stderr,"empty document\n");
        xmlFreeDoc(doc);
        return(0);
    }

    if (xmlStrcmp(cur->name, (const xmlChar *) "dst")) {
        fprintf(stderr,"document of the wrong type, root node = %s", cur->name);
        xmlFreeDoc(doc);
        return(0);
    }

    a = atoi((char*)xmlGetProp(cur, (const xmlChar*)"a"));
    b = atoi((char*)xmlGetProp(cur, (const xmlChar*)"b"));
    height = atoi((char*)xmlGetProp(cur, (const xmlChar*)"height"));

    printf("DST features : a = %d - b = %d - height = %d\n", a, b, height);

    xpath = (xmlChar*) "//node";
    result = getnodeset (doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (node=0; node < nodeset->nodeNr; node++) {

            cur = nodeset->nodeTab[node];

            if (cur != NULL) {
                if (!xmlStrcmp(cur->name, (const xmlChar*)"node")) {

                    id = atoi((char*)xmlGetProp(cur, (const xmlChar*)"id"));
                    printf("node id : %d\n", id);
                } else {

                    printf("[%s:%d] node name should have been 'node' here (%d) but it is '%s'",
                            __FUNCTION__,
                            __LINE__,
                            node,
                            cur->name);
                    return (0);
                }
            } else {

                printf("[%s:%d] cur shouldn't be NULL in row %d",
                        __FUNCTION__,
                        __LINE__,
                        node);
                return (0);
            }

            cur = cur->xmlChildrenNode;
            while (cur != NULL && xmlStrcmp(cur->name, (const xmlChar*) "stage")) {
                cur = cur->next;
            }
            if (cur != NULL) {
                brother = 0;
                printf("stage %s: ", xmlGetProp(cur, (const xmlChar*)"value"));
                cur = cur->xmlChildrenNode;
                while (cur != NULL && xmlStrcmp(cur->name, (const xmlChar*) "argument")) {
                    cur = cur->next;
                }
                do {
                    printf(" %d: %d | ",
                            brother,
                            atoi((char*)xmlGetProp(cur, (const xmlChar*)"value")));
                    brother++;
                    cur = cur->next;
                } while (cur != NULL);
                printf("\n");
            } else {
                printf("cur shouldn't be NULL here\n");
            }
            //keyword = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            //printf("keyword: %s\n", keyword);
            //xmlFree(keyword);
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return (1);
}
