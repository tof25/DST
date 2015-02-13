/*
 *  dst.h
 *
 *  Written by Christophe Enderlin on 2015/02/13
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "msg/msg.h"                        // to use MSG API of Simgrid
#include "xbt/log.h"                        // to get nice outputs
#include "xbt/asserts.h"                    // to use xbt_assert()
#include "xbt/xbt_os_time.h"                // to use a timer (located in Simgrid-3.9/src/include/xbt)
#include "xbt/ex.h"                         // to use exceptions
#include "xml/xml_create_writer.h"          // to create final xml files with routing tables
#include "xml/xml_to_array.h"               // to get routing and predecessors tables from xml files
#include <xbt/replay.h>

#ifndef DST_H
#define DST_H

XBT_LOG_NEW_DEFAULT_CATEGORY(msg_dst, "Messages specific for the DST");

/*
   ===============================  GLOBAL VALUES  ================================================
*/

#define COMM_SIZE 10                        // message size when creating a new task
#define COMP_SIZE 0                         // compute duration when creating a new task
#define MAILBOX_NAME_SIZE 50                // name size of a mailbox
#define TYPE_NBR 37                         // number of task types
#define MAX_WAIT_COMPL 20000                // won't wait longer for broadcast completion
#define MAX_WAIT_GET_REP 5000               // won't wait longer an answer to a GET_REP request
#define MAX_CNX 500                         // max number of attempts to run CNX_REQ (before trying another contact)
#define WAIT_BEFORE_END 2000                // Wait for last messages before ending simulation
#define LEN_XPATH 20                        // xpath size (for xml input file)
#define XML_NAME_SIZE 50                    // xml files names length


/*
   =================================== For XML files ==============================================
*/
static char     *xml_input_file = NULL;         // name of the optionnal xml input file (for routing tables)
static char     *xml_input_pred_file = NULL;    // name of the optionnal xml input file (for preds tables)
static char     *xml_output_file = NULL;        // name of the xml output file (for routing tables)
static char     *xml_output_pred_file = NULL;   // name of the xml output pred file (for preds tables)
static xmlDocPtr doc_i = NULL;                  // pointer to the parsed xml input file for routing tables
static xmlDocPtr doc_i_pred = NULL;             // pointer to the parsed xml input file for preds tables
static int       xml_height = -1;               // dst height read from xml input file

/*
   ================================================================================================
*/

static const int   a = 3;                                   // min number of brothers in a node (except for the root node)
static const int   b = 2 * a;                               // max number of brothers in a node (must be twice a)
static int         COMM_TIMEOUT = 19000;                    // timeout for communications (mustn't be greater than MAX_WAIT_COMPL)
static xbt_dynar_t infos_dst;                               // to store all global DST infos
static int         nb_messages[100000][TYPE_NBR] = {0};     // total number of messages exchanged for each task type per node
static int         nb_br_messages[100000][TYPE_NBR] = {0};  // total number of broadcasted messages exchanged for each task type per node
static int         order = 0;                               // order number of nodes arrival
static int         nb_nodes = 0;                            // total number of nodes to be inserted
static int         nb_ins_nodes = 0;                        // number of nodes actually inserted
static int         mem_log = -1;                            // ensures that log new setting occurs only once
static int         inserted_nodes[100000] = {-1};           // to store a list of currently inserted nodes

typedef struct f_node {                     // node that failed to join
    int   id;
    float f_time;
} s_f_node_t;

static int nb_abort = 0;                    // number of join abortions
static s_f_node_t *failed_nodes = NULL;     // array of nodes that couldn't join the DST


/**
 * A node representative
 */
typedef struct node_rep {

    int id;                                 // representative id
    char mailbox[MAILBOX_NAME_SIZE];        // representative main mailbox name
} s_node_rep_t, *node_rep_t;

/**
 * Per node infos about the DST (for reporting purposes)
 */
