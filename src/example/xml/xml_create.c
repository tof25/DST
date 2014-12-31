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

int main(int argc, char **argv) {

    char *docname;
    xmlDocPtr doc;
    xmlNodePtr nptrRoot, nptrNode, nptrTemp;
    xmlAttrPtr newattr;
    xmlChar *xa, *xb, *xheight;
    int a, b, height, id, stage, member;
    a = 3; xa = itox(a);
    b = 6; xb = itox(b);
    height = 3; xheight = itox(height);

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
        nptrRoot = xmlNewDocNode(doc, NULL, (const xmlChar*)"dst", NULL);
        newattr = xmlNewProp(nptrRoot, (const xmlChar*)"a", xa);
        newattr = xmlNewProp(nptrRoot, (const xmlChar*)"b", xb);
        newattr = xmlNewProp(nptrRoot, (const xmlChar*)"height", xheight);
        nptrTemp = xmlDocSetRootElement(doc, nptrRoot);
        addTextChild(nptrRoot, "\n    ");

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
        nptrNode = nodemkpToXml(id, nptrRoot, 0);

        for (stage = 0; stage < height; stage++) {

            stageToXml(stage, routing_table[stage], b, nptrNode, stage == height - 1);
        }

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
        nptrNode = nodemkpToXml(id, nptrRoot, 1);

        for (stage = 0; stage < height; stage++) {

            stageToXml(stage, routing_table[stage], b, nptrNode, stage == height - 1);
        }

        // save doc to disk
        xmlSaveFormatFile (docname, doc, 0);
        xmlFreeDoc(doc);
    }
    //free(docname);

    return (1);
}

