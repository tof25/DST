#include "actions.h"
#include "dst.h"

XBT_LOG_EXTERNAL_DEFAULT_CATEGORY(msg_dst);

// ================================ UTILITY FUNCTIONS =============================================
/**
 * \brief convert a string to an enum task type value
 * \param in_str the string to convert (task name)
 * \return the matching type value
 */
static int string2enum(const char *in_str) {
    if (0);
#define X(str, val, id) else if (0 == strcmp(in_str, str)) return val;
        X_TASK_TYPE_LIST
#undef X
        return -1;
}

// ================================= ACTIONS FUNCTIONS ============================================

/**
 * \brief Sends a task to some dst node. May receive an answer, depending on the sent task.
 * \param action[0] current process name
 * \param action[1] current action name
 * \param action[2] start time (won't start before this time)
 * \param action[3] task type (must exist in dst enum e_task_type_t)
 * \param action[4] id of the recipient
 * \param action[...] task arguments
 */
void action_send(const char *const *action) {
    XBT_IN();

    if (atof(action[2]) > 0.0) {

        XBT_INFO("%s: Action_%s - Waiting %s ...",
                action[0],
                action[1],
                action[2]);

        // wait for starting point
        MSG_process_sleep(atof(action[2]));
    }

    XBT_INFO("%s: Action_%s - Time to work !",
            action[0],
            action[1]);

    // wait until dst building is finished
    /*
    while (nb_ins_nodes < nb_nodes) {
        MSG_process_sleep(500.0);
    }
    */

    XBT_INFO("%s: Action_%s - Sending '%s' to node %s",
            action[0],              // process name
            action[1],              // action name
            action[3],              // task type
            action[4]);             // recipient id

    int loop = 0;
    e_val_ret_t ret = OK;
    u_req_args_t req_args;
    ans_data_t answer_data = NULL;
    msg_task_t task = NULL;

    xbt_assert(string2enum(action[3]) > -1, "%s: Action_%s - Task Type Error (%s)",
            action[0],
            action[1],
            action[3]);

    // get task type
    e_task_type_t task_type = string2enum(action[3]);

    req_data_t req_data = xbt_new0(s_req_data_t, 1);

    // fill request data
    req_data->type = task_type;
    req_data->sender_id = -1;
    req_data->recipient_id = atoi(action[4]);
    set_mailbox(req_data->recipient_id, req_data->sent_to);
    snprintf(req_data->answer_to, MAILBOX_NAME_SIZE, "%s", action[0]);

    switch (task_type) {

        case TASK_GET_SIZE:
            req_args.get_size.new_node_id = -1;
            req_args.get_size.stage = atoi(action[5]);
            req_data->args = req_args;

            XBT_INFO("%s: Action_%s - Sending ...",
                    action[0],
                    action[1]);

            MSG_task_send(MSG_task_create("ext", COMP_SIZE, COMM_SIZE, req_data), req_data->sent_to);

            XBT_INFO("%s: Action_%s - Receiving ...",
                    action[0],
                    action[1]);

            MSG_task_receive(&task, action[0]);
            xbt_assert(task != NULL, "%s: Action_%s - Receive Error", action[0], action[1]);

            answer_data = MSG_task_get_data(task);
            xbt_assert(answer_data != NULL, "%s: Action_%s - Get Data Error", action[0], action[1]);

            XBT_INFO("%s: Action_%s - Node %s stage %s size = %d",
                    action[0],
                    action[1],
                    action[4],
                    action[5],
                    (answer_data->answer).get_size.size);
            break;

        case TASK_SEARCH:
            req_data->sender_id = 125000;
            req_args.search.source_id = req_data->sender_id;
            req_args.search.item = action[5];
            req_data->args = req_args;

            XBT_INFO("%s: Action_%s - Sending ...",
                    action[0],
                    action[1]);

            MSG_task_send(MSG_task_create("ext", COMP_SIZE, COMM_SIZE, req_data), req_data->sent_to);
            XBT_INFO("%s: Action_%s - Receiving ...",
                    action[0],
                    action[1]);

            MSG_task_receive(&task, action[0]);
            xbt_assert(task != NULL, "%s: Action_%s - Receive Error", action[0], action[1]);

            answer_data = MSG_task_get_data(task);
            xbt_assert(answer_data != NULL, "%s: Action_%s - Get Data Error", action[0], action[1]);

            XBT_INFO("%s: Action_%s - item '%s' %s in node %s (%d)",
                    action[0],
                    action[1],
                    action[5],
                    debug_ret_msg[(answer_data->answer).search.search_ret],
                    req_data->sent_to,
                    (answer_data->answer).search.s_ret_id);
            break;

        case TASK_BROADCAST_SEARCH:

            do {
                loop++;

                req_data->sender_id = 125000;
                req_args.broad_search.source_id = req_data->sender_id;
                req_args.broad_search.item = action[5];
                req_data->args = req_args;

                XBT_INFO("%s: Action_%s - Sending ... (attempt %d)",
                        action[0],
                        action[1],
                        loop);

                MSG_task_send(MSG_task_create("ext", COMP_SIZE, COMM_SIZE, req_data), req_data->sent_to);
                XBT_INFO("%s: Action_%s - Receiving ... (attempt %d)",
                        action[0],
                        action[1],
                        loop);

                MSG_task_receive(&task, action[0]);
                xbt_assert(task != NULL, "%s: Action_%s - Receive Error", action[0], action[1]);

                answer_data = MSG_task_get_data(task);
                xbt_assert(answer_data != NULL, "%s: Action_%s - Get Data Error", action[0], action[1]);

                ret = (answer_data->answer).handle.val_ret;
                XBT_INFO("%s: Action_%s - DONE with ret = %s (attempt %d)",
                        action[0],
                        action[1],
                        debug_ret_msg[ret],
                        loop);

                if (ret != OK) {
                    MSG_process_sleep(500.0);
                    task_free(&task);
                    req_data = xbt_new0(s_req_data_t, 1);
                    req_data->type = TASK_BROADCAST_SEARCH;
                    req_data->recipient_id = atoi(action[4]);
                    set_mailbox(req_data->recipient_id, req_data->sent_to);
                    snprintf(req_data->answer_to, MAILBOX_NAME_SIZE, "%s", action[0]);
                }

            } while (ret != OK && loop < 5);

            break;

        default:
            XBT_INFO("%s: Action_%s - Unknown action",
                    action[0],
                    action[1]);
    }
    MSG_task_destroy(task);

    XBT_OUT();
}