typedef struct s_dst_info {
    int   order;                            // arrival order
    int   node_id;                          // node id
    char  active;                           // node state
    char *routing_table;                    // string representation of a routing table
    int **brothers;                         // node's routing table (contains only ids)
    int **preds;                            // node's preds table (contains only ids)
    int   height;                           // number of stages
    int   attempts;                         // number of attempts to join the dst
    int   add_stage;                        // boolean: was it necessary to add a stage to insert this node ?
    int   nbr_split_stages;                 // number of splitted stages to make room for this node
    int  *load;                             // load table (number of predecessors per stage)
    int  *size;                             // number of brothers per stage
    int   nb_cs_req_fail;                   // number of failed BR_CS_REQ
    int   nb_cs_req_success;                // number of successful BR_CS_REQ
    int   nb_set_update_fail;               // number of failed BR_SET_UPDATE
    int   nb_set_update_success;            // number of sucessful BR_SET_UPDATE
    int   nb_task_remove;                   // number of BR_TASK_REMOVE
    int   nb_chg_contact;                   // number of contact changes
} s_dst_infos_t, *dst_infos_t;

/**
 * Types of request/answer tasks
 */
typedef enum {

    TASK_NULL,              // no task
    TASK_GET_REP,           // get a node representative
    TASK_CNX_REQ,           // get the DST ready to receive a new node
    TASK_NEW_BROTHER_RCV,   // insert a new node in a node's first stage
    TASK_SPLIT_REQ,         // request a node to split
    TASK_ADD_STAGE,         // add a new stage
    TASK_CNX_GROUPS,        // make a node connect to his newly splitted sons
    TASK_SPLIT,             // execute the splitting
    TASK_NB_PRED,           // get the number of predecessors of a node, for a given stage
    TASK_ADD_PRED,          // add a predecessor
    TASK_DEL_PRED,          // delete a predecessor
    TASK_BROADCAST,         // broadcast a message
    TASK_DISPLAY_VAR,       // display a remote node's variable (for debugging purposes)
    TASK_GET_SIZE,          // return the number of brothers of a given stage
    TASK_DEL_BRO,           // delete a brother in a given stage
    TASK_REPL_BRO,          // replace a brother by a new one
    TASK_MERGE,             // merge groups when a node leaves
    TASK_BROADCAST_MERGE,   // broadcast a merge task
    TASK_DEL_ROOT,          // delete the root when a merge occurs
    TASK_CLEAN_STAGE,       // clean useless nodes in a stage when a merge occured in lower ones
    TASK_MERGE_REQ,         // request merges tasks
    TASK_SET_ACTIVE,        // set 'active' state
    TASK_SET_UPDATE,        // set 'update' state
    TASK_SET_STATE,         // set any state
    TASK_IS_BROTHER,        // test if a given id is a brother of mine
    TASK_TRANSFER,          // transfer some nodes to another group
    //TASK_DEL_MEMBER,        // delete a part of current group during a transfer  // TODO : plus besoin d'une tâche ? (voir DEL_MEMBER_REQ)
    TASK_ADD_BRO_ARRAY,     // add an array of brothers
    TASK_SHIFT_BRO,         // shift brothers to let a new one come in              // TODO : tâche pas utile si on ne l'exécute qu'en local
    TASK_ADD_BRO,           // add a brother at a given stage
    TASK_CUT_NODE,          // cut node during a transfer
    TASK_BR_ADD_BRO_ARRAY,  // broadcast an add_bro_array task
    TASK_UPDATE_UPPER_STAGE,// update upper stage after a transfer
    //TASK_GET_NEW_CONTACT,   // get a new contact for a new coming node that failed to join too much   // NOTE : mécanisme remplacé, plus utile
    TASK_CS_REQ,            // ask for permission to get into Critical Section (splitted area)
    TASK_END_GET_REP,       // remove 'g' state after load balance
    TASK_REMOVE_STATE,      // remove given state from states dynar
    TASK_CS_REL,            // reset cs_req flag
    TASK_IRQ                // requests permission from initiator to interrupt a CS_REQ broadcast
} e_task_type_t;

/**
 * Values returned by handle_task()
 */
typedef enum {
    OK,                     // no problem
    STORED,                 // task stored
    UPDATE_OK,              // set_update ok
    UPDATE_NOK,             // set_update not ok
    FAILED                  // task failed
} e_val_ret_t;

/**
 * Possible values for task run state
 */
typedef enum {
    RUNNING,
    IDLE
} e_run_state_t;

/**
 * Possible values for running task name
 */
typedef enum {
    NONE,
    OTHER,
    CS_REQ,
    SET_UPD
} e_name_run_t;


typedef struct ans_data s_ans_data_t, *ans_data_t;


/**
 * A recipient answer record                // see wait_for_completion()
 */
