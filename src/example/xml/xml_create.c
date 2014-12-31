#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>

xmlNodePtr addTextChild(xmlNodePtr parent, char *text) {

    xmlNodePtr temp;

    temp = xmlNewText((const xmlChar*)text);
    temp = xmlAddChild(parent, temp);
    return temp;
}

xmlNodePtr addTextSibling(xmlNodePtr parent, char *text) {

    xmlNodePtr temp;

    temp = xmlNewText((const xmlChar*)text);
    temp = xmlAddNextSibling(parent, temp);
    return temp;
}

xmlChar* itox(int arg) {

    int len = 10;
    char *str = malloc((len + 1) * sizeof(char));
    if (arg >= 0) {
        snprintf(str, len, "%d", arg);
    } else {
        sprintf(str, "");
    }

    return (xmlChar*)str;
}

void stageToXml(int stage, int *row, int size, xmlNodePtr parent, int last) {

    // children <stage>
    xmlChar *xstage = itox(stage);
    xmlNodePtr nptrStage = xmlNewTextChild(parent, NULL, (const xmlChar*)"stage", NULL);
    xmlAttrPtr newattr = xmlNewProp(nptrStage, (const xmlChar*)"value", xstage);

    if (last == 1) {
        addTextSibling(nptrStage, "\n    ");
    } else {
        addTextSibling(nptrStage, "\n        ");
    }

    // children <member>
    int member;
    xmlChar *xmember;
    xmlNodePtr nptrTemp;

    for (member = 0; member < size; member++) {

        xmember = itox(row[member]);
        nptrTemp = xmlNewTextChild(nptrStage, NULL, (const xmlChar*)"member", NULL);
        newattr = xmlNewProp(nptrTemp, (const xmlChar*)"value", xmember);
    }
}

xmlNodePtr nodemkpToXml(int node_id, xmlNodePtr parent, int last) {

    // children <node>
    xmlChar *xid = itox(node_id);
    xmlNodePtr nptrNode = xmlNewTextChild(parent, NULL, (const xmlChar*)"node", NULL);
    xmlNewProp(nptrNode, (const xmlChar*)"id", xid);
    addTextChild(nptrNode, "\n        ");

    if (last == 1) {
        addTextSibling(nptrNode, "\n");
    } else {
        addTextSibling(nptrNode, "\n    ");
    }
    return nptrNode;
}

void nodeToXml(int node_id, xmlNodePtr parent, int last, int **routing_table, int height, int row_size) {

    xmlNodePtr nptrNode = nodemkpToXml(node_id, parent, last);

    int stage;
    for (stage = 0; stage < height; stage++) {

        stageToXml(stage, routing_table[stage], row_size, nptrNode, stage == height - 1);
    }
}

xmlNodePtr rootToXml(int a, int b, int height, xmlDocPtr doc) {

    xmlChar *xa, *xb, *xheight;
    xa = itox(a);
    xb = itox(b);
    xheight = itox(height);

    // root <dst>
    xmlNodePtr nptrRoot = xmlNewDocNode(doc, NULL, (const xmlChar*)"dst", NULL);
    xmlNewProp(nptrRoot, (const xmlChar*)"a", xa);
    xmlNewProp(nptrRoot, (const xmlChar*)"b", xb);
    xmlNewProp(nptrRoot, (const xmlChar*)"height", xheight);
    xmlDocSetRootElement(doc, nptrRoot);
    addTextChild(nptrRoot, "\n    ");

    return nptrRoot;
}

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

        // root <dst>
        nptrRoot = rootToXml(a, b, height, doc);

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
        nodeToXml(id, nptrRoot, 0, routing_table, height, b);

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
        nodeToXml(id, nptrRoot, 1, routing_table, height, b);

        // save doc to disk
        xmlSaveFormatFile (docname, doc, 0);
        xmlFreeDoc(doc);
    }
    //free(docname);

    return (1);
}