/**
 * \brief Ends the simulation
 * \param action[0] current process name
 * \param action[1] current action name
 * \param action[2] start time (won't start before this time)
 */
void action_finalize(const char *const *action) {
    XBT_IN();

    float sleep_time = action[2] == NULL ? 0: atof(action[2]);

    XBT_INFO("%s: Action_%s - sleep: %f",
            action[0],
            action[1],
            sleep_time);

    while (nb_ins_nodes < nb_nodes) {
        MSG_process_sleep(sleep_time);
    }
    finished = 1;

    XBT_INFO("%s: finished = %d - nb_ins_nodes = %d", action[0], finished, nb_ins_nodes);

    XBT_OUT();
}

/**
 * \brief Gets inserted into dst (merely call the dst 'node' function)
 * \param action[0] current process name
 * \param action[1] current action name
 * \param action[2] contact id (a working member of dst)
 * \param action[3] current node's id
 * \param action[4] start time (won't start before this time)
 * \param action[5] deadline (will leave the dst at this time)
 */
void action_node(const char *const *action) {
    XBT_IN();

    XBT_INFO("%s: Action_%s", action[0], action[1]);
    int i = 0;
    int argv = 5;       // number of arguments
    char **args;

    args = xbt_new0(char*, argv);
    for (i = 0; i < argv; i++) {
        args[i] = xbt_new0(char, 20);
    }

    // remove action[1] from actions array (function name)
    // so that node will be called with proper arguments
    args[0] = (char*)action[0];

    for (i = 1; i < argv; i++) {
        args[i] = (char*)action[i+1];
    }

    node(argv, args);

    /*
    for (i = 0; i < argv; i++) {
        xbt_free(args[i]);
    }
    xbt_free(args);
    */

    XBT_OUT();
}