typedef struct recp_rec {

    e_task_type_t   type;                   // type of expected answer ...
    s_node_rep_t    recp;                   // ... from this recipient
    int             new_node_id;            // concern this new coming node
    e_task_type_t   br_type;                // type of broadcasted task
    ans_data_t      answer_data;            // answered data
} s_recp_rec_t, *recp_rec_t;

/**
 * Node state data
 */
typedef struct state {
    char active;        /* active code: active, build, update, load_balance
                           get_rep, pred waiting, non active
                           ('a', 'b', 'u', 'l', 'g', 'p'  or 'n') */
    int  new_id;        // new coming node id that caused the last state change
} s_state_t, *state_t;

/**
 * Node last run task state
 */
typedef struct {
    e_run_state_t run_state;
    e_val_ret_t   last_ret;
    e_name_run_t  name;
} s_run_task_t;

/**
 * Node data
 **/
typedef struct node {

    s_node_rep_t    self;                   // node's id and mailbox
    s_node_rep_t    **brothers;             // routing table of size b * height
    s_node_rep_t    **preds;                // predecessors table
    int             *pred_index;            // for each stage, the last predecessor index
    int             *bro_index;             // for each stage, the last brother index
    int             height;                 // height of the DST
    msg_comm_t      comm_received;          // current communication
    double          deadline;               // time to leave the DST
    xbt_dynar_t     states;                 // node states dynar
    s_dst_infos_t   dst_infos;              // infos about the DST
    xbt_dynar_t     tasks_queue;            // CNX_REQ tasks queue
    xbt_dynar_t     delayed_tasks;          // delayed tasks dynar
    int             prio;                   // priority to get into critical section (the lower the value, the higher the priority)
    char            cs_req;                 // Critical Section requested (boolean)
    float           cs_req_time;            // timestamp when cs_req was set
    int             cs_new_id;              // new node's id that set cs_req
    int             cs_new_node_prio;       // new node's priority
    int             cs_req_br_source;       // node's id that started broadcast of cs_req
    char            cs_irq_ans;             // answer given to an interrupt request (boolean)
    s_run_task_t    run_task;               // current running task state (delayed tasks)
} s_node_t, *node_t;

/**
 * son process data
 */
typedef struct proc_data {
    char         proc_mailbox[MAILBOX_NAME_SIZE];
    node_t       node;
    msg_task_t   task;
    xbt_dynar_t  async_answers;
    xbt_dynar_t  sync_answers;
} s_proc_data_t, *proc_data_t;

/**
 * Array of debug communication result messages
 */
static const char* debug_res_msg[] = {
    "MSG_OK",
    "MSG_TIMEOUT",
    "MSG_TRANSFER_FAILURE",
    "MSG_HOST_FAILURE",
    "MSG_TASK_CANCELED"
};

/**
 * Array of debug run state messages
 */
static const char* debug_run_msg[] = {
    "RUNNING",
    "IDLE"
};

/**
 * Array of debug messages for running task name
 */
static const char* debug_run_name_msg[] = {
    "NONE",
    "OTHER",
    "CS_REQ",
    "SET_UPD"
};

/**
 * Array of debug return messages
 */
static const char* debug_ret_msg[] = {
    "OK",
    "STORED",
    "UPDATE_OK",
    "UPDATE_NOK",
    "FAILED"
};

/**
 * Array of debug task messages
 */
static const char* debug_msg[] = {

    "...",
    "Get Representative",
    "Connexion Request",
    "New Brother Received",
    "Split Request",
    "Add Stage",
    "Connect Groups",
    "Split",
    "Number of Predecessors",
    "Add Predecessor",
    "Delete Predecessor",
    "Broadcast",
    "Display Var",
    "Get Size",
    "Delete Brother",
    "Replace Brother",
    "Merge",
    "Broadcast Merge",
    "Delete Root",
    "Clean Upper Stage",
    "Merge Request",
    "Set Active",
    "Set Update",
    "Set State",
    "Is Brother",
    "Transfer",
    //"Delete Member",
    "Add Brothers Array",
    "Shift Brothers",
    "Add Brother",
    "Cut Node",
    "Brd Add_Bro_Array",
    "Update Upper Stage",
    //"Get New Contact",
    "Critical Section Request",
    "End Get Rep",
    "Pop State",
    "Critical Section Released",
    "Interrupt Request"
};

