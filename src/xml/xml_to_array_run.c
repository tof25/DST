#include "xml_to_array.h"

int main(int argc, char **argv) {

    char *docname;
    xmlDocPtr doc;
    int ret = 0;
    char *a, *b, *height, *id;
    xmlNodePtr cur;
    const int LenXPATH = 25;
    char xpath[LenXPATH] = "//node";

    node_id_t node_table = malloc(sizeof(s_node_id_t));
    node_table->id = -1;
    node_table->routing_table = NULL;

    // usage warning
    if (argc <= 1) {
        printf("Usage: %s docname node_id\n", argv[0]);
        return(0);
    }

    if (argc == 3) {

        id = argv[2];
    } else {

        id = NULL;
    }

    // parse document
    docname = argv[1];
    doc = getdoc(docname);
    cur = xmlDocGetRootElement(doc);

    // error : empty document
    if (cur == NULL) {
        fprintf(stderr,"empty document\n");
        xmlFreeDoc(doc);
        return(0);
    }

    // error : wrong document
    if (xmlStrcmp(cur->name, (const xmlChar *) "dst")) {
        fprintf(stderr,"document of the wrong type, root node = %s", cur->name);
        xmlFreeDoc(doc);
        return(0);
    }

    // gets dst attributes
    a = getProp(cur, "a");
    b = getProp(cur, "b");
    height = getProp(cur, "height");

    // error : wrong dst attributes
    if (a == NULL || b == NULL || height == NULL) return(0);

    printf("DST attributes : a = %s - b = %s - height = %s\n\n", a, b, height);

    // print members
    if (id != NULL) {
        char buf[LenXPATH];
        sprintf(buf, "[@id=%s]", id);
        strncat(xpath, buf, LenXPATH - strlen(buf)) ;
    }

    node_table = getMembers(doc, xpath);

    if (node_table) {
        int i, j;
        printf("Node %d:\n", node_table->id);
        for (i = 0; i < atoi(height); i++) {
            for (j = 0; j < atoi(b); j++) {
                printf("\t%d", node_table->routing_table[i][j]);
            }
            free(node_table->routing_table[i]);
            printf("\n");
        }
        free(node_table->routing_table);
        free(node_table);
    } else {
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();
    return (ret);
}
