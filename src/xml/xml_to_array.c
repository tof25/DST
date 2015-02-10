#include "xml_to_array.h"

/**
 * \brief parse xml document
 * \param docname the document name
 * \return a pointer to the document
 */
xmlDocPtr getdoc (const char *docname) {

    xmlDocPtr doc;
    doc = xmlParseFile(docname);

    if (doc == NULL ) {
        fprintf(stderr,"Document not parsed successfully. \n");
        return NULL;
    }

    return doc;
}


/**
 * \brief return a node set for a given xpath expression and a context node
 * \param doc the document for the global context
 * \param ctxNode the context node (if NULL, evaluate xpath expression in the global context)
 * \param xpath the xpath expression
 * \return the object containing the result node set (has to be freed by the caller)
 */
xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlNodePtr ctxNode, char *xpath){

    xmlXPathContextPtr context = NULL;
    xmlXPathObjectPtr obj_res = NULL;

    // build context
    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        printf("[%s:%d] Error in xmlXPathNewContext\n", __FUNCTION__, __LINE__);
        return NULL;
    }

    // build node set
    if (ctxNode != NULL) {

        obj_res = xmlXPathNodeEval(ctxNode, (xmlChar*)xpath, context);
    } else {

        obj_res = xmlXPathEvalExpression((xmlChar*)xpath, context);
    }
    xmlXPathFreeContext(context);

    // errors processing
    if (obj_res->nodesetval == NULL) {

        xmlXPathFreeObject(obj_res);
        printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }

    if (xmlXPathNodeSetIsEmpty(obj_res->nodesetval)) {

        xmlXPathFreeObject(obj_res);
        printf("No result\n");
        return NULL;
    }

    return obj_res;
}


/**
 * \brief reads a node's given attribute and returns it as a string
 * \param cur a pointer to the given node
 * \para prop the name of the attribute to read
 * \return the attribute as a string
 */
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


/**
 * \brief same as getProp but return attribute as an integer
 * \param cur a pointer to the given node
 * \para prop the name of the attribute to read
 * \return the attribute as as integer
 */
int getIntProp(xmlNodePtr cur, char *prop) {

    int ret = -1;

    // reads attribute
    char *buf = getProp(cur, prop);

    // converts it into integer
    if (buf != NULL) {

        ret = (!strcmp(buf, "") ? -1 : atoi(buf));
        free(buf);
    }

    return ret;
}

/**
 * \brief read node->stages->members and return a pointer to a structure containing them
 * \param doc the document to search in (global context)
 * \param xpath xpath expression to get the node to parse (only one node)
 * \return structure containing the noode and its table
 */
node_id_t getMembers(xmlDocPtr doc, char *xpath) {

    xmlNodeSetPtr nodeset, stageset, memberset;
    xmlXPathObjectPtr objNode, objStage, objMember;
    xmlNodePtr curNode;
    int stage, member;
    node_id_t node_table = NULL;

    // get nodes
    objNode = getnodeset(doc, NULL, xpath);

    if (objNode) {

        nodeset = objNode->nodesetval;
        // stops if xpath expression returns more than one node
        if (nodeset->nodeNr > 1) {

            printf("[%s:%d] : Only one node at a time !\n", __FUNCTION__, __LINE__);
            return NULL;
        }

        node_table = malloc(sizeof(s_node_id_t));

        // get node
        curNode = nodeset->nodeTab[0];

        // get node's id
        node_table->id = getIntProp(curNode, "id");

        // get node's stages
        objStage = getnodeset(doc, curNode, "stage");

        if (objStage) {

            stageset = objStage->nodesetval;
            node_table->routing_table = malloc(stageset->nodeNr * sizeof(int*));
            node_table->sizes = malloc(stageset->nodeNr * sizeof(int));

            for (stage = 0; stage < stageset->nodeNr; stage++) {

                // get stage's members
                objMember = getnodeset(doc, stageset->nodeTab[stage], "member");

                if (objMember) {

                    memberset = objMember->nodesetval;
                    node_table->routing_table[stage] = malloc(memberset->nodeNr * sizeof(int*));

                    for (member = 0; member < memberset->nodeNr; member++) {

                        node_table->routing_table[stage][member] = getIntProp(memberset->nodeTab[member], "value");
                    }
                    node_table->sizes[stage] = memberset->nodeNr;

                    xmlXPathFreeObject(objMember);
                } else {

                    free (node_table->routing_table);
                    node_table->routing_table = NULL;
                    free(node_table->sizes);
                    node_table->sizes = NULL;
                    free(node_table);
                    node_table = NULL;
                }
            }
            xmlXPathFreeObject(objStage);
        }
        xmlXPathFreeObject(objNode);
    }

    return(node_table);
}