//======================================== XML & FILES FUNCTIONS ==================================
// global values
char *xml_input_file = NULL;
char *xml_input_pred_file = NULL;
char *xml_output_file = NULL;
char *xml_output_pred_file = NULL;
xmlDocPtr doc_i = NULL;
xmlDocPtr doc_i_pred = NULL;
int xml_height = -1;

/**
 * \brief Try to open the given xml input file.
 * \param doc_name the file name to open
 * \return a pointer to the document, NULL if failed
 */
xmlDocPtr get_xml_input_file(const char *doc_name) {

    xmlDocPtr doc;
    xmlNodePtr cur;
    int int_a, int_b, int_height;

    // process all failure cases
    if (doc_name == NULL) {

        XBT_WARN("[%s:%d] xml input file is NULL", __FUNCTION__, __LINE__);
        return NULL;
    }

    doc = getdoc(doc_name);
    if (doc == NULL) {

        XBT_WARN("[%s:%d] Document %s not parsed successfully", __FUNCTION__, __LINE__, doc_name);
        return NULL;
    }

    cur = xmlDocGetRootElement(doc);
    if (cur == NULL) {

        XBT_WARN("[%s:%d] Document %s is empty", __FUNCTION__, __LINE__, doc_name);
        return NULL;
    }

    if (xmlStrcmp(cur->name, (const xmlChar *)"dst")) {

        XBT_WARN("[%s:%d] %s : wrong type of document", __FUNCTION__, __LINE__, doc_name);
        return NULL;
    }

    int_a = getIntProp(cur, "a");
    int_b = getIntProp(cur, "b");
    int_height = getIntProp(cur, "height");
    if (int_a == -1 || int_b == -1 || int_height == -1) {

        XBT_WARN("[%s:%d] 'a', 'b' or 'height' attributes aren't well formed in document %s",
                __FUNCTION__,
                __LINE__,
                doc_name);
        return NULL;
    }

    if (int_a != a || int_b != b) {

        XBT_WARN("[%s:%d] 'a' and 'b' attributes in document %s doesn't match current ones",
                __FUNCTION__,
                __LINE__,
                doc_name);
        return NULL;
    }

    // success
    xml_height = int_height;

    return doc;
}

/**
 * \brief return file_name without suffix
 * \param file_name the full file name
 * \return name of file without suffix
 */
static char* filename(const char *file_name) {

    char *output_filename;

    int indexOf = strrchr(file_name, '.') - file_name + 1;

    if (indexOf >= 0) {

        output_filename = xbt_new0(char, indexOf);
        snprintf(output_filename, indexOf, "%s", file_name);
    } else {

        output_filename = (char*)file_name;
    }

    return output_filename;
}

//==================================== MAIN PROGRAM ================================================

/**
 * \brief Main function
 */
