#include "dst.h"

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

void action_send(const char *const *action) {
    XBT_IN();

    while (nb_ins_nodes < nb_nodes) {
        MSG_process_sleep(1000.0);
    }

    XBT_INFO("%s: Action_%s - Sending '%s' to node %s",
            action[0],              // process name
            action[1],              // action name
            action[2],              // task type
            action[3]);             // recipient id

    u_req_args_t req_args;
    ans_data_t answer_data = NULL;
    msg_task_t task = NULL;

    xbt_assert(string2enum(action[2]) > -1, "%s: Action_%s - Task Type Error (%s)",
            action[0],
            action[1],
            action[2]);

    e_task_type_t task_type = string2enum(action[2]);

    req_data_t req_data = xbt_new0(s_req_data_t, 1);

    req_data->type = task_type;
    req_data->sender_id = -1;
    req_data->recipient_id = atoi(action[3]);
    set_mailbox(req_data->recipient_id, req_data->sent_to);
    snprintf(req_data->answer_to, MAILBOX_NAME_SIZE, "%s", action[0]);

    switch (task_type) {

        case TASK_GET_SIZE:
            req_args.get_size.new_node_id = -1;
            req_args.get_size.stage = atoi(action[4]);
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
                    action[3],
                    action[4],
                    (answer_data->answer).get_size.size);
            break;

        default:
            XBT_INFO("%s: Action_%s - Unknown action",
                    action[0],
                    action[1]);
    }
    MSG_task_destroy(task);

    XBT_OUT();
}

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

