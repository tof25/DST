#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>

int main(int argc, char **argv) {

    char *docname;
    xmlDocPtr doc;
    xmlNodePtr element, temp;
    xmlAttrPtr newattr;
    xmlChar *a, *b, *height, *id, *stage, *member;
    a = (xmlChar*)"3";
    b = (xmlChar*)"6";
    height = (xmlChar*)"3";


    if (argc <= 1) {
        printf("Usage: %s docname\n", argv[0]);
        return(0);
    }

    docname = malloc((strlen(argv[1]) + 4) * sizeof(char));
    docname = argv[1];
    strcat(docname, ".xml");
    doc = xmlNewDoc((const xmlChar*)"1.0");

    if (doc != NULL) {

        // root <dst>
        element = xmlNewDocNode(doc, NULL, (const xmlChar*)"dst", NULL);
        newattr = xmlNewProp(element, (const xmlChar*)"a", a);
        newattr = xmlNewProp(element, (const xmlChar*)"b", b);
        newattr = xmlNewProp(element, (const xmlChar*)"height", height);
        temp = xmlDocSetRootElement(doc, element);

        // children <node>
        id = (xmlChar*)"42";
        element = xmlNewTextChild(element, NULL, (const xmlChar*)"node", NULL);
        newattr = xmlNewProp(element, (const xmlChar*)"id", id);

        // children <stage>
        stage = (xmlChar*)"0";
        element = xmlNewTextChild(element, NULL, (const xmlChar*)"stage", NULL);
        newattr = xmlNewProp(element, (const xmlChar*)"value", stage);

        // children <member>
        member = (xmlChar*)"42";
        element = xmlNewTextChild(element, NULL, (const xmlChar*)"member", NULL);
        newattr = xmlNewProp(element, (const xmlChar*)"value", member);

        // save doc to disk
        xmlSaveFormatFile (docname, doc, 0);
        xmlFreeDoc(doc);
    }
    //free(docname);

    return (1);
}

