#include "xml_create.h"

int main(int argc, char **argv) {

    char *docname;
    xmlDocPtr doc;
    xmlNodePtr nptrRoot;
    int a, b, height, id, stage, member;
    a = 3;
    b = 6;
    height = 3;

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

    // conversions
    xmlChar *xa, *xb, *xheight;
    xa = itox(a);
    xb = itox(b);
    xheight = itox(height);

    xmlTextWriterPtr writer = xmlNewTextWriterDoc(&doc, 0);
    xmlTextWriterSetIndentString(writer, (xmlChar*)"  ");

    xmlTextWriterSetIndent(writer, 1);
    xmlTextWriterStartElement(writer, (xmlChar*)"dst");
        xmlTextWriterWriteAttribute(writer, (xmlChar*)"a", xa);
        xmlTextWriterWriteAttribute(writer, (xmlChar*)"b", xb);
        xmlTextWriterWriteAttribute(writer, (xmlChar*)"height", xheight);

        xmlTextWriterStartElement(writer, (xmlChar*)"node");
            xmlTextWriterWriteAttribute(writer, (xmlChar*)"id", (xmlChar*)"42");

            xmlTextWriterStartElement(writer, (xmlChar*)"stage");
                xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", (xmlChar*)"0");

                xmlTextWriterSetIndent(writer, 0);
                xmlTextWriterStartElement(writer, (xmlChar*)"member");
                    xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", (xmlChar*)"42");
                xmlTextWriterEndElement(writer);
                xmlTextWriterStartElement(writer, (xmlChar*)"member");
                    xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", (xmlChar*)"178");
                xmlTextWriterEndElement(writer);
                xmlTextWriterStartElement(writer, (xmlChar*)"member");
                    xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", (xmlChar*)"92");
                xmlTextWriterEndElement(writer);
                xmlTextWriterStartElement(writer, (xmlChar*)"member");
                    xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", (xmlChar*)"195");
                xmlTextWriterEndElement(writer);
                xmlTextWriterStartElement(writer, (xmlChar*)"member");
                    xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", (xmlChar*)"30");
                xmlTextWriterEndElement(writer);

            xmlTextWriterSetIndentString(writer, "");
            xmlTextWriterSetIndent(writer, 1);
            xmlTextWriterEndElement(writer);    //stage

            xmlTextWriterSetIndentString(writer, "  ");
            xmlTextWriterStartElement(writer, (xmlChar*)"stage");
                xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", (xmlChar*)"1");

                xmlTextWriterSetIndent(writer, 0);
                xmlTextWriterStartElement(writer, (xmlChar*)"member");
                    xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", (xmlChar*)"42");
                xmlTextWriterEndElement(writer);
                xmlTextWriterStartElement(writer, (xmlChar*)"member");
                    xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", (xmlChar*)"178");
                xmlTextWriterEndElement(writer);
                xmlTextWriterStartElement(writer, (xmlChar*)"member");
                    xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", (xmlChar*)"92");
                xmlTextWriterEndElement(writer);
                xmlTextWriterStartElement(writer, (xmlChar*)"member");
                    xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", (xmlChar*)"195");
                xmlTextWriterEndElement(writer);
                xmlTextWriterStartElement(writer, (xmlChar*)"member");
                    xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", (xmlChar*)"30");
                xmlTextWriterEndElement(writer);

            xmlTextWriterSetIndentString(writer, "");
            xmlTextWriterSetIndent(writer, 1);
            xmlTextWriterEndElement(writer);    //stage

            xmlTextWriterSetIndentString(writer, "  ");
        xmlTextWriterEndElement(writer);        //node
    xmlTextWriterEndElement(writer);            //dst

    xmlFreeTextWriter(writer);

        // root <dst>
        //nptrRoot = rootToXml(a, b, height, doc);
        /*

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
        // */


        /*
        // node 42
        id = 42;
        nodeToXml(id, nptrRoot, 0, routing_table, height, b);
        */

        //*****************************************************************************************

        /*
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
        nodeToXml(id, nptrRoot, 1, routing_table, height, b);
        */

        // save doc to disk
        xmlSaveFormatFile (docname, doc, 0);
        xmlFreeDoc(doc);
    } else {

        free(docname);
    }

    return (1);
}
