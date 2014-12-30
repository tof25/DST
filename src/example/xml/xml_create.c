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


int main(int argc, char **argv) {

    char *docname;
    xmlDocPtr doc;
    xmlNodePtr nptrRoot, nptrNode, nptrStage, nptrTemp;
    xmlAttrPtr newattr;
    xmlChar *xa, *xb, *xheight, *xid, *xstage, *xmember;
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
        id = 42;
        // ====================================

        // children <node>
        xid = itox(id);
        nptrNode = xmlNewTextChild(nptrRoot, NULL, (const xmlChar*)"node", NULL);
        newattr = xmlNewProp(nptrNode, (const xmlChar*)"id", xid);
        addTextChild(nptrNode, "\n        ");
        addTextSibling(nptrNode, "\n    ");

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

        // children <stage>
        xstage = itox(stage);
        nptrStage = xmlNewTextChild(nptrNode, NULL, (const xmlChar*)"stage", NULL);
        newattr = xmlNewProp(nptrStage, (const xmlChar*)"value", xstage);
        addTextSibling(nptrStage, "\n        ");

        // children <member>
        for (member = 0; member < b; member++) {

            xmember = itox(routing_table[stage][member]);
            nptrTemp = xmlNewTextChild(nptrStage, NULL, (const xmlChar*)"member", NULL);
            newattr = xmlNewProp(nptrTemp, (const xmlChar*)"value", xmember);
        }

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

        // children <stage>
        xstage = itox(stage);
        nptrStage = xmlNewTextChild(nptrNode, NULL, (const xmlChar*)"stage", NULL);
        newattr = xmlNewProp(nptrStage, (const xmlChar*)"value", xstage);
        addTextSibling(nptrStage, "\n        ");

        // children <member>
        for (member = 0; member < b; member++) {

            xmember = itox(routing_table[stage][member]);
            nptrTemp = xmlNewTextChild(nptrStage, NULL, (const xmlChar*)"member", NULL);
            newattr = xmlNewProp(nptrTemp, (const xmlChar*)"value", xmember);
        }

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

        // children <stage>
        xstage = itox(stage);
        nptrStage = xmlNewTextChild(nptrNode, NULL, (const xmlChar*)"stage", NULL);
        newattr = xmlNewProp(nptrStage, (const xmlChar*)"value", xstage);
        addTextSibling(nptrStage, "\n    ");

        // children <member>
        for (member = 0; member < b; member++) {

            xmember = itox(routing_table[stage][member]);
            nptrTemp = xmlNewTextChild(nptrStage, NULL, (const xmlChar*)"member", NULL);
            newattr = xmlNewProp(nptrTemp, (const xmlChar*)"value", xmember);
        }

        //*****************************************************************************************

        // ====================================
        id = 121;
        // ====================================

        // children <node>
        xid = itox(id);
        nptrNode = xmlNewTextChild(nptrRoot, NULL, (const xmlChar*)"node", NULL);
        newattr = xmlNewProp(nptrNode, (const xmlChar*)"id", xid);
        addTextChild(nptrNode, "\n        ");
        //addTextSibling(nptrNode, "\n    ");
        addTextSibling(nptrNode, "\n");

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

        // children <stage>
        xstage = itox(stage);
        nptrStage = xmlNewTextChild(nptrNode, NULL, (const xmlChar*)"stage", NULL);
        newattr = xmlNewProp(nptrStage, (const xmlChar*)"value", xstage);
        addTextSibling(nptrStage, "\n        ");

        // children <member>
        for (member = 0; member < b; member++) {

            xmember = itox(routing_table[stage][member]);
            nptrTemp = xmlNewTextChild(nptrStage, NULL, (const xmlChar*)"member", NULL);
            newattr = xmlNewProp(nptrTemp, (const xmlChar*)"value", xmember);
        }

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

        // children <stage>
        xstage = itox(stage);
        nptrStage = xmlNewTextChild(nptrNode, NULL, (const xmlChar*)"stage", NULL);
        newattr = xmlNewProp(nptrStage, (const xmlChar*)"value", xstage);
        addTextSibling(nptrStage, "\n        ");

        // children <member>
        for (member = 0; member < b; member++) {

            xmember = itox(routing_table[stage][member]);
            nptrTemp = xmlNewTextChild(nptrStage, NULL, (const xmlChar*)"member", NULL);
            newattr = xmlNewProp(nptrTemp, (const xmlChar*)"value", xmember);
        }

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

        // children <stage>
        xstage = itox(stage);
        nptrStage = xmlNewTextChild(nptrNode, NULL, (const xmlChar*)"stage", NULL);
        newattr = xmlNewProp(nptrStage, (const xmlChar*)"value", xstage);
        addTextSibling(nptrStage, "\n    ");

        // children <member>
        for (member = 0; member < b; member++) {

            xmember = itox(routing_table[stage][member]);
            nptrTemp = xmlNewTextChild(nptrStage, NULL, (const xmlChar*)"member", NULL);
            newattr = xmlNewProp(nptrTemp, (const xmlChar*)"value", xmember);
        }

        // *****************************************************************************************

        // save doc to disk
        xmlSaveFormatFile (docname, doc, 0);
        xmlFreeDoc(doc);
    }
    //free(docname);

    return (1);
}

