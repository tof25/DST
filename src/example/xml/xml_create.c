#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>

int main(int argc, char **argv) {

    char *docname;
    xmlDocPtr doc;
    xmlNodePtr element, temp;
    xmlAttrPtr newattr;
    xmlChar *a, *b, *height;
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

        element = xmlNewDocNode(doc, NULL, (const xmlChar*)"dst", NULL);
        newattr = xmlNewProp(element, (const xmlChar*)"a", a);
        temp = xmlDocSetRootElement(doc, element);

        xmlSaveFormatFile (docname, doc, 0);
        xmlFreeDoc(doc);
    }
    //free(docname);

    return (1);
}