/*
  =============================  REQUESTS ARGS  ===============================
*/

// IMPORTANT NOTE : new_node_id must be the first field of every structure

typedef struct {

    int new_node_id;
    int stage;
} s_task_get_rep_t;             // get a node representative

typedef struct {

    int new_node_id;
    int cs_new_node_prio;
    int try;
} s_task_cnx_req_t;             // get the DST ready to receive a new node

typedef struct {

    int new_node_id;
} s_task_new_brother_rcv_t;     // insert a new node in a node's first stage

typedef struct {

    int new_node_id;
    int stage_nbr;
} s_task_split_req_t;           // request a node to split

typedef struct {

    int new_node_id,
        stage,
        pos_init,
        pos_new,
        init_rep_id,
        new_rep_id;
} s_task_cnx_groups_t;          // make a node connect to his newly splitted sons

typedef struct {

    int new_node_id;
    int stage_nbr;
} s_task_split_t;               // execute the splitting

typedef struct {

    int new_node_id;
    int stage;
} s_task_nb_pred_t;             // get the number of predecessors of a node, given its stage

typedef struct {

    int new_node_id;
    int stage;
    int new_pred_id;
    int w_ans;
} s_task_add_pred_t;            // add a predessessor

typedef struct {

    int new_node_id;
    int stage;
    int pred2del_id;
} s_task_del_pred_t;            // delete a predecessor

typedef union req_args u_req_args_t, *req_args_t;

typedef struct {

    int           new_node_id;
    e_task_type_t type;
    int           stage;
    int           first_call;
    int           source_id;
    int           lead_br;
    req_args_t    args;
} s_task_broadcast_t;           // broadcast a message of type 'type'

typedef struct {

    int new_node_id;
    int stage;
} s_task_get_size_t;            // get the number of brothers on 'stage'

typedef struct {

    int new_node_id;
    int stage;
    int bro2del;
} s_task_del_bro_t;             // delete a brother for a given stage

typedef struct {

    int new_node_id;
    int stage;
    int new_id;
} s_task_repl_bro_t;            // replace brother by a new one

typedef struct {

    int new_node_id;
    int *nodes_array;
    int nodes_array_size;
    int stage;
    int pos_me;
    int pos_contact;
    int right;
} s_task_merge_t;               // execute a merging of groups

typedef struct {

    int new_node_id;
    int stage;
    int pos_me;
    int pos_contact;
    int right;
    int lead_br;
} s_task_broadcast_merge_t;     // broadcast a merge task

typedef struct {

    int new_node_id;
    int init_height;
} s_task_del_root_t;            // delete the root stage

typedef struct {

    int new_node_id;
} s_task_merge_req_t;

typedef struct {

    int new_node_id;
    int stage;
    int pos_me;
    int pos_contact;
} s_task_clean_stage_t;         // clean useless nodes in a stage when a merge occured in lower ones

typedef struct {

    int new_node_id;
} s_task_set_active_t;          // set node state as 'a' (active)

typedef struct {

    int new_node_id;
    int new_node_prio;
} s_task_set_update_t;          // set node state as 'u' (update)

typedef struct {
    int new_node_id;
    char state;
} s_task_set_state_t;           // set node state as given state

typedef struct {

    int new_node_id;
    int id;
} s_task_is_brother_t;          // check if id is a brother of mine

typedef struct {

    int new_node_id;
    int st;
    int right;
    int cut_pos;
    s_node_rep_t sender;
} s_task_transfer_t;            // transfer some nodes to another group

typedef struct {

    int new_node_id;
    int stage;
    int start;
    int end;
} s_task_del_member_t;          // delete a part of current group during a transfer

typedef struct {

    int        new_node_id;
    int        stage;
    node_rep_t bro;
    int        array_size;
    int        right;
} s_task_add_bro_array_t;       // add an array of brothers

typedef struct {

    int          new_node_id;
    int          stage;
    s_node_rep_t new_node;
    int          right;
} s_task_shift_bro_t;           // shift brothers to let a new one come in

typedef struct {

    int new_node_id;
    int stage;
    int new_id;
} s_task_add_bro_t;             // add a brother at a given stage

typedef struct {

    int new_node_id;
    int stage;
    int right;
    int cut_pos;
} s_task_cut_node_t;            // Cut node during a transfer

