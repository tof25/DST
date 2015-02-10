#include "xml_create_writer.h"

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
 * \brief To start the xml file
 * \param writer the writer object to write to
 * \param a the a variable
 * \param b the b variable
 * \param height dst number of stages
 */
void xmlHeader(xmlTextWriterPtr writer, int a, int b, int height) {

    // conversions
    xmlChar *xa, *xb, *xheight;
    xa = itox(a);
    xb = itox(b);
    xheight = itox(height);

    xmlTextWriterSetIndentString(writer, (xmlChar*)"  ");
    xmlTextWriterSetIndent(writer, 1);

    xmlTextWriterStartElement(writer, (xmlChar*)"dst");
        xmlTextWriterWriteAttribute(writer, (xmlChar*)"a", xa);
        xmlTextWriterWriteAttribute(writer, (xmlChar*)"b", xb);
        xmlTextWriterWriteAttribute(writer, (xmlChar*)"height", xheight);

    free(xa);
    free(xb);
    free(xheight);
}

/**
 * \brief To end the xml file
 * \param writer the writer object to write to
 */
void xmlFooter(xmlTextWriterPtr writer) {

    xmlTextWriterEndElement(writer);
    xmlFreeTextWriter(writer);
}

/**
 * \brief write a <stage> node with its members
 * \param writer the writer object to write to
 * \param stage_nr current stage number
 * \param row array of stage members
 * \param row_size number of members in the array
 */
void stageToXml(xmlTextWriterPtr writer, int stage_nr, int* row, int row_size) {

    xmlChar *xStage = itox(stage_nr);

    xmlTextWriterSetIndentString(writer, (xmlChar*)"  ");
    xmlTextWriterSetIndent(writer, 1);

    xmlTextWriterStartElement(writer, (xmlChar*)"stage");

    xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", xStage);

    xmlTextWriterSetIndent(writer, 0);
    int idx;
    xmlChar *xMember;

    for (idx = 0; idx < row_size; idx++) {

        xMember = itox(row[idx]);

        xmlTextWriterStartElement(writer, (xmlChar*)"member");
        xmlTextWriterWriteAttribute(writer, (xmlChar*)"value", xMember);
        xmlTextWriterEndElement(writer);

        free(xMember);
    }

    xmlTextWriterSetIndentString(writer, (xmlChar*)"");
    xmlTextWriterSetIndent(writer, 1);

    xmlTextWriterEndElement(writer);    //stage

    free(xStage);
}


/**
 * \brief write a <node> node with his stage members
 * \param writer the writer object to write to
 * \param node_id the id of the node to write
 * \param table the routing or predecessors table
 * \param row_size array of each row size
 * \param height number of stages
 */
void nodeToXml(xmlTextWriterPtr writer, int node_id, int **table, int *row_size, int height) {

    int stage;
    xmlChar *xNode_id = itox(node_id);

    xmlTextWriterSetIndentString(writer, (xmlChar*)"  ");
    xmlTextWriterSetIndent(writer, 1);

    xmlTextWriterStartElement(writer, (xmlChar*)"node");
        xmlTextWriterWriteAttribute(writer, (xmlChar*)"id", xNode_id);

        for (stage = 0; stage < height; stage++) {

            stageToXml(writer, stage, table[stage], row_size[stage]);
        }

        xmlTextWriterSetIndentString(writer, (xmlChar*)"  ");
    xmlTextWriterEndElement(writer);        //node

    free(xNode_id);
}
