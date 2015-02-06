#include "xml_create_writer.h"

int main(int argc, char **argv) {

    char *docname;
    xmlDocPtr doc;
    int a, b, height, id, stage, member;
    a = 3;
    b = 6;
    height = 3;
    int *row_size = malloc(height * sizeof(int));
    for (stage = 0; stage < height; stage++) {
        row_size[stage] = b;
    }

    if (argc <= 1) {
        printf("Usage: %s docname\n", argv[0]);
        return(0);
    }

    docname = malloc((strlen(argv[1]) + 4) * sizeof(char));
    docname = argv[1];
    strcat(docname, ".xml");
    doc = xmlNewDoc((const xmlChar*)"1.0");

    // routing tables
    int **routing_table;

    routing_table = malloc(height * sizeof(int*));
    for (stage = 0; stage < height; stage++) {

        routing_table[stage] = malloc(b * sizeof(int));
    }

    if (doc != NULL) {

        xmlTextWriterPtr writer = xmlNewTextWriterDoc(&doc, 0);
        xmlHeader(writer, a, b, height);

        // ====================================
        stage = 0;
        member = 0;

        routing_table[stage][member++] = 42;
        routing_table[stage][member++] = 141;
        routing_table[stage][member++] = 112;
        routing_table[stage][member++] = 30;
        routing_table[stage][member++] = -1;
        routing_table[stage][member++] = -1;
        // ====================================
        stage = 1;
        member = 0;

        routing_table[stage][member++] = 42;
        routing_table[stage][member++] = 119;
        routing_table[stage][member++] = 130;
        routing_table[stage][member++] = 248;
        routing_table[stage][member++] = -1;
        routing_table[stage][member++] = -1;
        // ====================================
        stage = 2;
        member = 0;

        routing_table[stage][member++] = 42;
        routing_table[stage][member++] = 121;
        routing_table[stage][member++] = 14;
        routing_table[stage][member++] = 163;
        routing_table[stage][member++] = 46;
        routing_table[stage][member++] = -1;
        // ====================================

        // node 42
        id = 42;
        nodeToXml(writer, id, routing_table, row_size, height);

        //*****************************************************************************************

        // ====================================
        stage = 0;
        member = 0;

        routing_table[stage][member++] = 121;
        routing_table[stage][member++] = 49;
        routing_table[stage][member++] = 33;
        routing_table[stage][member++] = 213;
        routing_table[stage][member++] = 132;
        routing_table[stage][member++] = 97;
        // ====================================
        stage = 1;
        member = 0;

        routing_table[stage][member++] = 121;
        routing_table[stage][member++] = 253;
        routing_table[stage][member++] = 62;
        routing_table[stage][member++] = 1;
        routing_table[stage][member++] = -1;
        routing_table[stage][member++] = -1;
        // ====================================
        stage = 2;
        member = 0;

        routing_table[stage][member++] = 42;
        routing_table[stage][member++] = 121;
        routing_table[stage][member++] = 14;
        routing_table[stage][member++] = 163;
        routing_table[stage][member++] = 46;
        routing_table[stage][member++] = -1;
        // ====================================

        // node 121
        id = 121;
        nodeToXml(writer, id, routing_table, row_size, height);

        xmlFooter(writer);

        // save doc to disk
        xmlSaveFormatFile (docname, doc, 0);
        xmlFreeDoc(doc);
    } else {

        free(docname);
    }
    free(row_size);

    return (1);
}