typedef struct {

    int        new_node_id;
    int        stage;
    node_rep_t bro;
    int        array_size;
    int        right;
} s_task_br_add_bro_array_t;    // broadcast an add_bro_array task

typedef struct {

    int new_node_id;
    int stage;
    int pos2repl;
    int new_id;
} s_task_update_upper_stage_t;  // update upper stage after a transfer

/*
typedef struct {

    int new_node_id;
} s_task_get_new_contact_t;    // get new contact (in case of too much failures)
*/

typedef struct {

    int new_node_id;
    int sender_id;
    int cs_new_node_prio;
} s_task_cs_req_t;             // ask permission to get into Critical Section

typedef struct {

    int new_node_id;
} s_task_end_get_rep_t;        // remove 'g' state after load balance

typedef struct {
    int new_node_id;
    char active;
} s_task_remove_state_t;       // remove given state from states dynar

typedef struct {
    int new_node_id;
} s_task_cs_rel_t;             // reset cs_req flag

typedef struct {
    int new_node_id;
} s_task_irq_t;                 // request permission to interrupt a broadcast of CS_REQ

/**
 * Generic request args
 */
union req_args {
    char                        fill[24];   // to allow direct assignment
    s_task_get_rep_t            get_rep;
    s_task_cnx_req_t            cnx_req;
    s_task_new_brother_rcv_t    new_brother_rcv;
    s_task_split_req_t          split_req;
    s_task_cnx_groups_t         cnx_groups;
    s_task_split_t              split;
    s_task_nb_pred_t            nb_pred;
    s_task_add_pred_t           add_pred;
    s_task_del_pred_t           del_pred;
    s_task_broadcast_t          broadcast;
    s_task_get_size_t           get_size;
    s_task_del_bro_t            del_bro;
    s_task_repl_bro_t           repl_bro;
    s_task_merge_t              merge;
    s_task_broadcast_merge_t    broad_merge;
    s_task_del_root_t           del_root;
    s_task_merge_req_t          merge_req;
    s_task_clean_stage_t        clean_stage;
    s_task_set_active_t         set_active;
    s_task_set_update_t         set_update;
    s_task_set_state_t          set_state;
    s_task_is_brother_t         is_brother;
    s_task_transfer_t           transfer;
    s_task_del_member_t         del_member;
    s_task_add_bro_array_t      add_bro_array;
    s_task_shift_bro_t          shift_bro;
    s_task_add_bro_t            add_bro;
    s_task_cut_node_t           cut_node;
    s_task_br_add_bro_array_t   br_add_bro_array;
    s_task_update_upper_stage_t update_upper_stage;
    //s_task_get_new_contact_t    get_new_contact;
    s_task_cs_req_t             cs_req;
    s_task_end_get_rep_t        end_get_rep;
    s_task_remove_state_t       remove_state;
    s_task_cs_rel_t             cs_rel;
    s_task_irq_t                irq;
};

/**
 * Generic request data type
 */
typedef struct req_data {

    e_task_type_t type;                                // type of request task
    int           sender_id;                           // sender id
    char          answer_to[MAILBOX_NAME_SIZE];        // mailbox for the answer
    int           recipient_id;                        // recipient id
    char          sent_to[MAILBOX_NAME_SIZE];          // recipient mailbox
    u_req_args_t args;                                 // request args

} s_req_data_t, *req_data_t;

/*
  ================================  ANSWERS ARGS =============================
*/

typedef struct {

    s_node_rep_t new_rep;
} s_task_ans_get_rep_t;

typedef struct {

    s_node_rep_t **cur_table;
    int *cur_index;
    s_node_rep_t **brothers, new_contact;
    int *bro_index;
    int height;
    e_val_ret_t val_ret;

    // for debug
    struct state contact_state;

    // for DST infos
    int add_stage;
    int nbr_split_stages;
    int try;
} s_task_ans_cnx_req_t;

typedef struct {

    int load;
} s_task_ans_nb_pred_t;

typedef struct {

    int size;
} s_task_ans_get_size_t;

typedef struct {

    e_val_ret_t   val_ret;
    e_task_type_t br_type;
    int           val_ret_id;
} s_task_ans_handle_t;

typedef struct {

    int rep;
} s_task_ans_is_brother_t;

typedef struct {

    node_rep_t rep_array;
    int        rep_array_size;
    int        stay_id;
} s_task_ans_transfer_t;

