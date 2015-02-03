#include "xml_create.h"

/**
 * \brief add a new text node as a child of given parent
 * \param parent given parent node
 * \param text the child's text
 * \return a pointer to the new child
 */
xmlNodePtr addTextChild(xmlNodePtr parent, char *text) {

    xmlNodePtr temp;

    temp = xmlNewText((const xmlChar*)text);
    temp = xmlAddChild(parent, temp);
    return temp;
}


/**
 * \brief add a new text node as a sibling of given parent
 * \param parent given parent node
 * \param text the sibling's text
 * \return a pointer to the new sibling
 */
xmlNodePtr addTextSibling(xmlNodePtr parent, char *text) {

    xmlNodePtr temp;

    temp = xmlNewText((const xmlChar*)text);
    temp = xmlAddNextSibling(parent, temp);
    return temp;
}


/**
 * \brief convert an integer to an xmlChar.
 *        If the integer is negative, return an empty string
 * \param arg the integer to be converted
 * \return the equivalent in xmlChar*
 */
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


/**
 * \brief create a <stage> xml node with its members as a child of given parent xml node
 * \param stage stage number to be processed
 * \param row members list (array of nodes ids as integers)
 * \param size number of members
 * \param parent xml node parent to this <stage> node
 * \param last equals to 1 if this is the last 'stage' node to create
 */
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


/**
 * \brief create an xml <node> node with all its stages and members, as a child of given parent root node
 * \param node_id node's id to be processed
 * \param parent xml parent root node to this <node> node
 * \param last equals to one if this is the last 'node' node to create
 * \param routing_table node's complete routing table
 * \param height number of stages
 * \param row_sizes array of number of members per stage
 */
void nodeToXml(int node_id, xmlNodePtr parent, int last, int **routing_table, int height, int *row_sizes) {

    // <node> node
    xmlChar *xid = itox(node_id);
    xmlNodePtr nptrNode = xmlNewTextChild(parent, NULL, (const xmlChar*)"node", NULL);
    xmlNewProp(nptrNode, (const xmlChar*)"id", xid);
    addTextChild(nptrNode, "\n        ");

    if (last == 1) {
        addTextSibling(nptrNode, "\n");
    } else {
        addTextSibling(nptrNode, "\n    ");
    }

    // <stage> nodes
    int stage;
    for (stage = 0; stage < height; stage++) {

        stageToXml(stage, routing_table[stage], row_sizes[stage], nptrNode, stage == height - 1);
    }
}


/**
 * \brief create a root xml node into a given xml document
 * \param a 'a' parameter of dst
 * \param b 'b' parameter of dst
 * \param height number of stages of dst
 * \param doc given xml document
 * \return a pointer to this new root node
 */
xmlNodePtr rootToXml(int a, int b, int height, xmlDocPtr doc) {

    // conversions
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
