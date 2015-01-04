#include <string.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

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
    xmlChar *xpath = (xmlChar*) "//node[@id=225]";
    xmlNodeSetPtr nodeset, stageset;
    xmlXPathObjectPtr result;
    int i, j, prop;
    xmlChar *keyword;

    if (argc <= 1) {
        printf("Usage: %s docname\n", argv[0]);
        return(0);
    }

    docname = argv[1];
    doc = getdoc(docname);
    result = getnodeset (doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i = 0; i < nodeset->nodeNr; i++) {
            prop = getIntProp(nodeset->nodeTab[i], "id");
            printf("node id = %d\n", prop);
            stageset = (xmlXPathNodeEval(nodeset->nodeTab[i], (xmlChar*)"stage", xmlXPathNewContext(doc)))->nodesetval;
            if (stageset != NULL) {
                for (j = 0; j < stageset->nodeNr; j++) {
                    prop = getIntProp(stageset->nodeTab[j], "value");
                    printf("stage value = %d\n", prop);
                    keyword = xmlNodeListGetString(doc, stageset->nodeTab[j]->xmlChildrenNode, 1);
                    printf("members: %s\n", keyword);
                }
            }
            //xmlFree(keyword);
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return (1);
}