int main(int argc, char *argv[]) {

    XBT_IN();

    if (argc < 3) {

        printf("Usage: %s --log=msg_dst.thres:info platform_file.xml deployment_file.xml xml_output_file.xml [xml_input_file.xml] trace_file.txt"
                " 2>&1 | tools/MSG_visualization/colorize.pl\n",
                argv[0]);
        exit(1);
    }

    int i;

    /*
       g_cpt = xbt_new0(int, TYPE_NBR);
       for (i = 0; i < TYPE_NBR; i++) {

       g_cpt[i] = 0;
       }
    */

    if (b != 2 * a) {

        printf("Bounds error : b should be twice a : a = %d, b = %d", a, b);
        exit(1);
    }

    xbt_log_control_set("msg_dst.thres:TRACE");

    MSG_init(&argc, argv);
    MSG_action_init();

    infos_dst = xbt_dynar_new(sizeof(dst_infos_t), &elem_free);

    // init array of failed nodes
    failed_nodes = xbt_new0(s_f_node_t, 1);

    // create timer
    xbt_os_timer_t timer = xbt_os_timer_new();

    // get platform and application files names
    const char *platform_file = argv[1];
    const char *deployment_file = argv[2];
    const char *actions_file = NULL;


    /****** PROCESS XML INPUT AND OUTPUT FILES ******/

    // xml output files
    if (argc >= 4) {

        xml_output_file = xbt_new0(char, FILENAME_MAX);
        xml_output_pred_file = xbt_new0(char, FILENAME_MAX);

        snprintf(xml_output_file, FILENAME_MAX, "%s", argv[3]);
        snprintf(xml_output_pred_file, FILENAME_MAX, "%s", "pred_");
        strncat(xml_output_pred_file, xml_output_file, FILENAME_MAX - strlen(xml_output_pred_file));
    }

    // xml input file ?
    if (argc >= 5) {

        // is the fourth argument an action file or an xml input file ?
        if (strstr(argv[4], ".txt")) {

            // actions file
            actions_file = argv[4];
        } else {

            //  xml input files
            xml_input_file = xbt_new0(char, FILENAME_MAX);
            xml_input_pred_file = xbt_new0(char, FILENAME_MAX);

            snprintf(xml_input_file, FILENAME_MAX, "%s", argv[4]);

            snprintf(xml_input_pred_file, FILENAME_MAX, "%s", "pred_");
            strncat(xml_input_pred_file, xml_input_file, FILENAME_MAX - strlen(xml_input_pred_file));

            // output and input files mustn't be the same
            xbt_assert(strcmp(xml_input_file, xml_output_file) != 0,
                    "[%s:%d] xml output files and input files can't have the same name !",
                    __FUNCTION__,
                    __LINE__);
        }
    }

    // parse xml input files
    if (xml_input_file != NULL) {

        doc_i = get_xml_input_file(xml_input_file);
        doc_i_pred = get_xml_input_file(xml_input_pred_file);

        // failed
        if (doc_i == NULL || doc_i_pred == NULL) {

            XBT_WARN("[%s:%d] Failed to parse XML input files", __FUNCTION__, __LINE__);

            xbt_free(xml_input_file);
            xbt_free(xml_input_pred_file);
            xml_input_file = NULL;
            xml_input_pred_file = NULL;

            return 1;
        }

        if (argc >= 6) {

            actions_file = argv[5];
        } else {

            XBT_WARN("[%s:%d] No action file have been provided", __FUNCTION__, __LINE__);
            return 1;
        }
    }

    nb_nodes = count_dst_nodes(deployment_file);
    XBT_INFO("START BUILDING A DST OF %d NODES", nb_nodes);

    MSG_create_environment(platform_file);

    MSG_function_register("node", node);
    MSG_launch_application(deployment_file);

    xbt_os_walltimer_start(timer);
    msg_error_t res;

    // actions file ?
    if (actions_file != NULL) {

        xbt_replay_action_register("node", action_node);
        xbt_replay_action_register("finalize", action_finalize);
        xbt_replay_action_register("send", action_send);

        res = MSG_action_trace_run((char*)actions_file);
        //res = MSG_action_trace_run("trace.txt");
    } else {
        finished = 1;           // will end when building is done
        res = MSG_main();
    }

    MSG_action_exit();
    xbt_os_walltimer_stop(timer);


    // print all routing tables
    XBT_INFO("************************************     PRINT ALL  (nb_ins_nodes = %d)    "
            "************************************\n", nb_ins_nodes);
    unsigned int cpt = 0, loc_nb_nodes = 0, loc_nb_nodes_tot = 0;

    // to store non active nodes id
    int size = 100;
    int non_active[size];
    for (i = 0; i < size; i++) {

        non_active[i] = -1;
    }
    size = 0;

    dst_infos_t elem;
    int tot_msg_nodes = 0;              // total number of messages for all nodes
    int tot_msg = 0;                    // number of messages for current node
    int nb_msg[TYPE_NBR] = {0};         // total number of messages per task type
    int nb_br_msg[TYPE_NBR] = {0};      // total number of messages per broadcasted task type
    int max_msg_nodes = -1;             // max number of messages for one node
    int max_node;                       // node that needed max number
    int tot_cs_req_fail = 0;            // total number of cs_req broadcasts failures
    int tot_cs_req_success = 0;         // total number of cs_req broadcasts sucess
    int tot_set_update_fail = 0;        // total number of set_update broadcasts failures
    int tot_set_update_success = 0;     // total number of set_update broadcasts sucess
    int tot_task_remove = 0;            // total number of task_remove broadcasts
    int tot_chg_contact = 0;            // total number of times contact have been changed

    /*
    // write it down to a file
    FILE *fp;
    fp = fopen("./log_nodes.txt", "w+");
    fprintf(fp, "Node, Nb Msg, ");
    for (i = 0; i < TYPE_NBR; i++) {

        fprintf(fp, "%s, ", debug_msg[i]);
    }
    fprintf(fp, "\n");
    */

    // prepare xml output files
    char header_done = 0;
    char header_pred_done = 0;

    xmlDocPtr doc_o = xmlNewDoc((const xmlChar*)"1.0");
    xmlTextWriterPtr writer = xmlNewTextWriterDoc(&doc_o, 0);

    xmlDocPtr doc_o_pred = xmlNewDoc((const xmlChar*)"1.0");
    xmlTextWriterPtr writer_pred = xmlNewTextWriterDoc(&doc_o_pred, 0);

    // display loop
    xbt_dynar_foreach(infos_dst, cpt, elem) {
        if (elem != NULL) {

            if (!header_done) {

                xmlHeader(writer, a, b, elem->height);
                header_done = 1;
            }

            if (!header_pred_done) {

                xmlHeader(writer_pred, a, b, elem->height);
                header_pred_done = 1;
            }

            // xml <node> and children
            nodeToXml(writer, elem->node_id, elem->brothers, elem->size, elem->height);
            nodeToXml(writer_pred, elem->node_id, elem->preds, elem->load, elem->height);

            loc_nb_nodes_tot++;

            tot_cs_req_fail += elem->nb_cs_req_fail;
            tot_cs_req_success += elem->nb_cs_req_success;
            tot_set_update_fail += elem->nb_set_update_fail;
            tot_set_update_success += elem->nb_set_update_success;
            tot_task_remove += elem->nb_task_remove;
            tot_chg_contact += elem->nb_chg_contact;

            // sum messages counters
            tot_msg = 0;
            for (i = 0; i < TYPE_NBR; i++) {

                nb_msg[i]    += nb_messages[elem->node_id][i];
                nb_br_msg[i] += nb_br_messages[elem->node_id][i];
                //tot_msg      += nb_messages[elem->node_id][i];
                tot_msg += (nb_messages[elem->node_id][i] + nb_br_messages[elem->node_id][i]);
            }

            /*
            fprintf(fp, "%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
                    elem->node_id,
                    tot_msg,
                    nb_messages[elem->node_id][0],
                    nb_messages[elem->node_id][1],
                    nb_messages[elem->node_id][2],
                    nb_messages[elem->node_id][3],
                    nb_messages[elem->node_id][4],
                    nb_messages[elem->node_id][5],
                    nb_messages[elem->node_id][6],
                    nb_messages[elem->node_id][7],
                    nb_messages[elem->node_id][8],
                    nb_messages[elem->node_id][9],
                    nb_messages[elem->node_id][10],
                    nb_messages[elem->node_id][11],
                    nb_messages[elem->node_id][12],
                    nb_messages[elem->node_id][13],
                    nb_messages[elem->node_id][14],
                    nb_messages[elem->node_id][15],
                    nb_messages[elem->node_id][16],
                    nb_messages[elem->node_id][17],
                    nb_messages[elem->node_id][18],
                    nb_messages[elem->node_id][19],
                    nb_messages[elem->node_id][20],
                    nb_messages[elem->node_id][21],
                    nb_messages[elem->node_id][22],
                    nb_messages[elem->node_id][23],
                    nb_messages[elem->node_id][24],
                    nb_messages[elem->node_id][25],
                    nb_messages[elem->node_id][26],
                    nb_messages[elem->node_id][27],
                    nb_messages[elem->node_id][28],
                    nb_messages[elem->node_id][29],
                    nb_messages[elem->node_id][30],
                    nb_messages[elem->node_id][31],
                    nb_messages[elem->node_id][32],
                    nb_messages[elem->node_id][33],
                    nb_messages[elem->node_id][34],
                    nb_messages[elem->node_id][35],
                    nb_messages[elem->node_id][36],
                    nb_messages[elem->node_id][37]);
            */

            if (elem->active == 'a') {

                loc_nb_nodes++;
                tot_msg_nodes += tot_msg;
                if (tot_msg > max_msg_nodes) {

                    max_msg_nodes = tot_msg;
                    max_node = elem->node_id;
                }

                XBT_INFO("[Node %d]: \n%s\nNeeded to split %d stages\nNeeded"
                        " %d messages to join\nNeeded %d attempt(s) to join\n%s\n",
                        elem->node_id,
                        (elem->add_stage == 0 ? "Didn't add any stage" :
                         "Added a stage"),
                        elem->nbr_split_stages,
                        tot_msg,
                        elem->attempts,
                        elem->routing_table);
            } else {

                XBT_INFO("[Node %d]: Non active\n", elem->node_id);
                non_active[size] = elem->node_id;
                size++;
                xbt_assert(size < 100, "[%s:%d] Non_active array too small !", __FUNCTION__, __LINE__);
            }
            xbt_free(elem->routing_table);
            elem->routing_table = NULL;

            int j;
            for (j = 0; j < elem->height; j++) {

                xbt_free(elem->brothers[j]);
                xbt_free(elem->preds[j]);
            }

            xbt_free(elem->brothers);
            xbt_free(elem->preds);
            xbt_free(elem->size);
            xbt_free(elem->load);

            xbt_free(elem);
            elem = NULL;

        } else {
            XBT_VERB("cpt: %d, elem = NULL", cpt);
        }
    }

    //fclose(fp);

    // save xml files
    xmlFooter(writer);
    xmlFooter(writer_pred);

    xmlSaveFormatFile(xml_output_file, doc_o, 0);
    xmlFreeDoc(doc_o);
    xmlSaveFormatFile(xml_output_pred_file, doc_o_pred, 0);
    xmlFreeDoc(doc_o_pred);

    XBT_INFO("Number of elements in infos_dst = %d", cpt);
    XBT_INFO("Messages needed for %d active nodes / %d total nodes ( sent - broadcasted )",
            loc_nb_nodes,
            loc_nb_nodes_tot);      //TODO : loc_nb_nodes_tot contient certainement la même valeur que cpt
    for (i = 0; i < TYPE_NBR; i++) {
        XBT_INFO("\t['%25s': %6d - %3d]",
                debug_msg[i],
                nb_msg[i],
                nb_br_msg[i]);
    }

    if (loc_nb_nodes != loc_nb_nodes_tot) {

        XBT_INFO(" ");
        i = 0;
        while (non_active[i] != -1) {

            XBT_INFO("Node %d non active", non_active[i]);
            i++;
        }
        XBT_INFO(" ");
    }

    XBT_INFO(" ");

    /*
       for (i = 0; i < TYPE_NBR; i++) {
       XBT_INFO("g_cpt[%s] = %d", debug_msg[i], g_cpt[i]);
       }
       XBT_INFO("");
       xbt_free(g_cpt);
    */

    XBT_INFO("Total number of BR_CS_REQ failures : %d", tot_cs_req_fail);
    XBT_INFO("Total number of BR_CS_REQ success : %d", tot_cs_req_success);
    XBT_INFO("Total number of BR_SET_UPDATE failures : %d", tot_set_update_fail);
    XBT_INFO("Total number of BR_SET_UPDATE success : %d", tot_set_update_success);
    XBT_INFO("Total number of BR_TASK_REMOVE : %d", tot_task_remove);
    XBT_INFO("Total number of contact changes : %d", tot_chg_contact);

    XBT_INFO("\nTotal number of messages: %d\n", tot_msg_nodes);
    XBT_INFO("Max messages needed by node %d: %d\n",
            max_node,
            max_msg_nodes);
    XBT_INFO("Total number of join abortions: %d - Details:", nb_abort);
    for (i = 0; i < nb_abort; i++) {

        XBT_INFO("\tnode %d failed at [%f]",
                failed_nodes[i].id,
                failed_nodes[i].f_time);
    }
    xbt_free(failed_nodes);

    // all elements have already been freed during foreach
    xbt_dynar_free_container(&infos_dst);

    XBT_INFO("\nSimulation time %lf", xbt_os_timer_elapsed(timer));
    XBT_INFO("Simulated time: %g", MSG_get_clock());

    xbt_os_timer_free(timer);

    xbt_free(xml_output_file);
    xbt_free(xml_output_pred_file);
    xbt_free(xml_input_file);

    //MSG_clean();
    XBT_OUT();

    if (res == MSG_OK)
        return 0;
    else
        return 1;
}