/*
typedef struct {

    int id;
} s_task_ans_get_new_contact_t;
*/

typedef struct {

    char ans;
} s_task_ans_irq_t;

/**
 * Generic answer values
 */
typedef union answer {

    char                            fill[48];       // to allow direct assignment
    s_task_ans_get_rep_t            get_rep;
    s_task_ans_cnx_req_t            cnx_req;
    s_task_ans_nb_pred_t            nb_pred;
    s_task_ans_get_size_t           get_size;
    s_task_ans_handle_t             handle;
    s_task_ans_is_brother_t         is_brother;
    s_task_ans_transfer_t           transfer;
    //s_task_ans_get_new_contact_t    get_new_contact;
    s_task_ans_irq_t                irq;
} u_ans_data_t;

/**
 * Generic answer data type
 */
struct ans_data {

    int           new_node_id;                 // concerned new coming node's id
    e_task_type_t br_type;                     // broadcasted task type (must match request one)
    e_task_type_t type;                        // answer type (must match request type)
    int           sender_id;                   // sender id
    char          sent_to[MAILBOX_NAME_SIZE];  // recipient mailbox
    int           recipient_id;                // recipient id
    u_ans_data_t  answer;                      // returned data
};

/*
  ==========================  UTILITY FUNCTIONS ===============================
*/

static int          count_lines_of_file(const char *file);
static void         display_var(node_t me);
static char*        routing_table(node_t me);
static void         set_n_store_infos(node_t me);
static void         display_preds(node_t me, char log);
static void         display_rout_table(node_t me, char log);
static void         set_mailbox(int node_id, char* mailbox);
static void         set_proc_mailbox(char* proc_mailbox);
static void         set_fork_mailbox(int node_id, int new_node_id, char* session, char* mailbox);
static void         task_free(msg_task_t* task);
static int          index_bro(node_t me, int stage, int id);
static int          index_pred(node_t me, int stage, int id);
static e_val_ret_t  wait_for_completion(node_t me, int ans_cpt, int new_node_id);
static void         make_copy_brothers(node_t me,
                                       s_node_rep_t ***cpy_brothers,
                                       int **cpy_bro_index);
static void         make_copy_preds(node_t me,
                                    s_node_rep_t ***cpy_preds,
                                    int **cpy_pred_index);
static node_rep_t   compare_tables(node_t me, s_node_rep_t ***table, int **table_index);
static void         make_broadcast_task(node_t me, u_req_args_t args, msg_task_t *task);
static s_state_t    get_state(node_t me);
static void         set_active(node_t me, int new_id);
static e_val_ret_t  set_update(node_t me, int new_id, int new_node_prio);
static void         set_state(node_t me, int new_id, char active);
static void         remove_state(node_t me, int new_id, char active);
static int          check(node_t me);
static void         call_run_tasks_queue(node_t me, int new_id);
static void         run_delayed_tasks(node_t me);
static void         run_tasks_queue(node_t me);
static void         node_free(node_t me);
static void         data_ans_free(node_t me, ans_data_t *answer_data);
static void         data_req_free(node_t me, req_data_t *req_data);
static void         elem_free(void* elem_ptr);
static void         display_tasks_queue(node_t me);
static int          compar_fn(const void *arg1, const void *arg2);
static void         sort_tasks_queue(node_t me);
static void         display_delayed_tasks(node_t me);
static void         display_async_answers(node_t me, char log);
static void         display_states(node_t me, char mode);
static void         display_sc(node_t me, char mode);
static int          expected_answers_search(node_t me,
                                            xbt_dynar_t dynar,
                                            e_task_type_t type,
                                            e_task_type_t br_type,
                                            int recp_id,
                                            int new_node_id);
static int          state_search(node_t me, char active, int new_id);
static u_ans_data_t is_brother(node_t me, int id, int new_node_id);
static e_val_ret_t  cs_req(node_t me, int sender_id, int new_node_id, int cs_new_node_prio);
static void         cs_rel(node_t me, int new_node_id);
static void         rec_async_answer(node_t me, int idx, ans_data_t ans);
static void         rec_sync_answer(node_t me, int idx, ans_data_t ans);
static void         check_async_nok(node_t me, int *ans_cpt, e_val_ret_t *ret, int *nok_id, int new_node_id);
static void         launch_fork_process(node_t me, msg_task_t task);
static xmlDocPtr    get_xml_input_file(const char *doc_name);
static char*        filename(const char *file_name);
static int          read_xml_files(node_t me, char *xpath);

