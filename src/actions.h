#ifndef ACTIONS_H
#define ACTIONS_H


/*
   =================================== For XML files ==============================================
*/
char     *xml_input_file;                // name of the optionnal xml input file (for routing tables)
char     *xml_input_pred_file;           // name of the optionnal xml input file (for preds tables)
char     *xml_output_file;               // name of the xml output file (for routing tables)
char     *xml_output_pred_file;          // name of the xml output pred file (for preds tables)
xmlDocPtr doc_i;                         // pointer to the parsed xml input file for routing tables
xmlDocPtr doc_i_pred;                    // pointer to the parsed xml input file for preds tables
int       xml_height;                    // dst height read from xml input file

//============================= UTILITY FINCTIONS ===============================
static int string2enum(const char *in_str);

 //=========================== ACTIONS FUNCTIONS ================================
static void action_send(const char *const *action);
static void action_node(const char *const *action);
static void action_finalize(const char *const *action);

//============================ XML FUNCTIONS ====================================
       xmlDocPtr    get_xml_input_file(const char *doc_name);
static char*        filename(const char *file_name);

#endif