/*
  ======================= COMMUNICATION FUNCTIONS =============================
*/

static msg_error_t send_msg_sync(node_t        me,
                                 e_task_type_t type,
                                 int           recipient_id,
                                 u_req_args_t  args,
                                 ans_data_t   *answer_data);

static msg_error_t send_msg_async(node_t        me,
                                  e_task_type_t type,
                                  int           recipient_id,
                                  u_req_args_t  args);

static msg_error_t send_ans_sync(node_t me,
                                 int new_node_id,
                                 e_task_type_t type,
                                 int recipient_id,
                                 char* recipient_mailbox,
                                 u_ans_data_t u_ans_data);

static e_val_ret_t broadcast(node_t me, u_req_args_t args);
static void        send_completed(node_t        me,
                                  e_task_type_t type,
                                  int           recipient_id,
                                  char*         recipient_mailbox,
                                  int           new_node_id);

/*
 =========================== CORE FUNCTIONS ===================================
*/

static void         init(node_t me);
static int          join(node_t me, int contact_id);
static u_ans_data_t get_rep(node_t me, int stage, int new_node_id);
static u_ans_data_t connection_request(node_t me, int new_node_id, int cs_new_node_prio, int try);
static void         new_brother_received(node_t me, int new_node_id);
static void         split_request(node_t me, int stage_nbr, int new_node_id);
static void         add_stage(node_t me);
static void         add_pred(node_t me, int stage, int id);
static void         del_pred(node_t me, int stage, int pred2del);
static void         del_member(node_t me, int stage, int start, int end);
static void         cut_node(node_t me, int stage, int right, int cut_pos, int new_node_id);
static void         add_brother(node_t me, int stage, int id);
static void         insert_bro(node_t me, int stage, int id);
static void         add_bro_array(node_t me,
                                  int stage,
                                  node_rep_t bro,
                                  int array_size,
                                  int right,
                                  int new_node_id);
static void         br_add_bro_array(node_t me,
                                     int stage,
                                     node_rep_t bro,
                                     int array_size,
                                     int right,
                                     int new_node_id);
static void         update_upper_stage(node_t me, int stage, int pos2repl, int new_id, int new_node_id);
static void         del_bro(node_t me, int stage, int bro2del);
static void         connect_splitted_groups(node_t me,
                                            int stage,
                                            int pos_init,
                                            int pos_new,
                                            int init_rep_id,
                                            int new_rep_id,
                                            int new_node_id);
static void         split(node_t me, int stage, int new_node_id);
static int          merge_or_transfer(node_t me, int stage);
static void         merge(node_t me,
                          int *nodes_array,
                          int nodes_array_size,
                          int stage,
                          int pos_me,
                          int pos_contact,
                          int right,
                          int new_node_id);
static void         broadcast_merge(node_t me,
                                    int stage,
                                    int pos_me,
                                    int pos_contact,
                                    int right,
                                    int lead_br,
                                    int new_node_id);
static void         leave(node_t me);
static u_ans_data_t transfer(node_t me,
                             int st,
                             int right,
                             int cut_pos,
                             s_node_rep_t sender,
                             int new_node_id);
static void         replace_bro(node_t me,
                                int stage,
                                int init_idx,
                                int new_id,
                                int new_node_id);
static void         shift_bro(node_t me,
                              int stage,
                              s_node_rep_t new_node,
                              int right,
                              int new_node_id);
static void         del_root(node_t me, int init_height);
static void         clean_upper_stage(node_t me,
                                      int stage,
                                      int pos_me,
                                      int pos_contact,
                                      int new_node_id);
static void         merge_request(node_t me, int new_node_id);
static void         load_balance(node_t me, int contact_id);
//static u_ans_data_t get_new_contact(node_t me, int new_node_id);

/*
 ============================ PROCESS FUNCTIONS ===============================
*/

static int         node(int argc, char *argv[]);
static void        action_node(const char *const *action);
static e_val_ret_t handle_task(node_t me, msg_task_t* task);
static int         proc_handle_task(int argc, char *argv[]);
static int         proc_run_tasks(int argc, char* argv[]);
static void        proc_data_cleanup(void* arg);

#endif
