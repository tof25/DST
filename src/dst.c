/*
 *  dst.c
 *
 *  Written by Christophe Enderlin on 2014/11/17
 *
 */

// TODO : voir si 'p' pourrait arriver après un add_pred

/* TODO: gérer la non-réponse d'un contact lors de l'arrivée d'un nœud.
 *       gérer tous les timeout des comm_wait() */

// TODO: voir si tout est OK pour les timers (peut-on remplacer des MSG_get_clock() par des clock ?)

//TODO: introduire un indicateur de réponses à venir. On pourra s'en servir pour terminer la simulation.
//      (à la place de WAIT_BEFORE_END)

//#define MSG_USE_DEPRECATED
#include <stdio.h>                          // printf and friends
#include "msg/msg.h"                        // to use MSG API of Simgrid
#include "xbt/log.h"                        // to get nice outputs
#include "xbt/asserts.h"                    // to use xbt_assert()
#include "xbt/xbt_os_time.h"                /* to use a timer (located in
                                               Simgrid-3.9/src/include/xbt) */
#include "xbt/ex.h"                         // to use exceptions
#include <time.h>
#include <stdlib.h>                         // to use rand()

XBT_LOG_NEW_DEFAULT_CATEGORY(msg_dst, "Messages specific for the DST");

/*
   ========================  GLOBAL VALUES  ==================================
*/

#define COMM_SIZE 10                        // message size when creating a new task
#define COMP_SIZE 0                         // compute duration when creating a new task
#define MAILBOX_NAME_SIZE 50                // name size of a mailbox
#define TYPE_NBR 38                         // number of task types
#define MAX_WAIT_COMPL 20000                // won't wait longer for broadcast completion
#define MAX_WAIT_GET_REP 5000               // won't wait longer an answer to a GET_REP request
#define MAX_JOIN 250                        // number of joining attempts
#define TRY_STEP 50                         // number of tries before requesting a new contact
#define MAX_CS_REQ 700                      // max time between cs_req and matching set_update
#define MAX_CNX 200                         // max number of attempts to run CNX_REQ (before trying another contact)
#define WAIT_BEFORE_END 2000                // Wait for last messages before ending simulation


static const int a = 3;                     /* min number of brothers in a node
                                               (except for the root node) */
static const int b = 6;                     /* max number of brothers in a node
                                               (must be twice a) */
static int         COMM_TIMEOUT = 19000;                    // timeout for communications (mustn't be greater than MAX_WAIT_COMPL)
static double      max_simulation_time = 10500;             // max default simulation time              //TODO : plus utile ?
static xbt_dynar_t infos_dst;                               // to store all global DST infos
static int         nb_messages[100000][TYPE_NBR] = {0};     // total number of messages exchanged for each task type
static int         nb_br_messages[100000][TYPE_NBR] = {0};  // total number of broadcasted messages exchanged for each task type
static int         order = 0;                               // order number of nodes arrival
static int         nb_nodes = 0;                            // total number of nodes
static int         nb_ins_nodes = 0;                        // number of nodes already inserted
static int         mem_log = -1;                            // ensures that log new setting occurs only once
static int         inserted_nodes[100000] = {-1};           // to store a list of already inserted nodes

typedef struct f_node {                     // node that failed to join
    int   id;
    float f_time;
} s_f_node_t;

static int nb_abort = 0;                    // number of join abortions
static s_f_node_t *failed_nodes = NULL;     // array of nodes that couldn't join the DST

static int compteur[TYPE_NBR] = {0};
static int compt_proc = 0;
static int compt_proc_rest = 0;
static int cpt_loop_proc[100] = {0};

//static int *g_cpt = NULL;

/**
 * Infos about the DST (for reporting purposes)
 */
typedef struct s_dst_info {
    int   order;                            // arrival order
    int   node_id;                          // node id
    char  active;                           // node state
    char *routing_table;                    // string representation of a routing table
    int   attempts;                         // number of attempts to join the dst
    int   add_stage;                        /* boolean: was it necessary to add
                                               a stage to insert this node ? */
    int   nbr_split_stages;                 /* number of splitted stages to make
                                               room for this node */
    int  *load;                             /* load table (number of predecessors
                                               per stage) */
    int  *size;                             // number of brothers for each stage
    int   nb_cs_req_fail;                   // number of failed BR_CS_REQ
    int   nb_cs_req_success;                // number of successful BR_CS_REQ
    int   nb_set_update_fail;               // number of failed BR_SET_UPDATE
    int   nb_set_update_success;            // number of sucessful BR_SET_UPDATE
    int   nb_task_remove;                   // number of BR_TASK_REMOVE
    int   nb_chg_contact;                   // number of contact changes
} s_dst_infos_t, *dst_infos_t;

/**
 * A node representative
 */
typedef struct node_rep {

    int id;                                 // representative id
    char mailbox[MAILBOX_NAME_SIZE];        /* representative main mailbox name */
} s_node_rep_t, *node_rep_t;

/**
 * Types of request/answer tasks exchanged
 */
typedef enum {

    TASK_NULL,              // no task (for logging purposes)
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
    TASK_DISPLAY_VAR,       // display a variable of a remote node (for debugging purposes)
    TASK_GET_SIZE,          // return the number of brothers on a given stage
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
    TASK_GET_NEW_CONTACT,   // get a new contact for a new coming node that failed to join too much
    TASK_CS_REQ,            // ask for permission to get into Critical Section (splitted area)
    TASK_END_GET_REP,       // remove 'g' state after load balance
    TASK_REMOVE_STATE,      // remove given state from states dynar
    TASK_CS_REL,            // reset cs_req flag
    TASK_CHECK_CS           // send back a CS_REL if not 'b' neither 'n'
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
    int             prio;                   // priority to get into critical section (the lower the value, the highest priority)
    char            cs_req;                 // Critical Section requested
    float           cs_req_time;            // timestamp when cs_req was set
    int             cs_new_id;              // new node's id that set cs_req
    int             cs_new_node_prio;       // new node's priority
    float           last_check_time;        // last time when checking if cs_req has to be reset occured
    s_run_task_t    run_task;               // current running task state (delayed tasks)
    s_run_task_t    other_task_state;       // other task state
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
    "Get New Contact",
    "Critical Section Request",
    "End Get Rep",
    "Pop State",
    "Critical Section Released",
    "Check CS"
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
} s_task_clean_stage_t;         /* clean useless nodes in a stage when a merge
                                   occured in lower ones */

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

typedef struct {

    int new_node_id;
} s_task_get_new_contact_t;    // get new contact (in case of too much failures)

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
} s_task_check_cs_t;           // send back a CS_REL if not 'b' neither 'n'

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
    s_task_get_new_contact_t    get_new_contact;
    s_task_cs_req_t             cs_req;
    s_task_end_get_rep_t        end_get_rep;
    s_task_remove_state_t       remove_state;
    s_task_cs_rel_t             cs_rel;
    s_task_check_cs_t           check_cs;
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

typedef struct {

    int id;
} s_task_ans_get_new_contact_t;

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
    s_task_ans_get_new_contact_t    get_new_contact;
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
//static int          tot_msg_number(int id);
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
//static int          dst_xbt_dynar_search_or_negative(xbt_dynar_t dynar, const void *elem);
//static char         dst_xbt_dynar_member(xbt_dynar_t dynar, void *elem);
static e_val_ret_t  cs_req(node_t me, int sender_id, int new_node_id, int cs_new_node_prio);
static void         cs_rel(node_t me, int new_node_id);
static void         rec_async_answer(node_t me, int idx, ans_data_t ans);
static void         rec_sync_answer(node_t me, int idx, ans_data_t ans);
static void         check_async_nok(node_t me, int *ans_cpt, e_val_ret_t *ret, int *nok_id, int new_node_id);
static void         check_cs(node_t me, int sender_id);
static void         launch_fork_process(node_t me, msg_task_t task);

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
static u_ans_data_t get_new_contact(node_t me, int new_node_id);

/*
 ============================ PROCESS FUNCTIONS ===============================
*/

static int         node(int argc, char *argv[]);
static e_val_ret_t handle_task(node_t me, msg_task_t* task);
static int         proc_handle_task(int argc, char *argv[]);
static int         proc_run_tasks(int argc, char* argv[]);
static void        proc_data_cleanup(void* arg);

/*
 ========================== FUNCTIONS DEFINITIONS =============================
*/

/**
 * \brief Count lines of a file
 * \param file the name of the file
 * \return number of lines
 */
static int count_lines_of_file(const char *file) {
    int ch, count = 0;
    FILE *fp = fopen(file, "r");
    if(fp == NULL) {
        return -1;
    }
    while((ch = fgetc(fp)) != EOF) {
        if (ch == '\n') {
            count++;
        }
    }
    fclose(fp);
    return count - 4;
}

/**
 * \brief Ask a remote node to display some variable (for debug)
 * \param me the current node
 */
static void display_var(node_t me) {

    XBT_DEBUG("Node %d: me->bro_index[0] = %d", me->self.id, me->bro_index[0]);
}

/**
 * \brief Return a string representation of a routing table
 * \param me the current node
 * \return the routing table in text
 */
static char* routing_table(node_t me) {

    XBT_IN();

    xbt_dynar_t tab;
    tab = xbt_dynar_new(sizeof(char *), &xbt_free_ref);

    char buf[1024];
    char *s;

    sprintf(buf, "\nHere's node %d's routing table (with load):\n+", me->self.id);
    s = xbt_strdup(buf);
    xbt_dynar_push(tab, &s);

    int stage, brother;

    for (brother = 0; brother <= b; brother++) {

        sprintf(buf, "--------+");
        s = xbt_strdup(buf);
        xbt_dynar_push(tab, &s);
    }

    sprintf(buf, "  +--------+\n");
    s = xbt_strdup(buf);
    xbt_dynar_push(tab, &s);

    for (stage = 0; stage < me->height; stage++) {

        sprintf(buf, "| S%-5d |", stage);
        s = xbt_strdup(buf);
        xbt_dynar_push(tab, &s);

        for (brother = 0; brother < b; brother++) {

            if (brother >= me->bro_index[stage]) {

                sprintf(buf, "        |");
                s = xbt_strdup(buf);
                xbt_dynar_push(tab, &s);
            } else {

                sprintf(buf, " %-6d |", me->brothers[stage][brother].id);
                s = xbt_strdup(buf);
                xbt_dynar_push(tab, &s);
            }
        }

        sprintf(buf, "  | %-6d |\n+", me->dst_infos.load[stage]);
        s = xbt_strdup(buf);
        xbt_dynar_push(tab, &s);

        for (brother = 0; brother <= b; brother++) {

            sprintf(buf, "--------+");
            s = xbt_strdup(buf);
            xbt_dynar_push(tab, &s);
        }
        sprintf(buf, "  +--------+\n");
        s = xbt_strdup(buf);
        xbt_dynar_push(tab, &s);
    }
    sprintf(buf, "\n");
    s = xbt_strdup(buf);
    xbt_dynar_push(tab, &s);

    s = xbt_str_join(tab, "");

    xbt_dynar_free(&tab);

    XBT_OUT();
    return s;
}

/**
 * Store DST infos with routing table
 * \param me the current node
 */
static void set_n_store_infos(node_t me) {
    XBT_IN();

    /*
    // free string before replacing it
    if (me->dst_infos.routing_table != NULL) {

    xbt_free(me->dst_infos.routing_table);
    }
    */
    me->dst_infos.routing_table = routing_table(me);

    s_state_t state = get_state(me);
    me->dst_infos.active = state.active;

    me->dst_infos.size = me->bro_index;

    dst_infos_t elem = xbt_new0(s_dst_infos_t, 1);
    *elem = me->dst_infos;
    //if (xbt_dynar_member(infos_dst, elem) == 0) {

    xbt_dynar_replace(infos_dst, me->dst_infos.order, &elem);

    /*} else {

    XBT_INFO("number set = %d, order = %d", ++nb_calls2, me->dst_infos.order);
        XBT_DEBUG("Set");
        xbt_dynar_set(infos_dst, me->dst_infos.order, &elem);
    } */

    XBT_OUT();
}

/**
 * \brief Display the predecessors of a node
 * \param me the current node
 */
static void display_preds(node_t me, char log) {

    //XBT_IN();

    xbt_dynar_t tab;
    tab = xbt_dynar_new(sizeof(char *), &xbt_free_ref);

    char buf[1024];
    char *s;

    sprintf(buf, "\nHere are node %d's predecessors:\n+", me->self.id);
    s = xbt_strdup(buf);
    xbt_dynar_push(tab, &s);

    int stage, pred, max_idx = 0;
    for (stage = 0; stage < me->height; stage++) {

        if (me->pred_index[stage] > max_idx) max_idx = me->pred_index[stage];
    }

    for (pred = 0; pred <= max_idx; pred++) {

        sprintf(buf, "----+");
        s = xbt_strdup(buf);
        xbt_dynar_push(tab, &s);
    }
    sprintf(buf, "\n");
    s = xbt_strdup(buf);
    xbt_dynar_push(tab, &s);

    for (stage = 0; stage < me->height; stage++) {

        sprintf(buf, "|S%-3d|", stage);
        s = xbt_strdup(buf);
        xbt_dynar_push(tab, &s);

        for (pred = 0; pred < max_idx; pred++) {

            if (pred >= me->pred_index[stage]) {

                sprintf(buf, "    |");
                s = xbt_strdup(buf); 
                xbt_dynar_push(tab, &s);
            } else {

                sprintf(buf, "%-4d|", me->preds[stage][pred].id);
                s = xbt_strdup(buf);
                xbt_dynar_push(tab, &s);
            }
        }
        sprintf(buf, "\t\tidx = %d\n+", me->pred_index[stage]);
        s = xbt_strdup(buf);
        xbt_dynar_push(tab, &s);

        for (pred = 0; pred <= max_idx; pred++) {

            sprintf(buf, "----+");
            s = xbt_strdup(buf);
            xbt_dynar_push(tab, &s);
        }
        sprintf(buf, "\n");
        s = xbt_strdup(buf);
        xbt_dynar_push(tab, &s);
    }
    sprintf(buf, "\n");
    s = xbt_strdup(buf);
    xbt_dynar_push(tab, &s);

    s = xbt_str_join(tab, "");
    switch (log) {

        case 'V': XBT_VERB("%s", s);
                  break;

        case 'I': XBT_INFO("%s", s);
                  break;

        case 'D': XBT_DEBUG("%s", s);
                  break;
    }
    xbt_free(s);
    xbt_dynar_free(&tab);

    //XBT_OUT();
}

/**
 * \brief Display me's routing table
 * \param me the current node
 */
static void display_rout_table(node_t me, char log) {

    //XBT_IN();

    char *rt = routing_table(me);

    switch (log) {

        case 'D':
            XBT_DEBUG("%s", rt);
            break;

        case 'V':
            XBT_VERB("%s", rt);
            break;

        case 'I':
            XBT_INFO("%s", rt);
            break;
    }

    xbt_free(rt);

    //XBT_OUT();
}

/**
 * \brief Sets the mailbox name of a node
 * \param node_id node id
 * \param mailbox pointer to where the mailbox name should be written to
 *        (there must be enough space)
 */
static void set_mailbox(int node_id, char* mailbox) {

    snprintf(mailbox, MAILBOX_NAME_SIZE, "%d", node_id);
}

/**
 * \brief Sets the mailbox name of current process
 * \param proc_mailbox pointer to where the mailbox name should be written to
 *        (there must be enough space)
 */
static void set_proc_mailbox(char* proc_mailbox) {

    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());
    snprintf(proc_mailbox, MAILBOX_NAME_SIZE, "%s", proc_data->proc_mailbox);
}

/**
 * \brief Add process ID to the mailbox name of a process to make it unique
 */
static void setId_proc_mailbox(void) {

    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());
    char str_ID[MAILBOX_NAME_SIZE];

    snprintf(str_ID, MAILBOX_NAME_SIZE, "{%d}", MSG_process_get_PID(MSG_process_self()));
    strcat(proc_data->proc_mailbox, str_ID);
}

/**
 * \brief Sets the fork process mailbox name
 * \param node_id the current node id
 * \param new_node_id new coming node id
 * \param session session name
 * \param mailbox pointer to where the mailbox name should be written
 *        (there must be enough space)
 */
static void set_fork_mailbox(int node_id, int new_node_id, char* session, char* mailbox) {

    snprintf(mailbox, MAILBOX_NAME_SIZE, "%d-%s-%d", node_id, session, new_node_id);
}

/**
 * \brief Frees the memory used by a task. Any attached data have to be freed
 *        before calling this function.
 * \param task pointer to the MSG task to destroy
 */
static void task_free(msg_task_t *task) {

    //XBT_IN();

    xbt_ex_t ex;

    XBT_DEBUG("in task_free, task = %p", *task);

    if (*task == NULL) {

        //XBT_VERB("in task_free, task = %p", *task);
    } else {

        TRY {
            XBT_DEBUG("Destroy task");
            XBT_DEBUG("Before destruction : *task = %p - name = %p/'%s'", *task, MSG_task_get_name(*task),MSG_task_get_name(*task));

            MSG_task_destroy(*task);

            XBT_DEBUG("After destruction : *task = %p - name = %p/'%s'", *task, MSG_task_get_name(*task),MSG_task_get_name(*task));

            *task = NULL;

            XBT_DEBUG("Task destroyed");
        }
        CATCH(ex) {
            *task = NULL;
            xbt_ex_free(ex);
            XBT_DEBUG("Error in task_free");
        }
    }
    //XBT_OUT();
}

/**
 * \brief Returns the index of brother 'id' for a given stage
 *        (-1 if not found)
 * \param me the current node
 * \param stage the given stage
 * \param id the brother id to look for
 * \return The index of searched brother
 */
static int index_bro(node_t me, int stage, int id) {

    XBT_IN();
    int idx = 0;
    while ((idx < me->bro_index[stage]) && (me->brothers[stage][idx].id != id)) {

        idx++;
    }
    if (idx == me->bro_index[stage]) {

        XBT_DEBUG("Node %d not found as a brother of node %d for stage %d",
                id,
                me->self.id,
                stage);
        idx = -1;
    }
    XBT_OUT();
    return idx;
}

/**
 * \brief Returns the index of '{type, br_type, id}' in an array of s_recp_rec_t.
 *        (-1 if not found)
 * \param id the id to search
 * \param type the task type to search
 * \param br_type the broadcasted task type to search
 * \param array the array to search in
 * \param size size of the array
 * \return The index of found entry
 */
/*
   static int index_array(int id,
   e_task_type_t type,
   e_task_type_t br_type,
   recp_rec_t array,
   int size) {

   XBT_IN();

   s_recp_rec_t triplet;
   triplet.type = type;
   triplet.br_type = br_type;
   triplet.recp.id = id;

// search from top (most recent request) to bottom (oldest request)
int idx = size - 1;

while ((idx >= 0) &&
((array[idx].type != triplet.type) ||
(array[idx].br_type != triplet.br_type) ||
(array[idx].recp.id != triplet.recp.id))) {

idx--;
}

XBT_OUT();
return idx;
}
*/

/**
 * \brief Returns the position of '{type, br_type, id, new_node_id}' in given dynar
 *        (either sync or async expected answers)
 * \param me the current node
 * \param dynar the dynar to search into
 * \param type the task type to search
 * \param br_type the broadcasted task type to search
 * \param recp_id the recipient id to search
 * \param new_node_id the new node id attached to the answer
 * \return the position of found entry (-1 if not found)
 */
static int expected_answers_search(node_t me,
        xbt_dynar_t dynar,
        e_task_type_t type,
        e_task_type_t br_type,
        int recp_id,
        int new_node_id) {

    XBT_IN();

    int pos = -1;
    unsigned int iter = 0;
    recp_rec_t elem;

    xbt_dynar_foreach(dynar, iter, elem) {

        XBT_DEBUG("Node %d dynar traversal: iter = %d - idx = %d --> {type:"
                " '%s | %s' - recipient: %d - data: %p - new_node: %d}",
                me->self.id,
                iter,
                pos,
                debug_msg[elem->type],
                debug_msg[elem->br_type],
                elem->recp.id,
                elem->answer_data,
                elem->new_node_id);

        if (elem->type == type &&
                elem->br_type == br_type &&
                elem->recp.id == recp_id &&
                elem->new_node_id == new_node_id &&
                elem->answer_data == NULL) {

            // record found
            pos = iter;
        }
    }

    XBT_OUT();
    return pos;
}

/**
 * \brief Search for a given state in dynar states
 * \param me the current node
 * \param active state member to be searched
 * \param new_id state member to be searched
 * \return position of searched item, -1 otherwise
 */
static int state_search(node_t me, char active, int new_id) {

    XBT_IN(" *** param: active = %c - new_id = %d ***",
            active, new_id);

    int pos = -1;
    unsigned int iter = 0;
    state_t elem = NULL;

    /*
       XBT_DEBUG("Node %d: Dynar states length: %u",
       me->self.id,
       xbt_dynar_length(me->states));
       */

    xbt_dynar_foreach(me->states, iter, elem) {

        XBT_DEBUG("Node %d dynar states traversal: iter = %d - pos = %d --> "
                "{state '%c'/%u}",
                me->self.id,
                iter,
                pos,
                elem->active,
                elem->new_id);

        if (elem->active == active &&
                (new_id == -1 ||
                 elem->new_id == new_id)) {

            // record found
            pos = iter;
        }
    }

    XBT_DEBUG("Node %d: [%s:%d] pos = %d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            pos);

    XBT_OUT();

    return pos;
}

/**
 * \brief Answers 1 if id is a brother of current node
 * \param me the current node
 * \param id another node's id
 * \param new_node_id new concerned coming node
 * \return 1 if id is a brother of current node, -1 otherwise
 */
static u_ans_data_t is_brother(node_t me, int id, int new_node_id) {

    XBT_IN();

    u_ans_data_t answer;
    answer.is_brother.rep = -1;
    int i;

    for (i = 0; i < me->bro_index[0]; i++) {

        if (me->brothers[0][i].id == id) {

            answer.is_brother.rep = 1;
            break;
        }
    }
    XBT_OUT();
    return answer;
}

/**
 * \brief Some node asks for permission to get into Critical Section
 * \param me the current node
 * \param sender_id sender node's id
 * \param new_node_id involved new coming node
 * \param cs_new_node_prio new node's priority
 * \return OK or UPDATE_NOK for permission granted or not
 */
static e_val_ret_t cs_req(node_t me, int sender_id, int new_node_id, int cs_new_node_prio) {
    XBT_IN();

    XBT_VERB("Node %d: [%s:%d] new node %d from %d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            new_node_id,
            sender_id);

    display_sc(me, 'D');

    e_val_ret_t val_ret = OK;
    s_state_t state = get_state(me);
    char test = (MSG_get_clock() - me->cs_req_time > MAX_CS_REQ);

    /* to avoid dealocks : if CS has been requested and not answered for long
       ago, or if new node's priority is lower, cancel this request */

    if (me->cs_req == 1 && me->cs_new_id != new_node_id && state.active == 'a' &&
        (cs_new_node_prio < me->cs_new_node_prio || test == 1)) {        //TODO: à vérifier

        me->cs_req = 0;

        XBT_VERB("Node %d: '%c'/%d cs_req set at clock %f--> reset - node %d's priority : %d - node %d's priority : %d",
                me->self.id,
                state.active,
                state.new_id,
                me->cs_req_time,
                new_node_id,
                cs_new_node_prio,
                me->cs_new_id,
                me->cs_new_node_prio);
    }

    if (me->cs_req == 1) {
        if (me->cs_new_id != new_node_id || state.active != 'a') {

            // answer NOK
            val_ret = UPDATE_NOK;

        } else {

            // answer OK
            val_ret = OK;
        }
    } else {

        if (state.active == 'a') {

            me->cs_req = 1;
            me->cs_new_id = new_node_id;
            me->cs_req_time = MSG_get_clock();
            me->cs_new_node_prio = cs_new_node_prio;

            val_ret = OK;
        } else {

            val_ret = UPDATE_NOK;
        }
    }

    display_sc(me, 'V');

    XBT_OUT();
    return val_ret;
}

/**
 * \brief Critical Section is released
 * \param new_node_id the new node that's currently being inserted
 */
static void cs_rel(node_t me, int new_node_id) {
    XBT_IN();

    if (me->cs_new_id == new_node_id) {

        me->cs_req = 0;
        me->cs_req_time = MSG_get_clock();
        me->cs_new_id = new_node_id;
    }

    s_state_t state = get_state(me);
    XBT_VERB("Node %d: '%c'/%d end of cs_rel()",
            me->self.id,
            state.active,
            state.new_id);

    display_sc(me, 'V');

    xbt_assert(state.active != 'u', "'u' state in cs_rel() !!");

    XBT_OUT();
}

/**
 * \brief Record an async answer in async_answers dynar
 * \param me the current node
 * \param idx the record's position in the dynar
 * \param ans answer to be written
 */
static void rec_async_answer(node_t me, int idx, ans_data_t ans) {
    XBT_IN();

    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());
    recp_rec_t *elem_ptr = xbt_dynar_get_ptr(proc_data->async_answers, idx);

    // mark answer as received
    (*elem_ptr)->recp.id = -1;

    xbt_assert((*elem_ptr)->answer_data == NULL,
            "Node %d: rec answer_data %d not NULL !",
            me->self.id,
            idx);

    // record the answer
    if ((*elem_ptr)->answer_data == NULL) {

        (*elem_ptr)->answer_data = xbt_new0(s_ans_data_t, 1);
        (*elem_ptr)->answer_data->answer.handle.val_ret =
            ans->answer.handle.val_ret;
        (*elem_ptr)->answer_data->answer.handle.val_ret_id =
            ans->answer.handle.val_ret_id;
    }

    XBT_OUT();
}

/**
 * \brief Record a sync answer in sync_answers dynar
 * \param me the current node
 * \param idx the record's position in the dynar
 * \param ans answer to be written
 */
static void rec_sync_answer(node_t me, int idx, ans_data_t ans) {
    XBT_IN();

    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());
    recp_rec_t *elem_ptr = xbt_dynar_get_ptr(proc_data->sync_answers, idx);

    xbt_assert((*elem_ptr)->answer_data == NULL,
            "Node %d in rec_sync_answer(): dynar rec %d data not null",
            me->self.id,
            idx);

    // record answer
    (*elem_ptr)->answer_data = xbt_new0(s_ans_data_t, 1);
    *((*elem_ptr)->answer_data) = *ans;

    XBT_OUT();
}

/**
 * \brief Auxiliary function for wait_for_completion().
 *        Search for received answers in proc_data->async_answers dynar, adjust ans_cpt accordingly,
 *        and records any NOK answer.
 * \param me the current node
 * \param ans_cpt number of expected answers
 * \param ret return value (OK or UPDATE_NOK)
 * \param nok_id the first met node's id that answered NOK
 * \param new_node_id the current new coming node
 */
static void check_async_nok(node_t me, int *ans_cpt, e_val_ret_t *ret, int *nok_id, int new_node_id) {
    XBT_IN();

    recp_rec_t *elem_ptr = NULL;
    int idx;
    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());
    int dynar_size = (int) xbt_dynar_length(proc_data->async_answers);

    // ans_cpt musn't be greater than dynar
    xbt_assert(*ans_cpt <= dynar_size,
            "Node %d: in check_async_nok() - ans_cpt = %d - dynar_size = %d",
            me->self.id,
            *ans_cpt,
            dynar_size);

    for (idx = dynar_size - 1; idx >= dynar_size - *ans_cpt; idx--) {

        XBT_DEBUG("IDX = %d - ans_cpt = %d - dynar_size = %d", idx, *ans_cpt, dynar_size);

        elem_ptr = xbt_dynar_get_ptr(proc_data->async_answers, idx);
        if ((*elem_ptr)->recp.id == -1) {

            // check if an UPDATE_NOK has been received
            if ((*elem_ptr)->answer_data != NULL && *ret != UPDATE_NOK) {

                /* only records answers for the same "instance" of
                   wait_for_completion */
                if ((*elem_ptr)->new_node_id == new_node_id) {

                    *ret = (*elem_ptr)->answer_data->answer.handle.val_ret;
                    *nok_id = (*ret == UPDATE_NOK ?
                               (*elem_ptr)->answer_data->answer.handle.val_ret_id : -1);
                }
            }

            if ((*elem_ptr)->answer_data != NULL) {

                xbt_free((*elem_ptr)->answer_data);
                (*elem_ptr)->answer_data = NULL;
            }

            // yes
            (*ans_cpt) --;

            // delete this entry from expected answers
            xbt_dynar_remove_at(proc_data->async_answers, idx, NULL);
            dynar_size = (int) xbt_dynar_length(proc_data->async_answers);
        }
    }

    XBT_OUT();
}

/**
 * \brief If state is not 'n' nor 'b', sends a cs_rel to sender_id
 * \param me the current node
 * \param sender_id cs_rel has to be sent to this node
 */
static void check_cs(node_t me, int sender_id) {
    XBT_IN();

    s_state_t state = get_state(me);
    if (state.active != 'n' && state.active != 'b') {

        XBT_VERB("Node %d: Yes, node %d's cs_req has to be reset",
                me->self.id,
                sender_id);

        if (me->self.id != sender_id) {

            // send a cs_rel if active
            u_req_args_t args;
            //args.cs_rel.new_node_id = me->self.id;
            args.set_active.new_node_id = me->self.id;

            msg_error_t res = send_msg_async(me,
                    //TASK_CS_REL,
                    TASK_SET_ACTIVE,
                    sender_id,
                    args);
        } else {

            set_active(me, me->self.id);
        }
    }

    XBT_OUT();
}


/**
 * \brief Run a given task locally or with a fork process, according to its type
 * \param me the current node
 * \param task the task to be executed
 */
static void launch_fork_process(node_t me, msg_task_t task) {

    XBT_IN();
    e_val_ret_t val_ret = OK;
    req_data_t req = MSG_task_get_data(task);

    if (req->type == TASK_CNX_REQ ||
        (req->type == TASK_BROADCAST && (req->args.broadcast.type == TASK_SPLIT ||
                                         req->args.broadcast.type == TASK_CS_REQ))) {

        // handle the task with a fork process ..
        proc_data_t proc_data = xbt_new0(s_proc_data_t, 1);

        proc_data->node = me;
        proc_data->task = task;
        proc_data->async_answers = xbt_dynar_new(sizeof(recp_rec_t), &xbt_free_ref);
        proc_data->sync_answers = xbt_dynar_new(sizeof(recp_rec_t), &xbt_free_ref);

        char proc_label[10];

        switch (req->type) {
            case TASK_CNX_REQ:
                snprintf(proc_label, 10, "Cnx_req");
                XBT_VERB("Node %d: [%s:%d] cnx_req.try = %d",
                        me->self.id,
                        __FUNCTION__,
                        __LINE__,
                        req->args.cnx_req.try);
                break;

            case TASK_BROADCAST:
                switch (req->args.broadcast.type) {
                    case TASK_SPLIT:
                        snprintf(proc_label, 10, "Br_Split");
                        break;

                    case TASK_CS_REQ:
                        snprintf(proc_label, 10, "Br_Cs_Req");
                        val_ret = cs_req(me,
                                req->args.broadcast.args->cs_req.sender_id,
                                req->args.broadcast.args->cs_req.new_node_id,
                                req->args.broadcast.args->cs_req.cs_new_node_prio);
                        break;

                    default:
                        snprintf(proc_label, 10, "Broadcast");
                        break;
                }
                break;
        }

        // create process only if cs_req succeeded
        if (val_ret == OK) {

            set_fork_mailbox(me->self.id,
                    req->args.cnx_req.new_node_id,
                    proc_label,
                    proc_data->proc_mailbox);

            XBT_VERB("Node %d: [%s:%d] create fork process (%s), task = %p, mailbox = %s",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    proc_label,
                    task,
                    proc_data->proc_mailbox);

            MSG_process_create(proc_data->proc_mailbox,
                    proc_handle_task,
                    proc_data,
                    MSG_host_self());

            XBT_VERB("Node %d: process created", me->self.id);
        } else {

            // cs_req failed
            XBT_VERB("Node %d: [%s:%d] not available - process not created",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__);

            // tell the sender
            if (strcmp(proc_data->proc_mailbox, req->answer_to) != 0) {

                u_ans_data_t answer;
                answer.handle.val_ret = val_ret;
                answer.handle.val_ret_id = me->self.id;
                answer.handle.br_type = req->args.broadcast.type;

                msg_error_t res = send_ans_sync(me,
                        req->args.broadcast.new_node_id,
                        req->type,
                        req->sender_id,
                        req->answer_to,
                        answer);

                XBT_VERB("Node %d: [%s:%d] answer '%s' sent to %s",
                        me->self.id,
                        __FUNCTION__,
                        __LINE__,
                        debug_ret_msg[val_ret],
                        req->answer_to);
            } else {

                XBT_DEBUG("Node %d: [%s:%d] No answer - answer_to: %s",
                        me->self.id,
                        __FUNCTION__,
                        __LINE__,
                        req->answer_to);
            }

            //compteur[req->args.broadcast.type]--;

            // free unused memory
            data_req_free(me, &req);
            task_free(&(proc_data->task));
            xbt_dynar_free(&(proc_data->async_answers));
            xbt_dynar_free(&(proc_data->sync_answers));
            xbt_free(proc_data);

        }
    } else {

        // ... or locally
        xbt_assert(!(req->type == TASK_BROADCAST && req->args.broadcast.type == TASK_CS_REQ),
                "[:%d] STOP !! - task %s", __LINE__, debug_msg[req->args.broadcast.type]);

        handle_task(me, &task);
    }

    XBT_OUT();
}

/**
 * \brief Wait for the completion of a bunch of async sent tasks
 * \param me the current node
 * \param ans_cpt the number of expected anwswers
 * \param new_node_id the involved new coming node
 * \return A value indicating if tasks succeeded or not
 */
static e_val_ret_t wait_for_completion(node_t me, int ans_cpt, int new_node_id) {

    XBT_IN("\tNode %d: [%s:%d] waiting for %d answers",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            ans_cpt);

    xbt_assert(ans_cpt >= 0,
            "Node %d: in wait_for_completion() - ans_cpt should be >= 0 : %d",
            me->self.id,
            ans_cpt);

    // inits
    e_val_ret_t ret = OK;
    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());
    int dynar_size = (int) xbt_dynar_length(proc_data->async_answers);

    XBT_DEBUG("Node %d: in wait_for_completion - async_answers dynar size = %d",
            me->self.id,
            dynar_size);

    recp_rec_t *elem_ptr = NULL;
    float max_wait = MSG_get_clock() + MAX_WAIT_COMPL;
    msg_task_t task_received = NULL;
    ans_data_t ans = NULL;
    msg_error_t res = MSG_OK;
    int k, idx, nok_id;
    s_state_t state;
    char proc_mailbox[MAILBOX_NAME_SIZE];
    set_proc_mailbox(proc_mailbox);

    // async answers already received ? (from other recursive calls of this function)
    check_async_nok(me, &ans_cpt, &ret, &nok_id, new_node_id);
    dynar_size = (int) xbt_dynar_length(proc_data->async_answers);

    xbt_assert(ans_cpt >= 0,
            "Node %d: in wait_for_completion - ack reception error",
            me->self.id);

    int dynar_idx = -1;
    msg_comm_t comm = NULL;
    // waiting loop
    while (MSG_get_clock() <= max_wait && ans_cpt > 0) {

        XBT_VERB("Node %d: Waiting for completion on mailbox '%s' ... - ans_cpt = %d",
                me->self.id,
                proc_mailbox,
                ans_cpt);

        display_async_answers(me, 'D');

        //res = MSG_task_receive(&task_received, me->self.mailbox);
        //comm = MSG_task_irecv(&task_received, me->self.mailbox);

        task_received = NULL;
        comm = MSG_task_irecv(&task_received, proc_mailbox);

        res = MSG_comm_wait(comm, -1);

        // a communication has occurred
        state = get_state(me);
        XBT_VERB("Node %d: '%c'/%d - End of wait - ans_cpt = %d",
                me->self.id,
                state.active,
                state.new_id,
                ans_cpt);

        display_sc(me, 'V');

        xbt_assert(res != MSG_TIMEOUT,
                "Node %d: [%s:%d] receiving TIMEOUT in wait_for_completion",
                me->self.id,
                __FUNCTION__,
                __LINE__);

        xbt_assert(res == MSG_OK,
                "Node %d: [%s:%d] receiving failure %s in wait_for_completion",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                debug_res_msg[res]);

        MSG_comm_destroy(comm);

        if (res != MSG_OK) {

            // reception failure
            XBT_ERROR("Node %d: Failed to receive ack answer",
                    me->self.id);

            task_free(&task_received);
        } else {

            // reception success, extract data from task
            ans = MSG_task_get_data(task_received);

            req_data_t req = (req_data_t)ans;

            // log
            if (!strcmp(MSG_task_get_name(task_received), "ans")) {

                // answer
                XBT_VERB("Node %d: Received message : '%s - %s %s'"
                        " from %d to %d -> %s",
                        me->self.id,
                        MSG_task_get_name(task_received),
                        debug_msg[ans->type],
                        debug_msg[ans->br_type],
                        ans->sender_id,
                        ans->recipient_id,
                        ans->sent_to);
            } else {

                // request
                XBT_VERB("Node %d: Received message  : '%s - %s %s'"
                        " from %d to %d -> %s",
                        me->self.id,
                        MSG_task_get_name(task_received),
                        debug_msg[req->type],
                        debug_msg[(req->type == TASK_BROADCAST ?
                            req->args.broadcast.type : TASK_NULL)],
                        req->sender_id,
                        req->recipient_id,
                        req->sent_to);
            }

            // process received message
            dynar_idx = -1;
            proc_data_t proc_data = MSG_process_get_data(MSG_process_self());

            if (!strcmp(MSG_task_get_name(task_received), "ans")) {

                /* the received message is an answer
                 * ***********************************/

                /* is it one of the async expected answers ? */
                dynar_idx = expected_answers_search(me,
                        proc_data->async_answers,
                        ans->type,
                        ans->br_type,
                        ans->sender_id,
                        ans->new_node_id);

                XBT_VERB("Node %d in wait_for_completion(): matching record%sfound"
                        " in async answers dynar: idx = %d - dynar size = %d",
                        me->self.id,
                        (dynar_idx == -1 ? " not " : " "),
                        dynar_idx,
                        dynar_size);

                if (dynar_idx != -1) {

                    /* Yes. Is it one of the ans_cpt expected ones ? */

                    if (dynar_idx <= dynar_size - 1 && dynar_idx >= dynar_size - ans_cpt) {

                        // look if a set_update task has failed
                        if (ans->type == TASK_BROADCAST || ans->type == TASK_SET_UPDATE) {
                            if (ret != UPDATE_NOK) {

                                ret = ans->answer.handle.val_ret;
                                nok_id = (ret == UPDATE_NOK ?  ans->answer.handle.val_ret_id : -1);
                            }
                        }

                        // yes, this answer is one of the current expected acknowledgments
                        ans_cpt--;

                        // remove this entry from dynar
                        elem_ptr = xbt_dynar_get_ptr(proc_data->async_answers, dynar_idx);
                        if ((*elem_ptr)->answer_data != NULL) {

                            xbt_free((*elem_ptr)->answer_data);
                            (*elem_ptr)->answer_data = NULL;
                        }

                        xbt_dynar_remove_at(proc_data->async_answers, dynar_idx, NULL);

                        dynar_size = (int) xbt_dynar_length(proc_data->async_answers);
                    } else {

                        /* No, this answer is then expected by one of parent calls:
                           just record it in the dynar */

                        // NOTE : comment peut-on arriver là si la réponse ne se trouve pas déjà dans le dynar ?
                        // TODO : check point rencontré

                        /*
                        xbt_assert(1 == 0, "Node %d: [%s:%d] Check point",
                                me->self.id,
                                __FUNCTION__,
                                __LINE__);
                        */

                        rec_async_answer(me, dynar_idx, ans);
                    }

                    XBT_VERB("Node %d: answer '%s - %s' received from %d -"
                            " ans_cpt = %d",
                            me->self.id,
                            debug_msg[ans->type],
                            debug_msg[ans->br_type],
                            ans->sender_id,
                            ans_cpt);

                } else {

                    /* No, this answer is not an async expected one.
                       Is it a sync expected one ? */

                    dynar_idx = expected_answers_search(me,
                            proc_data->sync_answers,
                            ans->type,
                            ans->br_type,
                            ans->sender_id,
                            ans->new_node_id);

                    XBT_VERB("Node %d in wait_for_completion(): matching record%sfound"
                            " in sync answers dynar: idx = %d",
                            me->self.id,
                            (dynar_idx == -1 ? " not " : " "),
                            dynar_idx);

                    if (dynar_idx > -1) {

                        /* Yes, matching entry found */
                        rec_sync_answer(me, dynar_idx, ans);

                    } else {

                        /* No, this answer wasn't expected. Ignore it. */
                        XBT_DEBUG("Node %d: not found in dynars. Ignore it", me->self.id);
                    }
                }

                // answer has been processed. discard task and data
                data_ans_free(me, &ans);
                task_free(&task_received);

            } else {

                /* the received message is a request
                 * *********************************/

                XBT_VERB("Node %d: This is not an answer", me->self.id);

                /*NOTE: il ne semble pas nécessaire de récupérer la valeur de
                  retour de handle_task() ici */

                // push the received CNX_REQ task onto the dynar ...
                if (req->type == TASK_CNX_REQ) {

                    xbt_dynar_push(me->tasks_queue, &task_received);
                    task_received = NULL;

                    XBT_VERB("Node %d: task %s(%d) pushed",
                            me->self.id,
                            debug_msg[req->type],
                            req->args.cnx_req.new_node_id);

                } else {

                    launch_fork_process(me, task_received);
                }
                ans = NULL;
                req = NULL;

                /* NOTE: ne pas détruire les data ici.
                   Si la tâche traitée envoie un message, c'est le
                   destinataire qui doit détruire ces données */

                XBT_VERB("Node %d: back to wait_for_completion()."
                        " Answers received meanwhile ?",
                        me->self.id);

                // async expected answers received meanwhile ?

                XBT_DEBUG("Before check : ans_cpt = %d - dynar_size = %d",
                        ans_cpt, (int) xbt_dynar_length(proc_data->async_answers));

                check_async_nok(me, &ans_cpt, &ret, &nok_id, new_node_id);
                dynar_size = (int) xbt_dynar_length(proc_data->async_answers);
                // TODO : ajout de l'ajustement de dynar_size. Vérifier que ça ne pose pas problème

                XBT_DEBUG("After check : ans_cpt = %d - dynar_size = %d",
                        ans_cpt, (int) xbt_dynar_length(proc_data->async_answers));

                xbt_assert(ans_cpt >= 0,
                        "Node %d: ack reception error",
                        me->self.id);
            } // task_received is a request
        } // task reception OK
    } // reception loop

    // Error if max_wait reached
    if (ans_cpt > 0) {

        display_async_answers(me, 'I');
    }
    xbt_assert(ans_cpt == 0, "Node %d: [:%d] Wait error - cpt = %d",
            me->self.id,
            __LINE__,
            ans_cpt);

    XBT_VERB("Node %d: wait completed\n", me->self.id);

    display_async_answers(me, 'D');

    XBT_VERB("Node %d: end of wait_for_completion(): val_ret = '%s' - nok_id = %d",
            me->self.id,
            (ret == UPDATE_NOK ? "NOK" : "OK"),
            (ret == UPDATE_NOK ? nok_id : -1));

    display_sc(me, 'V');

    XBT_OUT();
    return ret;
}

/**
 * \brief Returns the total number of messages exchanged for a given node
 * \param id the node for which messages will be counted
 * \return The total number of messages
 */
/*
static int tot_msg_number(int id) {

    int i, tot = 0;

    for (i = 0; i < TYPE_NBR; i++) {
        tot += nb_messages[id][i];
    }
    return tot;
}
*/

/**
 * \brief Provide a copy of me's routing table
 * \param me the current node
 * \param cpy_brothers handler to the copy
 * \param cpy_bro_index copy of indexes
 */
static void make_copy_brothers(node_t me,
        s_node_rep_t ***cpy_brothers,
        int **cpy_bro_index) {

    XBT_IN();

    int stage, brother;

    // make a copy of all brothers
    (*cpy_bro_index) = xbt_new0(int, me->height);
    (*cpy_brothers) = xbt_new0(node_rep_t, me->height);
    for (stage = 0; stage < me->height; stage++) {

        (*cpy_brothers)[stage] = xbt_new0(s_node_rep_t, b);
        (*cpy_bro_index)[stage] = me->bro_index[stage];

        for (brother = 0; brother < (*cpy_bro_index)[stage]; brother++) {

            (*cpy_brothers)[stage][brother] = me->brothers[stage][brother];
        }
    }

    XBT_OUT();
}

/**
 * \brief Provide a copy of me's predecessors table
 * \param me the current node
 * \param cpy_preds handler for the copy
 * \param cpy_pred_index copy of indexes
 */
static void make_copy_preds(node_t me,
        s_node_rep_t ***cpy_preds,
        int **cpy_pred_index) {

    XBT_IN();

    int stage, pred;

    // make a copy of all predecessors
    (*cpy_pred_index) = xbt_new0(int, me->height);
    (*cpy_preds) = xbt_new0(node_rep_t, me->height);
    for (stage = 0; stage < me->height; stage++) {

        (*cpy_preds)[stage] = xbt_new0(s_node_rep_t, me->pred_index[stage]);
        (*cpy_pred_index)[stage] = me->pred_index[stage];

        for (pred = 0; pred < (*cpy_pred_index)[stage]; pred++) {

            (*cpy_preds)[stage][pred] = me->preds[stage][pred];
        }
    }

    XBT_OUT();
}

/**
 * \brief Returns an array of nodes that are in the given table but not in current one
 *        Tables dimensions have to be the same
 *        Returns NULL if tables are the same
 * \param me the current node
 * \param table the table to compare to
 * \param table_index given table indexes
 * \return array of nodes
 */
static node_rep_t compare_tables(node_t me, s_node_rep_t ***table, int **table_index) {

    XBT_IN();

    node_rep_t ret_nodes = NULL;
    int bro = 0;
    int stage = 0;
    int idx = 0;

    display_rout_table(me, 'V');
    // checks if stage sizes are the same
    for (stage = 1; stage < me->height; stage++) {
        xbt_assert(me->bro_index[stage] == (*table_index)[stage],
                "Node %d: [%s:%d] indexes aren't the same !!"
                "me->bro_index[%d] = %d | table_index[%d] = %d",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                stage,
                me->bro_index[stage],
                stage,
                (*table_index)[stage]);
    }

    // looks for any difference
    for (stage = 0; stage < me->height; stage++) {
        for (bro = 0; bro < me->bro_index[stage]; bro++) {
            if (me->brothers[stage][bro].id != (*table)[stage][bro].id) {
                if (ret_nodes == NULL) {

                    ret_nodes = xbt_new0(s_node_rep_t, idx + 1);
                } else {

                    ret_nodes = xbt_realloc(ret_nodes, (idx + 1) * sizeof(s_node_rep_t));
                }
                ret_nodes[idx] = (*table)[stage][bro];
                idx++;
            }
        }
    }

    // displays the result
    if (ret_nodes != NULL) {

        printf("Node %d: [%s:%d] diff nodes found !! : [ ",
        me->self.id,
        __FUNCTION__,
        __LINE__);

        for (bro = 0; bro < idx; bro++) {

            printf("%d ", ret_nodes[bro].id);
        }

        printf("]\n");
    }

    return ret_nodes;
    XBT_OUT();
}

/**
 * \brief Make a broadcast task to be handled by handle_task
 * \param me the current node
 * \param args arguments needed by the request to be broadcasted
 * \param task the created task
 */
static void make_broadcast_task(node_t me, u_req_args_t args, msg_task_t *task) {

    XBT_IN();

    //if (args.broadcast.first_call == 1) {

        //compteur[args.broadcast.type]++;
    //}

    req_data_t req_data = xbt_new0(s_req_data_t, 1);

    XBT_DEBUG("Node %d: [%s:%d] REQ_DATA = %p",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            req_data);

    req_data->type = TASK_BROADCAST;
    req_data->sender_id = me->self.id;
    req_data->recipient_id = me->self.id;

    //set_mailbox(me->self.id, req_data->answer_to);
    set_proc_mailbox(req_data->answer_to);
    set_mailbox(me->self.id, req_data->sent_to);

    req_data->args = args;

    (*task) = MSG_task_create("async", COMP_SIZE, COMM_SIZE, req_data);
    //MSG_task_set_name((*task), "async");

    XBT_DEBUG("Node %d in make_broadcast_task(): br_stage = %d - br_type = '%s - lead_br = %d'",
            me->self.id,
            args.broadcast.stage,
            debug_msg[args.broadcast.type],
            args.broadcast.lead_br);

    XBT_OUT();
}

/**
 * \brief Get current node state (the one that's on top of dynar states)
 * \param me the current node
 */
static s_state_t get_state(node_t me) {

    //XBT_IN();
    unsigned int len = xbt_dynar_length(me->states);
    s_state_t state;

    if (len == 0) {

        state.active = ' ';
        state.new_id = -1;
    } else {

        state_t *state_ptr = NULL;
        state_ptr = xbt_dynar_get_ptr(me->states, len - 1);

        state.active = (*state_ptr)->active;
        state.new_id = (*state_ptr)->new_id;
    }

    //XBT_OUT();
    return state;
}

/**
 * \brief Set current node state as 'active'
 * \param me the current node
 * \param new_id id of the new coming node that triggered this state change
 */
static void set_active(node_t me, int new_id) {

    XBT_IN();

    s_state_t state = get_state(me);
    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());

    int idx = -1;
    unsigned int iter = 0;
    recp_rec_t elem;

    // don't set active if another one has not been answered yet
    xbt_dynar_foreach(proc_data->async_answers, iter, elem){

        if (elem->type == TASK_BROADCAST &&
            elem->br_type == TASK_SET_ACTIVE &&
            elem->answer_data == NULL) {

            idx = iter;
        }
    }

    // get current state
    if (idx == -1  &&
        ((state.active == 'u' && (state.new_id == new_id || state.new_id == -1)) ||
             //(state.active == 'l' && state.new_id == new_id) ||
             (state_search(me, 'l', new_id) > -1) ||
             //(state.active != 'u' && state.active != 'l'))) {
             (state.active != 'u' && state_search(me, 'l', -1) == -1))) {

        xbt_dynar_reset(me->states);
        set_state(me, new_id, 'a');

        // release cs request
        cs_rel(me, new_id);

        state = get_state(me);
    } else {

        XBT_VERB("Node %d: '%c'/%d - set_active for %d rejected",
                me->self.id,
                state.active,
                state.new_id,
                new_id);
    }

    XBT_OUT();
}

/**
 * \brief Set current node state as 'update'
 * \param me the current node
 * \param new_id id of the new coming node that triggered this state change
 * \param new_node_prio new node's priority (ignored if -1)
 * \return A value indicating if set_update succeded or not
 */

static e_val_ret_t set_update(node_t me, int new_id, int new_node_prio) {

    XBT_IN();

    s_state_t state = get_state(me);
    e_val_ret_t val_ret;
    char test = 0;
    int nb_loops = 2;

    XBT_VERB("Node %d: [%s:%d]", me->self.id, __FUNCTION__, __LINE__);
    display_sc(me, 'V');
    display_states(me, 'V');

    do {
        nb_loops--;

        // test = 1 if this set_update is the one that's expected
        if (((me->cs_req == 1 && me->cs_new_id == new_id) || (me->cs_req == 0)) &&
                ((state.active == 'a') || (state.active == 'u' && state.new_id == new_id))) {

            test = 1;
        } else {

            test = 0;
        }

        // if test = 0, check priority
        if (new_node_prio > -1 && test == 0 && nb_loops > 0){

            cs_req(me, me->self.id, new_id, new_node_prio);
        }
    } while (new_node_prio > -1 && test == 0 && nb_loops > 0);

    XBT_VERB("Node %d: [%s:%d] '%c'/%d - test = %d - new_id = %d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            state.active,
            state.new_id,
            test,
            new_id);

    //if (test || (state.active == 'g')) {
    if (test || (state_search(me, 'g', -1) > -1)) {

        // push new state 'u'
        set_state(me, new_id, 'u');
        val_ret = UPDATE_OK;
    } else {

        if (state.active != 'a') {

            //if (state.active == 'l') {
            if (state_search(me, 'l', -1) > -1) {

                val_ret = UPDATE_OK;
            } else {

                // OK if requested 'u' state is already found in the dynar
                int found = state_search(me, 'u', new_id);
                if (found == -1) {

                    val_ret = UPDATE_NOK;
                } else {

                    val_ret = UPDATE_OK;
                }
            }
        } else {

            val_ret = UPDATE_NOK;
        }
    }

    state = get_state(me);
    XBT_VERB("Node %d: '%c'/%d end of set_update(): val_ret = '%s'",
            me->self.id,
            state.active,
            state.new_id,
            (val_ret == UPDATE_OK ?  "OK" : "NOK"));

    XBT_OUT();
    return val_ret;
}

/**
 * \brief Push new node's state on dynar states
 * \param me the current node
 * \param new_id id of the new coming node that triggered this state change
 * \param active the new state code
 */
static void set_state(node_t me, int new_id, char active) {

    //XBT_IN();

    /*
    if (state.active != active || state.new_id != new_id) {
    */

    s_state_t state = get_state(me);
    if (state_search(me, active, new_id) == -1) {

        state_t state_ptr = xbt_new0(s_state_t, 1);
        state_ptr->active = active;
        state_ptr->new_id = new_id;
        if (state.active == 'a' || active == 'u' || active == 'b') {

            xbt_dynar_push(me->states, &state_ptr);
        } else {

            xbt_dynar_unshift(me->states, &state_ptr);
        }
    } else {

        XBT_VERB("Node %d: [%s:%d] Current state already set. '%c'/%d",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                active,
                new_id);
    }

    state = get_state(me);
    XBT_VERB("Node %d: '%c'/%d - end of set_state() ... - new_node = %d",
            me->self.id,
            state.active,
            state.new_id,
            new_id);

    display_states(me, 'V');

    //XBT_OUT();
}

/**
 * \brief remove given state from states dynar (in case of set_update broadcast failed, for instance))
 * \param me the current node
 * \param new_id the new coming node
 * \param active the active state to be poped
 */
static void remove_state(node_t me, int new_id, char active) {
    XBT_IN();

    s_state_t state = get_state(me);
    XBT_VERB("Node %d: '%c'/%d for new node %d - start remove_state()",
            me->self.id,
            state.active,
            state.new_id,
            new_id);

    int idx = state_search(me, active, new_id);

    if (idx > -1) {

        state_t state_ptr = NULL;
        xbt_dynar_remove_at(me->states, idx, &state_ptr);
        xbt_free(state_ptr);
        state = get_state(me);
    }

    XBT_VERB("Node %d: '%c'/%d for new node %d - end remove_state() with idx = %d",
            me->self.id,
            state.active,
            state.new_id,
            new_id,
            idx);

    display_states(me, 'V');

    XBT_OUT();
}

/**
 * \brief Check if current node's routing table is consistent
 * \param me the node to be checked
 */
static int check(node_t me) {

    XBT_IN();
    int stage, brother;
    unsigned int cpt = 0;
    int chk = 0;
    dst_infos_t elem = NULL;
    for (stage = 0; stage < me->height - 1 && chk == 0; stage++) {

        for (brother = 0; brother < me->bro_index[stage] && chk == 0; brother++) {

            XBT_DEBUG("Node %d: stage %d - brother %d - size %d",
                    me->self.id,
                    stage,
                    me->brothers[stage][brother].id,
                    me->bro_index[stage + 1]);

            xbt_dynar_foreach(infos_dst, cpt, elem) {
                if (elem != NULL) {
                    if (elem->node_id == me->brothers[stage][brother].id) {

                        XBT_DEBUG("Node %d: cpt %u - node_id %d - size %d",
                                me->self.id,
                                cpt,
                                elem->node_id,
                                elem->size[stage + 1]);

                        if (elem->size[stage + 1] != me->bro_index[stage + 1]) {

                            chk = -1;
                            XBT_INFO("Node %d: check error - stage = %d -"
                                    " brother = %d - my size %d - his size %d",
                                    me->self.id,
                                    stage,
                                    me->brothers[stage][brother].id,
                                    me->bro_index[stage + 1],
                                    elem->size[stage + 1]);
                            XBT_INFO("%s", elem->routing_table);
                        }
                        /*
                           xbt_assert(elem->size[stage + 1] == me->bro_index[stage + 1],
                           "Node %d: check error - stage = %d -"
                           " brother = %d - my size %d - his size %d",
                           me->self.id,
                           stage,
                           me->brothers[stage][brother].id,
                           me->bro_index[stage + 1],
                           elem->size[stage + 1]);
                           */
                    }
                }
            }
        }
        XBT_DEBUG(" ");
    }
    XBT_OUT();
    return chk;
}

/**
 * \brief Create a fork process in charge of running run_tasks_queue()
 * \param me the current node
 * \param new_id new coming node
 */
static void call_run_tasks_queue(node_t me, int new_id) {
    XBT_IN();

    s_state_t state = get_state(me);
    XBT_DEBUG("Node %d: '%c'/%d - run tasks queue",
            me->self.id,
            state.active,
            state.new_id);

    proc_data_t proc_data = xbt_new0(s_proc_data_t, 1);
    proc_data->node = me;

    proc_data->task = NULL;
    proc_data->async_answers = xbt_dynar_new(sizeof(recp_rec_t), &xbt_free_ref);
    proc_data->sync_answers = xbt_dynar_new(sizeof(recp_rec_t), &xbt_free_ref);

    set_fork_mailbox(me->self.id,
            new_id,
            "tasks_queue",
            proc_data->proc_mailbox);

    XBT_VERB("Node %d: create fork process (tasks_queue)", me->self.id);

    MSG_process_create(proc_data->proc_mailbox,
            proc_run_tasks,
            proc_data,
            MSG_host_self());

    XBT_OUT();
}

/**
 * \brief Run tasks stored in tasks_queue dynar
 * \param me the current node
 */
static void run_tasks_queue(node_t me) {
    XBT_IN();

    // inits
    int cpt = -1;
    char is_leader = 0;
    char is_contact = 0;
    msg_task_t elem = NULL;
    msg_task_t cur_task = NULL;
    msg_task_t *task_ptr = NULL;
    req_data_t req_data = NULL;
    s_state_t state;
    me->run_task.run_state = IDLE;
    me->run_task.last_ret = UPDATE_NOK;
    e_run_state_t prev_state = RUNNING;
    u_ans_data_t answer;

    // sort queue by priority order
    sort_tasks_queue(me);

    float max = 14.3;
    float min = 1.2;
    double sleep_time = 0;
    int mem_head_id = -1;

    XBT_DEBUG("Node %d: [%s:%d] set run_state = {%s - %s}",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            debug_run_msg[me->run_task.run_state],
            debug_ret_msg[me->run_task.last_ret]);

    do {

        //log
        if (xbt_dynar_length(me->tasks_queue) > 0) {

            task_ptr = xbt_dynar_get_ptr(me->tasks_queue, 0);

            xbt_assert(task_ptr != NULL && *task_ptr != NULL,
                    "Node %d: [%s:%d] task_ptr shouldn't be NULL here !",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__);

            req_data = MSG_task_get_data(*task_ptr);

            if (mem_head_id == -1 || mem_head_id != req_data->args.cnx_req.new_node_id) {

                mem_head_id = req_data->args.cnx_req.new_node_id;

                XBT_INFO("Node %d: [%s:%d] nb_ins_nodes = %d, queue_length = %d, head new_node = %d",
                        me->self.id,
                        __FUNCTION__,
                        __LINE__,
                        nb_ins_nodes,
                        (int)xbt_dynar_length(me->tasks_queue),
                        req_data->args.cnx_req.new_node_id);
            }
        }

        state = get_state(me);

        // run delayed tasks, if any
        if (xbt_dynar_is_empty(me->delayed_tasks) == 0) {

            run_delayed_tasks(me);
        }

        // display tasks queue only once
        if (state.active != 'b') {
            if (me->run_task.run_state != prev_state) {

                prev_state = me->run_task.run_state;
                XBT_VERB("Node %d: [%s:%d] '%c'/%d - run_state = %s - last_ret = %s",
                        me->self.id,
                        __FUNCTION__,
                        __LINE__,
                        state.active,
                        state.new_id,
                        debug_run_msg[me->run_task.run_state],
                        debug_ret_msg[me->run_task.last_ret]);

                display_tasks_queue(me);
            }
        }

        if (xbt_dynar_is_empty(me->tasks_queue) == 0 && state.active == 'a') {

            // something to do
            if (me->run_task.run_state == IDLE) {

                if (me->run_task.last_ret != UPDATE_NOK) {

                    XBT_VERB("Node %d: task done - ret = '%s' - may be shifted out",
                            me->self.id,
                            debug_ret_msg[me->run_task.last_ret]);
                } else {

                    XBT_VERB("Node %d: something to do - cpt = %d", me->self.id, cpt);
                }

                // no task running
                if (me->run_task.last_ret == UPDATE_NOK) {

                    // last run was not OK
                    //cpt++;
                    if (cpt >= MAX_CNX) {

                        // too many attempts
                        task_ptr = xbt_dynar_get_ptr(me->tasks_queue, 0);

                        if (!strcmp(MSG_task_get_name(*task_ptr),"")) {
                            xbt_assert(1 == 0,
                                    "Node %d: [%s:%d] Name empty !",
                                    me->self.id,
                                    __FUNCTION__,
                                    __LINE__);
                        }

                        xbt_assert(task_ptr != NULL && *task_ptr != NULL,
                                "Node %d: [%s:%d] task_ptr shouldn't be NULL here (1) !",
                                me->self.id,
                                __FUNCTION__,
                                __LINE__);

                        req_data = MSG_task_get_data(*task_ptr);

                        XBT_VERB("Node %d: [%s:%d] Too many attemps (%d) for task {'%s - %s' from %d for new node %d}",
                                me->self.id,
                                __FUNCTION__,
                                __LINE__,
                                cpt,
                                MSG_task_get_name(*task_ptr),
                                debug_msg[req_data->type],
                                req_data->sender_id,
                                req_data->args.cnx_req.new_node_id);

                        // shift queue to remove head task

                        XBT_VERB("Node %d: [%s:%d] last run was '%s' : shift queue and answer sender %d - run_state = %s - nb_attempts = %d",
                                me->self.id,
                                __FUNCTION__,
                                __LINE__,
                                debug_ret_msg[me->run_task.last_ret],
                                req_data->sender_id,
                                debug_run_msg[me->run_task.run_state],
                                req_data->args.cnx_req.try);

                        cpt = 0;

                        // tell sender to test another contact
                        answer.cnx_req.val_ret = UPDATE_NOK;
                        answer.cnx_req.new_contact.id = -1;
                        answer.cnx_req.try = req_data->args.cnx_req.try;

                        send_ans_sync(me,
                                req_data->args.cnx_req.new_node_id,
                                req_data->type,
                                req_data->sender_id,
                                req_data->answer_to,
                                answer);

                        //TODO : pourquoi ne faut-il pas libérer ici ?

                        data_req_free(me, &req_data);
                        //task_free(task_ptr);

                        xbt_dynar_shift(me->tasks_queue, NULL);
                    }
                } else {

                    // last run was OK or FAILED : shift queue to remove head task
                    task_ptr = xbt_dynar_get_ptr(me->tasks_queue, 0);
                    req_data = MSG_task_get_data(*task_ptr);
                    data_req_free(me, &req_data);
                    task_free(task_ptr);

                    XBT_VERB("Node %d: [%s:%d] last run was '%s' : shift queue - run_state = %s",
                            me->self.id,
                            __FUNCTION__,
                            __LINE__,
                            debug_ret_msg[me->run_task.last_ret],
                            debug_run_msg[me->run_task.run_state]);

                    xbt_dynar_shift(me->tasks_queue, NULL);

                    // get ready for next loop
                    me->run_task.last_ret = UPDATE_NOK;
                    cpt = 0;
                }

                // sort queue by priority order
                sort_tasks_queue(me);

                state = get_state(me);
                if (xbt_dynar_is_empty(me->tasks_queue) == 0 && state.active == 'a') {

                    // run top request

                    XBT_VERB("Node %d: [%s:%d] run head request",
                            me->self.id,
                            __FUNCTION__,
                            __LINE__);

                    task_ptr = xbt_dynar_get_ptr(me->tasks_queue, 0);

                    xbt_assert(task_ptr != NULL && *task_ptr != NULL,
                            "Node %d: [%s:%d] task_ptr shouldn't be NULL here (2)!",
                            me->self.id,
                            __FUNCTION__,
                            __LINE__);

                    req_data = MSG_task_get_data(*task_ptr);

                    xbt_assert(req_data->type == TASK_CNX_REQ,
                            "Node %d: [%s:%d] task shouldn't be other than CNX_REQ : '%s'",
                            me->self.id,
                            __FUNCTION__,
                            __LINE__,
                            debug_msg[req_data->type]);

                    XBT_VERB("Node %d: launch fork process for task {'%s - %s' from %d for new node %d}",
                            me->self.id,
                            MSG_task_get_name(*task_ptr),
                            debug_msg[req_data->type],
                            req_data->sender_id,
                            req_data->args.cnx_req.new_node_id);

                    // increment number of attempts only if me is leader
                    //is_leader = me->self.id == me->brothers[0][0].id;
                    is_contact = req_data->sender_id == req_data->args.cnx_req.new_node_id;
                    if (is_contact) {

                        req_data->args.cnx_req.try++;
                    }

                    xbt_assert(*task_ptr != NULL, "[%s:%d] STOP", __FUNCTION__, __LINE__);
                    cpt++;
                    launch_fork_process(me, *task_ptr);
                }
            }
        }

        // wait a random delay
        srand(time(NULL));
        sleep_time = ((double)rand() * (double)(max - min) / (double)RAND_MAX) + (double)min;
        MSG_process_sleep(sleep_time);

    } while (nb_ins_nodes < nb_nodes);

    XBT_OUT();
}

/**
 * \brief Run tasks stored for a delayed execution
 * \param me the current node
 */
static void run_delayed_tasks(node_t me) {

    XBT_IN();

    // get current values
    s_state_t state = get_state(me);
    unsigned int nb_elems = xbt_dynar_length(me->delayed_tasks);

    // display delayed tasks
    if (nb_elems > 0 && state.active == 'a') {

        XBT_VERB("Node %d: [%s:%d] start run - '%c'/%d -  delayed_tasks length = %u",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                state.active,
                state.new_id,
                nb_elems);
    }

    display_sc(me, 'D');

    msg_task_t *task_ptr = NULL;
    req_data_t req_data = NULL;
    msg_task_t elem;
    int cpt = 0;
    int idx = 0;
    int k = 0;
    e_val_ret_t val_ret = OK;

    /* run delayed CNX_GROUPS for new node new_id if current state is 'u'/new_id */
    if (state.active == 'u' && xbt_dynar_is_empty(me->delayed_tasks) == 0) {

        display_delayed_tasks(me);

        u_req_args_t req_args;

        for (cpt = 0; cpt < nb_elems && state.active == 'u'; cpt++) {

            task_ptr = xbt_dynar_get_ptr(me->delayed_tasks, cpt);
            req_data = MSG_task_get_data(*task_ptr);

            if (req_data->type == TASK_CNX_GROUPS) {

                req_args = req_data->args;
                if (req_args.cnx_groups.new_node_id == state.new_id) {

                    XBT_VERB("Node %d: [%s:%d] run - '%c'/%d - run CNX_GROUPS (task[%d])",
                            me->self.id,
                            __FUNCTION__,
                            __LINE__,
                            state.active,
                            state.new_id,
                            cpt);

                    // run CNX_GROUPS
                    xbt_dynar_remove_at(me->delayed_tasks, cpt, &elem);
                    cpt--;
                    handle_task(me, &elem);

                    /*TODO : il semble qu'il y ait ici une possiblité de boucle infinie si les
                             CNX_GROUPS sont à nouveau stockées. Possible ? */

                    // state may have been changed by handle_task()
                    state = get_state(me);
                }
            }
        }
    }

    // run other delayed tasks
    if (state.active == 'a' && xbt_dynar_is_empty(me->delayed_tasks) == 0) {

        if (nb_elems > 0) {

            //int nb_cnx_req = 0;
            req_data_t req = NULL;
            //e_task_type_t buf_type = TASK_NULL;
            char is_contact = 0;
            int buf_new_node_id = -1;
            int mem_nb_elems = 0;

            do {
                for (idx = 0; idx < nb_elems && state.active == 'a'; ) {

                    if (mem_nb_elems > nb_elems) {

                        display_delayed_tasks(me);
                    }

                    // read head element
                    task_ptr = xbt_dynar_get_ptr(me->delayed_tasks, idx);
                    req = MSG_task_get_data(*task_ptr);
                    elem = *task_ptr;

                    // store current length
                    mem_nb_elems = nb_elems;

                    // there shouldn't be any CNX_REQ in that dynar
                    xbt_assert(req->type != TASK_CNX_REQ,
                            "Node %d: [%s:%d] There shouldn't be any CNX_REQ in delayed_tasks anymore !! - task[%d] is {'%s - %s' from %d}",
                            me->self.id,
                            __FUNCTION__,
                            __LINE__,
                            idx,
                            MSG_task_get_name(*task_ptr),
                            debug_msg[req->type],
                            req->sender_id);

                    XBT_VERB("Node %d: [%s:%d] run - '%c'/%d - task[%d] is {'%s - %s' from %d}",
                            me->self.id,
                            __FUNCTION__,
                            __LINE__,
                            state.active,
                            state.new_id,
                            idx,
                            MSG_task_get_name(*task_ptr),
                            debug_msg[req->type],
                            req->sender_id);

                    //buf_type = req->type;
                    is_contact = (req->sender_id == req->args.cnx_req.new_node_id);  // new_node_id is the first field of every request
                    buf_new_node_id = req->args.cnx_req.new_node_id;

                    // run task
                    val_ret = handle_task(me, &elem);

                    XBT_VERB("Node %d: [%s:%d] back to run_delayed_tasks : val_ret = %s",
                            me->self.id,
                            __FUNCTION__,
                            __LINE__,
                            debug_ret_msg[val_ret]);

                    //TODO : expliquer. Voir si le cas se produit ou pas
                    if (val_ret == UPDATE_NOK) {

                        state = get_state(me);
                        if (state.active == 'a' && me->cs_req == 1 && me->cs_new_id == buf_new_node_id) {
                            me->cs_req = 0;
                            me->cs_req_time = MSG_get_clock();

                            XBT_VERB("Node %d: [%s:%d] cs_req reset",
                                    me->self.id,
                                    __FUNCTION__,
                                    __LINE__);

                            display_sc(me, 'V');
                        }
                    }

                    if (val_ret  == OK ||
                        val_ret  == UPDATE_OK ||
                        (val_ret == UPDATE_NOK && !is_contact) ||       //TODO : ne pourrait-on pas utiliser FAILED plutôt que cette formule ici ?
                        val_ret  == STORED) {

                        /* remove task from dynar if it succeeded or if failed but me is not contact
                           (contact will have to transmit again to its current leader)
                           Also remove in case of STORED so that this task isn't duplicated */
                        xbt_dynar_remove_at(me->delayed_tasks, idx, &elem);
                        nb_elems--;

                        /*
                        if (buf_type == TASK_CNX_REQ) {

                            nb_cnx_req--;
                        }
                        */

                        XBT_VERB("Node %d: [%s:%c] task[%d] removed",
                                me->self.id,
                                __FUNCTION__,
                                __LINE__,
                                idx);

                    } else {

                        // if on contact and task didn't run sucessfully, don't remove it
                        XBT_VERB("Node %d: [%s:%d] task[%d] %s not removed",
                                me->self.id,
                                __FUNCTION__,
                                __LINE__,
                                idx,
                                debug_ret_msg[val_ret]);

                        idx++;
                    }

                    //req = NULL;
                    state = get_state(me);

                    //dynar may have been modified by handle_task           //TODO : pourquoi avoir commenté cette partie ?
                    /*
                    nb_elems = (int) xbt_dynar_length(me->delayed_tasks);
                    if (nb_elems != mem_nb_elems) {

                        XBT_VERB("Node %d - run %c - in run_delayed_tasks(): old length = %d -"
                                " new length = %d",
                                me->self.id,
                                c,
                                mem_nb_elems,
                                nb_elems);

                        if (nb_cnx_req > nb_elems) {

                            nb_cnx_req = nb_elems;
                        }
                    }
                    */
                } // next task
            } while (val_ret == OK && nb_elems > 0 && state.active == 'a');
        }

        state = get_state(me);
        XBT_VERB("Node %d: run - '%c'/%d - end of delayed tasks execution. dynar_length = %lu",
                me->self.id,
                state.active,
                state.new_id,
                xbt_dynar_length(me->delayed_tasks));

        // one of the delayed tasks may have stored a task itself
        /*
        if (xbt_dynar_length(me->delayed_tasks) > 0) {

            run_delayed_tasks(me);
        }
        */
    }
    XBT_OUT();
}

/**
 * \brief Frees the memory used by a node
 * \param me the current node
 */
static void node_free(node_t me) {

    XBT_IN();

    xbt_dynar_free(&(me->tasks_queue));
    xbt_dynar_free(&(me->delayed_tasks));
    xbt_dynar_free(&(me->states));

    /*
    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());
    xbt_dynar_free(&(proc_data->async_answers));
    xbt_dynar_free(&(proc_data->sync_answers));
    xbt_free(proc_data);
    proc_data = NULL;
    */

    /*
    // will be freed with dst_infos dynar
    xbt_free(me->dst_infos.load);
    me->dst_infos.load = NULL;
    */

    xbt_free(me->bro_index);
    me->bro_index = NULL;
    xbt_free(me->pred_index);
    me->pred_index = NULL;
    xbt_free(me->comm_received);
    me->comm_received = NULL;

    int i;
    for (i = 0; i < me->height; i++) {

        xbt_free(me->brothers[i]);
        me->brothers[i] = NULL;
        xbt_free(me->preds[i]);
        me->preds[i] = NULL;
    }
    xbt_free(me->brothers);
    me->brothers = NULL;
    xbt_free(me->preds);
    me->preds = NULL;

    XBT_OUT();
}

/**
 * \brief Free exchanged data
 * \param me current node
 * \param answer_data data to be freed
 */
static void data_ans_free(node_t me, ans_data_t *answer_data) {

    XBT_IN();

    if (*answer_data != NULL) {

        XBT_DEBUG("Node %d: free data '%s' from %d to %d",
                me->self.id,
                debug_msg[(*answer_data)->type],
                (*answer_data)->sender_id,
                (*answer_data)->recipient_id);

        xbt_free(*answer_data);
        *answer_data = NULL;
    }

    XBT_OUT();
}

/**
 * \brief Free exchanged data
 * \param me current node
 * \param req_data data to be freed
 */
static void data_req_free(node_t me, req_data_t *req_data) {

    XBT_IN();

    if (*req_data != NULL) {

        XBT_DEBUG("Node %d: free data '%s' - %p - from %d to %d",
                me->self.id,
                debug_msg[(*req_data)->type],
                *req_data,
                (*req_data)->sender_id,
                (*req_data)->recipient_id);

        xbt_free(*req_data);
        *req_data = NULL;
    }

    XBT_OUT();
}

/**
 * \brief Free an element of dynar infos_dst when a Replace occurs
 * \param elem_ptr a pointer to a dynar entry
 */
static void elem_free(void *elem_ptr) {

    XBT_IN();

    dst_infos_t* ptr = (dst_infos_t*)elem_ptr;
    if (*ptr != NULL) {

        xbt_free((*ptr)->routing_table);
        (*ptr)->routing_table = NULL;

        /*
           xbt_free((*ptr)->load);      //TODO: voir pourquoi cette libération pose problème
           (*ptr)->load = NULL;

           xbt_free((*ptr)->size);
           (*ptr)->size = NULL;
        */

        xbt_free(*ptr);
        *ptr = NULL;
    }

    XBT_OUT();
}

static void display_tasks_queue(node_t me) {
    XBT_IN();

    int k = 0;
    msg_task_t *task_ptr = NULL;
    req_data_t req_data = NULL;
    unsigned int nb_elems = xbt_dynar_length(me->tasks_queue);
    XBT_VERB("Node %d: *==== Tasks Queue ====*\tnb_elems = %d", me->self.id, nb_elems);

    for (k = 0; k < nb_elems; k++) {

        XBT_DEBUG("me->tasks_queue = %p", me->tasks_queue);    //TODO: ne pas oublier

        task_ptr = xbt_dynar_get_ptr(me->tasks_queue, k);

        XBT_DEBUG("*task_ptr = %p - name = %p/%s", *task_ptr, MSG_task_get_name(*task_ptr), MSG_task_get_name(*task_ptr));

        req_data = MSG_task_get_data(*task_ptr);

        if (MSG_task_get_name(*task_ptr) == NULL) {

            XBT_VERB("Node %d: \ttask[%d] = NULL", me->self.id, k);
        } else {

            xbt_assert(*task_ptr != NULL, "[%s:%d] STOP", __FUNCTION__, __LINE__);
            XBT_VERB("Node %d: \t%p: task[%d] = {'%s - %s' from %d for new node %d} - prio = %d",
                    me->self.id,
                    *task_ptr,
                    k,
                    MSG_task_get_name(*task_ptr),
                    debug_msg[req_data->type],
                    req_data->sender_id,
                    req_data->args.cnx_req.new_node_id,
                    (req_data->type == TASK_CNX_REQ ? req_data->args.cnx_req.cs_new_node_prio : -100));
        }
    }
    XBT_VERB("Node %d: *==== End Tasks Queue ====*", me->self.id);

    XBT_OUT();
}

/*
 * \brief Comparison function used to sort tasks_queue dynar
 * \param arg1 first element
 * \param arg2 second element
 * \return int value
 */
static int compar_fn(const void *arg1, const void *arg2) {
    XBT_IN();

    msg_task_t *task1 = (msg_task_t*)arg1;
    msg_task_t *task2 = (msg_task_t*)arg2;

    xbt_assert(task1 != NULL && *task1 != NULL,
            "[%s:%d] task1 should not be NULL !!",
            __FUNCTION__,
            __LINE__);

    xbt_assert(task2 != NULL && *task2 != NULL,
            "[%s:%d] task2 should not be NULL !!",
            __FUNCTION__,
            __LINE__);

    req_data_t req_data1 = MSG_task_get_data(*task1);
    req_data_t req_data2 = MSG_task_get_data(*task2);

    xbt_assert(req_data1->type == TASK_CNX_REQ && req_data2->type == TASK_CNX_REQ,
            "[%s:%d] task should be CNX_REQ !! - task_1 = %s - task 2 = %s",
            __FUNCTION__,
            __LINE__,
            debug_msg[req_data1->type],
            debug_msg[req_data2->type]);

    return (req_data1->args.cnx_req.cs_new_node_prio > req_data2->args.cnx_req.cs_new_node_prio);

    XBT_OUT();
}

/**
 * \brief Sort tasks queue according to new nodes' priority
 * \param me the current node
 */
static void sort_tasks_queue(node_t me) {
    XBT_IN();

    if (xbt_dynar_length(me->tasks_queue) > 1) {

        XBT_DEBUG("Node %d: [%s:%d] sort tasks queue",
                me->self.id,
                __FUNCTION__,
                __LINE__);

        XBT_DEBUG("Node %d: before sort", me->self.id);
        //display_tasks_queue(me);

        xbt_dynar_sort(me->tasks_queue, compar_fn);

        XBT_DEBUG("Node %d: after sort", me->self.id);
        //display_tasks_queue(me);
    }

    XBT_OUT();
}

/**
 * \brief display delayed tasks
 * \param me the current node
 */
static void display_delayed_tasks(node_t me) {
    XBT_IN();

    int k = 0;
    msg_task_t *task_ptr = NULL;
    req_data_t req_data = NULL;
    unsigned int nb_elems = xbt_dynar_length(me->delayed_tasks);
    XBT_VERB("Node %d: *==== Delayed Tasks  ====*\tnb_elems = %d", me->self.id, nb_elems);

    for (k = 0; k < nb_elems; k++) {

        XBT_DEBUG("me->delayed_tasks = %p", me->delayed_tasks);    //TODO: ne pas oublier

        task_ptr = xbt_dynar_get_ptr(me->delayed_tasks, k);

        XBT_DEBUG("*task_ptr = %p - name = %p/%s", *task_ptr, MSG_task_get_name(*task_ptr), MSG_task_get_name(*task_ptr));

        req_data = MSG_task_get_data(*task_ptr);

        if (MSG_task_get_name(*task_ptr) == NULL) {                     //TODO : faire un try/catch ici ?

            XBT_VERB("Node %d: \ttask[%d] = NULL", me->self.id, k);
        } else {

            XBT_VERB("Node %d: \t%p: task[%d] = {'%s - %s' from %d for new node %d}",
                    me->self.id,
                    *task_ptr,
                    k,
                    MSG_task_get_name(*task_ptr),
                    debug_msg[req_data->type],
                    req_data->sender_id,
                    req_data->args.cnx_req.new_node_id);
        }
    }
    XBT_VERB("Node %d: *==== End of Delayed Tasks ====*", me->self.id);

    XBT_OUT();
}

static void display_async_answers(node_t me, char log) {

    XBT_IN();

    unsigned int cpt = 0;
    recp_rec_t elem = NULL;
    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());

    switch(log) {

        case 'D':
            XBT_DEBUG("Node %d: dynar of %lu expected answers",
                    me->self.id,
                    xbt_dynar_length(proc_data->async_answers));
            break;

        case 'V':
            XBT_VERB("Node %d: dynar of %lu expected answers",
                    me->self.id,
                    xbt_dynar_length(proc_data->async_answers));
            break;

        case 'I':
            XBT_INFO("Node %d: dynar of %lu expected answers",
                    me->self.id,
                    xbt_dynar_length(proc_data->async_answers));
            break;
    }

    xbt_dynar_foreach(proc_data->async_answers, cpt, elem) {

        switch(log) {

            case 'D':
                XBT_DEBUG("elem[%d] = {%s | %s - %d-'%s' - %p - new_node %d}",
                        cpt,
                        debug_msg[elem->type],
                        debug_msg[elem->br_type],
                        elem->recp.id,
                        elem->recp.mailbox,
                        elem->answer_data,
                        elem->new_node_id);
                break;

            case 'V':
                XBT_VERB("elem[%d] = {%s | %s - %d-'%s' - %p - new_node %d}",
                        cpt,
                        debug_msg[elem->type],
                        debug_msg[elem->br_type],
                        elem->recp.id,
                        elem->recp.mailbox,
                        elem->answer_data,
                        elem->new_node_id);
                break;

            case 'I':
                XBT_INFO("elem[%d] = {%s | %s - %d-'%s' - %p - new_node %d}",
                        cpt,
                        debug_msg[elem->type],
                        debug_msg[elem->br_type],
                        elem->recp.id,
                        elem->recp.mailbox,
                        elem->answer_data,
                        elem->new_node_id);
                break;
        }
    }

    XBT_OUT();
}

/**
 * \brief Displays current node's dynar states
 * \param me the current node
 */
static void display_states(node_t me, char mode) {

    XBT_IN();

    unsigned int iter = 0;
    state_t elem = NULL;

    if (mode == 'I') {

        XBT_INFO("Node %d: states dynar", me->self.id);
    } else {

        if (mode == 'D') {

            XBT_DEBUG("Node %d: states dynar", me->self.id);
        } else {

            XBT_VERB("Node %d: states dynar", me->self.id);
        }
    }

    xbt_dynar_foreach(me->states, iter, elem) {

        if (mode == 'I') {

            XBT_INFO(" {[%d] --> '%c'/%d}",
                    iter,
                    elem->active,
                    elem->new_id);
        } else {

            XBT_VERB(" {[%d] --> '%c'/%d}",
                    iter,
                    elem->active,
                    elem->new_id);
        }

    }

    if (mode == 'I') {

        XBT_INFO("");
    } else {

        XBT_VERB("");
    }

    XBT_OUT();
}

/**
 * \brief display Critical Section informations
 * \param me the current node
 * \param mode logging level
 */
static void  display_sc(node_t me, char mode) {
    XBT_IN();

    s_state_t state = get_state(me);
    switch (mode) {
        case 'V':
            XBT_VERB("Node %d: '%c'/%d - cs_req = %d cs_new_id = %d cs_req_time = %f cs_new_node_prio = %d",
                    me->self.id,
                    state.active,
                    state.new_id,
                    me->cs_req,
                    me->cs_new_id,
                    me->cs_req_time,
                    me->cs_new_node_prio);
            break;

        case 'I':
            XBT_INFO("Node %d: '%c'/%d - cs_req = %d cs_new_id = %d cs_req_time = %f cs_new_node_prio = %d",
                    me->self.id,
                    state.active,
                    state.new_id,
                    me->cs_req,
                    me->cs_new_id,
                    me->cs_req_time,
                    me->cs_new_node_prio);
            break;

        case 'D':
            XBT_DEBUG("Node %d: '%c'/%d - cs_req = %d cs_new_id = %d cs_req_time = %f cs_new_node_prio = %d",
                    me->self.id,
                    state.active,
                    state.new_id,
                    me->cs_req,
                    me->cs_new_id,
                    me->cs_req_time,
                    me->cs_new_node_prio);
            break;
    }

    XBT_OUT();
}

/**
 * \brief Look for a request in dynar me->sync_answers, according to some
 *        parameters
 * \param me the current node
 * \param type the request task type
 * \param br_type the broadcasted request task type
 * \param recp_id the recipient of the request
 * \return the index of found request (-1 if not found)
 */
/*
   static int sync_answers_search(node_t me,
   e_task_type_t type,
   e_task_type_t br_type,
   int recp_id) {
   XBT_IN();

   int pos = -1;
   unsigned int iter = 0;
   recp_rec_t elem;

   xbt_dynar_foreach(me->sync_answers, iter, elem) {

   XBT_DEBUG("Node %d dynar traversal: iter = %d - idx = %d --> {type:"
   " '%s - %s' - recipient: %d - data: %p}",
   me->self.id,
   iter,
   pos,
   debug_msg[elem->type],
   debug_msg[elem->br_type],
   elem->recp.id,
   elem->answer_data);

   if (elem->type == type &&
   elem->br_type == br_type &&
   elem->recp.id == recp_id &&
   elem->answer_data == NULL) {

// record found
pos = iter;
}
}

XBT_OUT();
return pos;
}
*/

/**
 * \brief Returns the index of predecessor 'id' for a given stage
 *        (-1 if not found)
 * \param me the current node
 * \param stage the given stage
 * \param id the predecessor id to look for
 * \return The index of given predecessor
 */
static int index_pred(node_t me, int stage, int id) {

    XBT_IN();
    int idx = 0;

    xbt_assert(stage < me->height,
            "Node %d: in index_pred, stage %d is higher than DST height %d",
            me->self.id,
            stage,
            me->height - 1);

    while ((idx < me->pred_index[stage]) && (me->preds[stage][idx].id != id)) {

        idx++;
    }
    if (idx == me->pred_index[stage]) {

        XBT_DEBUG("Node %d not found as a predecessor of node %d for stage %d",
                id,
                me->self.id,
                stage);

        idx = -1;
    }
    XBT_OUT();
    return idx;
}

/**
 * \brief Sends a request task to another node and gets the answer back
 * \param me the current node
 * \param type type of request task
 * \param recipient_id id of the requested node
 * \param args arguments needed by this type of task
 * \param answer_data data returned by the requested node
 * \return Communication error code
 */
static msg_error_t send_msg_sync(node_t me,
        e_task_type_t type,
        int recipient_id,
        u_req_args_t args,
        ans_data_t *answer_data) {

    XBT_IN();

    e_val_ret_t ret = 0;

    req_data_t req_data = xbt_new0(s_req_data_t, 1);
    req_data_t cpy_req_data = xbt_new0(s_req_data_t, 1);

    // init request datas
    req_data->type = type;
    req_data->sender_id = me->self.id;
    req_data->recipient_id = recipient_id;
    req_data->args = args;

    // create mailboxes
    set_mailbox(req_data->recipient_id, req_data->sent_to);
    set_proc_mailbox(req_data->answer_to);

    // req_data may be altered during loops
    *cpy_req_data = *req_data;

    // create and send task with data
    msg_task_t task_sent = MSG_task_create("sync", COMP_SIZE, COMM_SIZE, req_data);

    XBT_VERB("Node %d: Sending sync request '%s %s' to %d for new node %d",
            req_data->sender_id,
            debug_msg[req_data->type],
            debug_msg[(req_data->type == TASK_BROADCAST ? req_data->args.broadcast.type : TASK_NULL)],
            req_data->recipient_id,
            req_data->args.get_rep.new_node_id);

    float max_wait = MSG_get_clock() + COMM_TIMEOUT;
    msg_comm_t comm = NULL;
    msg_error_t res = MSG_OK;
    int max_loops = 10;
    int loop_cpt = 0;
    proc_data_t proc_data = NULL;
    recp_rec_t req_elem = NULL;
    recp_rec_t ans_elem = NULL;
    recp_rec_t *elem_ptr = NULL;


    // send request
    do {

        // async send
        comm = MSG_task_isend(task_sent, req_data->sent_to);

        // push onto proc sync_answers dynar
        req_elem = xbt_new0(s_recp_rec_t, 1);
        req_elem->type = type;
        req_elem->recp.id = recipient_id;
        set_mailbox(recipient_id, req_elem->recp.mailbox);
        req_elem->br_type = (type == TASK_BROADCAST ? args.broadcast.type : TASK_NULL);
        req_elem->answer_data = NULL;

        // every request data's first field is new_node_id - so get_rep is OK
        // NOTE : si c'est pas bon, il faut modifier send_msg_sync pour ajouter new_node_id à ses arguments
        req_elem->new_node_id = req_data->args.get_rep.new_node_id;

        proc_data = MSG_process_get_data(MSG_process_self());
        xbt_dynar_push(proc_data->sync_answers, &req_elem);

        XBT_DEBUG("Node %d: {type = '%s - %s' - recipient = %d - answer_data = %p}"
                " pushed, dynar length = %lu",
                me->self.id,
                debug_msg[req_elem->type],
                debug_msg[req_elem->br_type],
                req_elem->recp.id,
                req_elem->answer_data,
                xbt_dynar_length(proc_data->sync_answers));

        /* max_wait has to be used in case of receiver process is down.
         * If host is shut down, MSG_TRANSFER_FAILURE is raised but nothing happens if receiver process is down.
         * TODO : à vérifier
         * TODO : comment ajuster COMM_TIMEOUT en cas de mauvais réseau ou de grand DST ?
         * TODO : tester des cas de TIMEOUT pour voir s'ils sont bien pris en compte
         */
        while (!MSG_comm_test(comm) && MSG_get_clock() <= max_wait) {

            MSG_process_sleep(1.2);
        }
        if (MSG_get_clock() > max_wait || comm == NULL || MSG_comm_get_status(comm) == MSG_TRANSFER_FAILURE) {

            res = MSG_TRANSFER_FAILURE;
            xbt_assert(1 == 0, "Node %d: [%s:%d] TRANSFER FAILURE - max_wait = %f - clock = %f - comm = %p",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    max_wait,
                    MSG_get_clock(),
                    comm);
        } else {

            res = MSG_comm_get_status(comm);
        }

        // only max_loops attempts are allowed
        if (res == MSG_TIMEOUT) {

            loop_cpt++;

            // get ready for another try
            xbt_dynar_pop(proc_data->sync_answers, &req_elem);
            xbt_free(req_elem);
        }

    } while (res == MSG_TIMEOUT && loop_cpt < max_loops);


    xbt_assert(loop_cpt < max_loops,
            "Node %d: [%s:%d] too many sending TIMEOUT in send_msg_sync() to %d - '%s' - max_wait = %f - loop_cpt = %d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            recipient_id,
            req_data->sent_to,
            max_wait,
            loop_cpt);

    xbt_assert(res == MSG_OK, "Node %d: [%s:%d] sending failure '%s' in send_msg_sync() to %d - '%s' - max_wait = %f",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            debug_res_msg[res],
            recipient_id,
            req_data->sent_to,
            max_wait);

    MSG_comm_destroy(comm);
    comm = NULL;

    // count messages
    nb_messages[args.get_rep.new_node_id][cpy_req_data->type]++;
    if (cpy_req_data->type == TASK_BROADCAST) {

        nb_br_messages[args.get_rep.new_node_id][cpy_req_data->args.broadcast.type]++;
    }


    // NOTE : req_data may be freed by receiver and mustn't be used anymore by
    // the sender

    // reception loop to get the answer back
    msg_task_t task_received = NULL;
    ans_data_t ans = NULL;
    res = MSG_OK;
    int dynar_idx;
    int idx;
    s_state_t state;

    while (res != MSG_TIMEOUT || cpy_req_data->type != TASK_GET_REP) {

        dynar_idx = -1;

        // sync reception
        state = get_state(me);
        XBT_VERB("Node %d: '%c'/%d - Waiting for sync answer ... - task_sent = %p",
                cpy_req_data->sender_id,
                state.active,
                state.new_id,
                task_sent);

        if (cpy_req_data->type == TASK_GET_REP) {

            res = MSG_task_receive_with_timeout(&task_received, cpy_req_data->answer_to, MAX_WAIT_GET_REP);
        } else {

            res = MSG_task_receive(&task_received, cpy_req_data->answer_to);
        }

        if (res != MSG_OK) {

            // TODO : si TIMEOUT et GET_REP, il faut laisser tomber
            // reception failure
            xbt_assert(1 == 0, "Node %d: Failed to receive the answer to my '%s' request from %s"
                    " result : %s (line %d)",
                    cpy_req_data->sender_id,
                    debug_msg[cpy_req_data->type],
                    cpy_req_data->sent_to,
                    debug_res_msg[res],
                    __LINE__);

            task_free(&task_received);
        } else {

            // reception success, extract data from task
            ans = MSG_task_get_data(task_received);
            req_data_t req = (req_data_t)ans;

            // get answer data
            *answer_data = ans;

            // log
            if (!strcmp(MSG_task_get_name(task_received), "ans")) {

                // answer
                XBT_VERB("Node %d: Received message : '%s - %s %s'"
                        " from %d to %d -> %s",
                        me->self.id,
                        MSG_task_get_name(task_received),
                        debug_msg[ans->type],
                        debug_msg[ans->br_type],
                        ans->sender_id,
                        ans->recipient_id,
                        ans->sent_to);
            } else {

                // request
                XBT_VERB("Node %d: Received message : '%s - %s %s'"
                        " from %d to %d -> %s",
                        me->self.id,
                        MSG_task_get_name(task_received),
                        debug_msg[req->type],
                        debug_msg[(req->type == TASK_BROADCAST ?
                            req->args.broadcast.type : TASK_NULL)],
                        req->sender_id,
                        req->recipient_id,
                        req->sent_to);
            }

            if (strcmp(MSG_task_get_name(task_received), "ans") != 0) {

                /* the received message is a request
                 * *********************************/

                XBT_VERB("Node %d: This is not the expected answer"
                        " {'%s' from %d}, it's a '%s' request from %d",
                        me->self.id,
                        debug_msg[cpy_req_data->type],
                        cpy_req_data->recipient_id,
                        debug_msg[req->type],
                        req->sender_id);

                //if (xbt_dynar_is_empty(me->tasks_queue) == 0 &&
                if (req->type == TASK_CNX_REQ) {

                    xbt_dynar_push(me->tasks_queue, &task_received);
                    task_received = NULL;
                } else {

                    // handle this received request
                    //TODO : ne pas oublier (si du broadcast est reçu ici, il faut peut-être remplacer handle_task par launch_fork_process)
                    if (req->type == TASK_BROADCAST) {
                        XBT_INFO("Node %d: [%s:%d] TASK_BROADCAST received in '%s' : '%s'",
                                me->self.id,
                                __FUNCTION__,
                                __LINE__,
                                __FUNCTION__,
                                debug_msg[req->args.broadcast.type]);
                        //xbt_assert(1 == 0);
                    }
                    handle_task(me, &task_received);
                }
                ans = NULL;
                req = NULL;

                /* NOTE: ne pas détruire les data ici.
                   Si la tâche traitée envoie une réponse, c'est le
                   destinataire qui doit détruire ces données */

                // look if the expected answer has been received meanwhile
                XBT_VERB("Node %d: [%s:%d] back to send_sync(). Answer received"
                        " meanwhile? - dynar length = %lu",
                        me->self.id,
                        __FUNCTION__,
                        __LINE__,
                        xbt_dynar_length(proc_data->sync_answers));

                // if it exists, the expected answer is at the top of dynar
                elem_ptr = xbt_dynar_get_ptr(
                        proc_data->sync_answers,
                        xbt_dynar_length(proc_data->sync_answers) - 1);

                XBT_DEBUG("Node %d: top dynar : {type: '%s - %s' - recipient: %d"
                        " - data: %p}",
                        me->self.id,
                        debug_msg[(*elem_ptr)->type],
                        debug_msg[(*elem_ptr)->br_type],
                        (*elem_ptr)->recp.id,
                        (*elem_ptr)->answer_data);

                if ((*elem_ptr)->answer_data != NULL) {

                    // the expected answer has been received
                    *answer_data = (*elem_ptr)->answer_data;

                    // pop record from dynar since it's now useless
                    xbt_dynar_pop(proc_data->sync_answers, &ans_elem);
                    xbt_free(ans_elem);     //TODO: s'assurer que ça ne libère pas answer_data

                    XBT_VERB("Node %d: Yes. dynar length = %lu",
                            me->self.id,
                            xbt_dynar_length(proc_data->sync_answers));

                    // run delayed tasks
                    //call_run_delayed_tasks(me, req_data->args.cnx_req.new_node_id);
                    //run_delayed_tasks(me);
                    break;
                } else {

                    // the answer has not been received
                    XBT_VERB("Node %d: No. dynar length = %lu",
                            me->self.id,
                            xbt_dynar_length(proc_data->sync_answers));
                }
            } else {

                /* the received task is an answer
                 * ******************************/

                // look for matching record in dynar
                dynar_idx = expected_answers_search(me,
                        proc_data->sync_answers,
                        ans->type,
                        ans->br_type,
                        ans->sender_id,
                        ans->new_node_id);

                XBT_DEBUG("Node %d in send_msg_sync(): matching record%sfound"
                        " in sync answers dynar: idx = %d",
                        me->self.id,
                        (dynar_idx == -1 ? " not " : " "),
                        dynar_idx);

                if (dynar_idx == (int)xbt_dynar_length(proc_data->sync_answers) - 1) {

                    // this is the expected answer
                    state = get_state(me);
                    XBT_DEBUG("Node %d: This is the expected answer -"
                            " active = '%c'",
                            me->self.id,
                            state.active);

                    // pop the answer from dynar sync_answers
                    xbt_dynar_pop(proc_data->sync_answers, &ans_elem);

                    // free memory
                    if (ans_elem->answer_data != NULL) {    // NOTE: answer_data devrait toujours être NULL ici

                        xbt_free(ans_elem->answer_data);
                        ans_elem->answer_data = NULL;
                    }
                    xbt_free(ans_elem);
                    task_free(&task_received);      // NOTE: ne pas libérer les data ici

                    XBT_DEBUG("Node %d: answer received. dynar has been poped:"
                            " length = %lu",
                            me->self.id,
                            xbt_dynar_length(proc_data->sync_answers));
                    break;

                } else {

                    /* this is not the expected answer.
                       Record its data into dynar sync_answers ? */
                    if (dynar_idx > -1) {

                        rec_sync_answer(me, dynar_idx, ans);

                        XBT_VERB("Node %d: This not expected sync answer"
                                " has been recorded into sync_answers dynar ->"
                                " req_type = %s-%s - sent to %d | ans_type = %s-%s"
                                " - received from %d",
                                me->self.id,
                                debug_msg[cpy_req_data->type],
                                debug_msg[(cpy_req_data->type == TASK_BROADCAST ?
                                    cpy_req_data->args.broadcast.type : TASK_NULL)],
                                cpy_req_data->recipient_id,
                                debug_msg[ans->type],
                                debug_msg[ans->br_type],
                                ans->sender_id);

                    } else {

                        // No. Is it an async expected answer, then ?
                        dynar_idx = expected_answers_search(me,
                                proc_data->async_answers,
                                ans->type,
                                ans->br_type,
                                ans->sender_id,
                                ans->new_node_id);

                        if (dynar_idx != -1) {

                            /* this answer is one of the expected acknowledgments:
                               record it in the dynar */
                            rec_async_answer(me, dynar_idx, ans);

                            XBT_VERB("Node %d: async answer '%s - %s' received from %d"
                                    " recorded in async_answers dynar : idx = %d",
                                    me->self.id,
                                    debug_msg[ans->type],
                                    debug_msg[ans->br_type],
                                    ans->sender_id,
                                    dynar_idx);

                        }
                    } // task_received is not a sync expected answer

                    // get prepared for a new loop
                    data_ans_free(me, &ans);
                    task_free(&task_received);

                }     // task_received is not the expected answer
            }         // task_received is an answer
        }             // reception success

        xbt_assert(ans == NULL && task_received == NULL,
                "Node %d in send_msg_sync(): ans and task_received should"
                " be NULL here ! ans = %p - task_received = %p",
                me->self.id,
                ans,
                task_received);

    } // reception loop

    // don't wait too long for a GET_REP answer
    //if (MSG_get_clock() - timeout >= MAX_WAIT_GET_REP &&
    if (res == MSG_TIMEOUT &&
            cpy_req_data->type == TASK_GET_REP) {

        /*
           xbt_assert(res != MSG_TIMEOUT,
           "Node %d: TIMEOUT sur recv GET_REP - res = %d",
           me->self.id,
           res);

           res = MSG_TIMEOUT;
           */

        XBT_WARN("Node %d: sync timeout",
                me->self.id);

        if (xbt_dynar_is_empty(proc_data->sync_answers) == 0) {

            // if waiting for GET_REP answer is canceled, then pop sync_answers
            elem_ptr = xbt_dynar_get_ptr(proc_data->sync_answers,
                    xbt_dynar_length(proc_data->sync_answers) - 1);
            if ((*elem_ptr)->type == TASK_GET_REP) {

                xbt_dynar_pop(proc_data->sync_answers, &ans_elem);

                if (ans_elem->answer_data != NULL) {

                    xbt_free(ans_elem->answer_data);
                    ans_elem->answer_data = NULL;
                }
                xbt_free(ans_elem);

                XBT_DEBUG("Node %d: TASK_GET_REP has been poped from dynar",
                        me->self.id);
            }
        }
        *answer_data = NULL;
    }

    if (res != MSG_TIMEOUT) {

        XBT_DEBUG("Node %d in send_msg_sync(): answer_data = %p",
                me->self.id,
                *answer_data);

        xbt_assert(*answer_data != NULL,
                "Node %d in send_msg_sync(): answer_data shouldn't be NULL : %p",
                me->self.id,
                *answer_data);
    }

    // TODO : regarder pourquoi il ne faut pas faire ces libérations
    // ( probablement parce que le receiver le fait )
    /*
    xbt_free(task_sent);

    xbt_free(req_data);
    req_data = NULL;
    */

    xbt_free(cpy_req_data);
    cpy_req_data = NULL;

    XBT_OUT();
    return res;
}

/**
 * \brief Asynchronously sends a request task to another node
 * \param me the current node
 * \param type request type
 * \param recipient_id id of the recipient
 * \param args args needed by this type of task
 * \return Communication error code
 */
static msg_error_t send_msg_async(node_t me,
        e_task_type_t type,
        int recipient_id,
        u_req_args_t args) {

    XBT_IN();

    // memorize new node id
    int new_node_id = args.get_rep.new_node_id;

    e_task_type_t mem_type = type;
    e_task_type_t mem_br_type = 0;
    if (mem_type == TASK_BROADCAST) { mem_br_type = args.broadcast.type; }

    req_data_t req_data = xbt_new0(s_req_data_t, 1);

    // init request data
    req_data->type = type;
    req_data->sender_id = me->self.id;
    req_data->recipient_id = recipient_id;
    req_data->args = args;

    // create mailboxes
    set_mailbox(req_data->recipient_id, req_data->sent_to);
    set_proc_mailbox(req_data->answer_to);

    // create and send task with data
    msg_task_t task_sent = MSG_task_create("async", COMP_SIZE, COMM_SIZE, req_data);

    XBT_VERB("Node %d: [%s:%d] Sending async '%s %s' to %d - '%s'",
            req_data->sender_id,
            __FUNCTION__,
            __LINE__,
            debug_msg[req_data->type],
            debug_msg[(req_data->type == TASK_BROADCAST ? req_data->args.broadcast.type : TASK_NULL)],
            req_data->recipient_id,
            req_data->sent_to);

    float max_wait = MSG_get_clock() + COMM_TIMEOUT;
    msg_comm_t comm = NULL;
    msg_error_t res = MSG_OK;
    int max_loops = 10;
    int loop_cpt = 0;

    // send request
    do {

        // async send
        comm = MSG_task_isend(task_sent, req_data->sent_to);

        /* max_wait has to be used in case of receiver process is down.
         * If host is shut down, MSG_TRANSFER_FAILURE is raised but nothing happens if receiver process is down.
         * TODO : à vérifier
         * TODO : comment ajuster COMM_TIMEOUT en cas de mauvais réseau ou de grand DST ?
         * TODO : tester des cas de TIMEOUT pour voir s'ils sont bien pris en compte
         */
        while (!MSG_comm_test(comm) && MSG_get_clock() <= max_wait) {

            MSG_process_sleep(0.9);
        }
        if (MSG_get_clock() > max_wait || comm == NULL || MSG_comm_get_status(comm) == MSG_TRANSFER_FAILURE) {

            res = MSG_TRANSFER_FAILURE;
            xbt_assert(1 == 0, "Node %d: [%s:%d] TRANSFER FAILURE - max_wait = %f - clock = %f - comm = %p",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    max_wait,
                    MSG_get_clock(),
                    comm);
        } else {

            res = MSG_comm_get_status(comm);
        }

        // only loop_cpt attemps are allowed
        if (res == MSG_TIMEOUT) loop_cpt++;

    } while (res == MSG_TIMEOUT && loop_cpt < max_loops);    //TODO : task_sent est-elle à refaire après une tentative ?


    xbt_assert(loop_cpt < max_loops,
            "Node %d: [%s:%d] too many sending TIMEOUT in send_msg_async() to %d - '%s' - max_wait = %f - loop_cpt = %d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            recipient_id,
            req_data->sent_to,
            max_wait,
            loop_cpt);

    if (res != MSG_OK) {

        XBT_WARN("Node %d: [%s:%d] Sending error '%s' to %d - '%s' - max_wait = %f",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                debug_res_msg[res],
                recipient_id,
                req_data->sent_to,
                max_wait);
    }

    MSG_comm_destroy(comm);
    comm = NULL;

    // count messages
    xbt_assert(mem_type < TYPE_NBR , "send_msg_async : STOP !! - type = %d", mem_type);
    xbt_assert(mem_br_type < TYPE_NBR , "send_msg_async : STOP !! - br_type = %d", mem_br_type);

    // new_node_id may be -1 when TASK_REMOVE_STATE, for example
    if (new_node_id < 0) {

        new_node_id = me->self.id;
    }
    nb_messages[new_node_id][mem_type]++;
    if (mem_type == TASK_BROADCAST) {

        //nb_br_messages[req_data->args.get_rep.new_node_id][mem_br_type]++;
        nb_br_messages[new_node_id][mem_br_type]++;
    }

    XBT_OUT();
    return MSG_OK;
}

/**
 * \brief Sends an answer back to a requesting node
 * \param me the current node
 * \param new_node_id new involved coming node
 * \param type type of sent task (must match the type of request that is answered)
 * \param recipient_id answer's recipient id
 * \param recipient_mailbox mailbox name to answer to
 * \param u_answer_data data answered back
 */
static msg_error_t send_ans_sync(node_t me,
        int new_node_id,
        e_task_type_t type,
        int recipient_id,
        char* recipient_mailbox,
        u_ans_data_t u_ans_data) {

    XBT_IN();

    e_task_type_t br_type = (type == TASK_BROADCAST ? u_ans_data.handle.br_type : TASK_NULL);
    ans_data_t ans_data = xbt_new0(s_ans_data_t, 1);

    // init answer data
    ans_data->new_node_id = new_node_id;
    ans_data->type = type;
    ans_data->br_type = br_type;
    ans_data->sender_id = me->self.id;
    ans_data->recipient_id = recipient_id;
    ans_data->answer = u_ans_data;

    // create mailbox
    snprintf(ans_data->sent_to, MAILBOX_NAME_SIZE, "%s", recipient_mailbox);

    // create task with answer data
    msg_task_t task_sent = MSG_task_create("ans", COMP_SIZE, COMM_SIZE, ans_data);

    XBT_VERB("Node %d: [%s:%d] {%d} Answering '%s - %s' to %d - '%s'",
            ans_data->sender_id,
            __FUNCTION__,
            __LINE__,
            new_node_id,
            debug_msg[ans_data->type],
            debug_msg[ans_data->br_type],
            ans_data->recipient_id,
            ans_data->sent_to);

    float max_wait = MSG_get_clock() + COMM_TIMEOUT;
    msg_comm_t comm = NULL;
    msg_error_t res = MSG_OK;
    int max_loops = 10;
    int loop_cpt = 0;
    int nb_proc = 0;
    xbt_swag_t proc_set = NULL;
    msg_process_t elem = NULL;

    // send answer
    do {

        // async send
        comm = MSG_task_isend(task_sent, ans_data->sent_to);

        /* max_wait has to be used in case of receiver process is down.
         * If host is shut down, MSG_TRANSFER_FAILURE is raised but nothing happens if receiver process is down.
         * // TODO : comment ajuster COMM_TIMEOUT en cas de mauvais réseau ou de grand DST ?
         */
        while (!MSG_comm_test(comm) && MSG_get_clock() <= max_wait) {

            proc_set = MSG_host_get_process_list(MSG_host_self());
            nb_proc = xbt_swag_size(proc_set);

            xbt_swag_foreach(elem, proc_set) {
                XBT_DEBUG("[%s:%d] swag elem = '%s'", __FUNCTION__, __LINE__,  MSG_process_get_name(elem));
            }

            XBT_DEBUG("SLEEP ...");
            MSG_process_sleep(1.0);
        }

        if (MSG_get_clock() > max_wait || comm == NULL) {

            res = MSG_TRANSFER_FAILURE;
            xbt_assert(1 == 0, "Node %d: [%s:%d] TRANSFER FAILURE - max_wait = %f - clock = %f - comm = %p",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    max_wait,
                    MSG_get_clock(),
                    comm);
        } else {

            res = MSG_comm_get_status(comm);
            XBT_DEBUG("RES = %s - loop_cpt = %d", debug_res_msg[res], loop_cpt);
        }

        // only loop_cpt attemps are allowed
        if (res == MSG_TIMEOUT) loop_cpt++;

    } while (res == MSG_TIMEOUT && loop_cpt < max_loops);

    xbt_assert(loop_cpt < max_loops,
            "Node %d: [: %d] too many sending TIMEOUT in send_ans_sync() to %d - '%s' - max_wait = %f - loop_cpt = %d",
            me->self.id,
            __LINE__,
            recipient_id,
            recipient_mailbox,
            max_wait,
            loop_cpt);

    xbt_assert(res == MSG_OK || res == MSG_TRANSFER_FAILURE, "Node %d: [: %d] sending failure '%s' in send_ans_sync() to %d - '%s' - COMM_TIMEOUT = %d - max_wait = %f",
            me->self.id,
            __LINE__,
            debug_res_msg[res],
            recipient_id,
            recipient_mailbox,
            COMM_TIMEOUT,
            max_wait);

    MSG_comm_destroy(comm);
    comm = NULL;

    if (res != MSG_OK) {

        // send failure
        task_free(&task_sent);
        data_ans_free(me, &ans_data);

        XBT_ERROR("Node %d: Failed to send '%s' answer to %d - '%s'",
                me->self.id,
                debug_msg[type],
                recipient_id,
                debug_res_msg[res]);

        if (res == MSG_TIMEOUT) {

            XBT_VERB("Node %d: Sender Timeout", me->self.id);
        } else {

            nb_messages[new_node_id][type]++;
        }
    }

    /*
    xbt_assert(res == MSG_OK, "Node %d: Failed to answer to '%s' from %d",
            me->self.id,
            debug_msg[type],
            recipient_id);
    */

    nb_messages[new_node_id][type]++;
    if (type == TASK_BROADCAST) {

        nb_br_messages[new_node_id][br_type]++;
    }

    XBT_OUT();
    return res;
}

/**
 * \brief Broadcast a message to the whole structure
 * \param me the current node
 * \param args the message - and its data - to be broadcasted
 * \return A value indicating if broadcast succeeded or not
 */
static e_val_ret_t broadcast(node_t me, u_req_args_t args) {

    XBT_IN(" - Node %d: broadcast source = %d",
            me->self.id,
            args.broadcast.source_id);

    //e_task_type_t mem_type = args.broadcast.type;

    // cases where current broadcast musn't be interrupted by another one
    switch(args.broadcast.type) {

        case TASK_SET_UPDATE:
            set_update(me, args.broadcast.args->set_update.new_node_id, args.broadcast.args->set_update.new_node_prio); //TODO: ne pas poursuivre si ret = NOK
            break;

        case TASK_SET_ACTIVE:
            set_update(me, args.broadcast.args->set_active.new_node_id, -1);  //TODO: ne pas oublier
            break;

        case TASK_REMOVE_STATE:
            remove_state(me, args.broadcast.args->remove_state.new_node_id, args.broadcast.args->remove_state.active);
            break;

        /*
        case TASK_CS_REQ:
            cs_req(me,
                    args.broadcast.args->cs_req.sender_id,
                    args.broadcast.args->cs_req.new_node_id);
            break;
         */
    }

    e_val_ret_t ret = OK;
    e_val_ret_t local_ret = OK;

    /* works on a copy of brothers, in case they'd be modified by the
       broadcasted function */
    node_rep_t cpy_brothers = xbt_new0(s_node_rep_t, b);
    int i;
    for (i = 0; i < b; i++) {

        cpy_brothers[i] = me->brothers[args.broadcast.stage][i];
    }

    int brother = 0;
    int nb_bro = me->bro_index[args.broadcast.stage];

    // number of expected answers
    int ans_cpt = nb_bro;

    msg_task_t task_sent = NULL;
    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());
    req_data_t req_data = NULL;

    if (args.broadcast.stage > 0 ||
            (args.broadcast.stage == 0 && args.broadcast.lead_br == 0)) {

        for (brother = 0; brother < nb_bro; brother++) {

            XBT_VERB("Node %d: [%s:%d] brother = %d, stage = %d",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    cpy_brothers[brother].id,
                    args.broadcast.stage);

            if (cpy_brothers[brother].id == me->self.id) {

                /* local direct call (no expected answer)
                   to be handled at the end of function (see below) */
                ans_cpt--;
                make_broadcast_task(me, args, &task_sent);

            } else {

                //if (args.broadcast.first_call == 1) {

                    //compteur[args.broadcast.type]++;
                //}

                // remote call
                msg_error_t res = send_msg_async(me,
                        TASK_BROADCAST,
                        cpy_brothers[brother].id,
                        args);

                xbt_assert(res == MSG_OK, "Node %d: Broadcast error", me->self.id);

                // record recipient in expected answers dynar
                recp_rec_t elem = xbt_new0(s_recp_rec_t, 1);

                elem->type = TASK_BROADCAST;
                elem->recp = cpy_brothers[brother];
                elem->new_node_id = args.broadcast.new_node_id;
                elem->br_type = args.broadcast.type;
                elem->answer_data = NULL;

                xbt_assert(elem->recp.id > - 1,
                        "Node %d: #1# recp.id is %d !!",
                        me->self.id,
                        elem->recp.id);

                xbt_dynar_push(proc_data->async_answers, &elem);
            }
        }
    } else {
        /* stage 0 is reached and a 'leaders-only' broadcast is requested
         * only the leader has to do the work */
        if (me->self.id == cpy_brothers[0].id) {

            ans_cpt = 0;
            make_broadcast_task(me, args, &task_sent);
        } else {

            ans_cpt = 1;

            //if (args.broadcast.first_call == 1) {

                //compteur[args.broadcast.type]++;
            //}

            // remote call
            msg_error_t res = send_msg_async(me,
                    TASK_BROADCAST,
                    cpy_brothers[0].id,
                    args);

            xbt_assert(res == MSG_OK, "Node %d: Broadcast error 2",
                    me->self.id);

            // record recipient in expected answers dynar
            recp_rec_t elem = xbt_new0(s_recp_rec_t, 1);

            elem->type = TASK_BROADCAST;
            elem->recp = cpy_brothers[0];
            elem->new_node_id = args.broadcast.new_node_id;
            elem->br_type = args.broadcast.type;
            elem->answer_data = NULL;

            xbt_assert(elem->recp.id > - 1,
                    "Node %d: #1# recp.id is %d !!",
                    me->self.id,
                    elem->recp.id);

            xbt_dynar_push(proc_data->async_answers, &elem);
        }
    }

    // synchronize each stage
    if (ans_cpt > 0) {

        ret = wait_for_completion(me, ans_cpt, args.broadcast.new_node_id);
    }

    // Handle local task if OK
    if (ret != UPDATE_NOK) {
        if (!(args.broadcast.type == TASK_CLEAN_STAGE &&
                    args.broadcast.stage == 0 &&
                    me->self.id == args.broadcast.source_id)) {

            // return NOK if any of both answers is NOK
            if (task_sent != NULL) {

                //req_data = MSG_task_get_data(task_sent);
                //compteur[req_data->args.broadcast.type]--;

                local_ret = handle_task(me, &task_sent);
                if (local_ret == UPDATE_NOK || ret == UPDATE_NOK) {

                    ret = UPDATE_NOK;
                }
            }
        } else {

            /* If a clean_stage is broadcasted, the source branch mustn't locally
             * run this task since it has already been run before launching
             * broadcast */

            XBT_VERB("Node %d: DON'T run TASK_CLEAN_STAGE - source_id = %d",
                    me->self.id,
                    args.broadcast.source_id);

            if (task_sent != NULL) {

                req_data = MSG_task_get_data(task_sent);
                data_req_free(me, &req_data);
            }
            task_free(&task_sent);
        }
    } else {
        XBT_VERB("Node %d: STOP BROADCAST", me->self.id);

        if (task_sent != NULL) {

            req_data = MSG_task_get_data(task_sent);
            //compteur[req_data->args.broadcast.type]--;  //TODO : regarder si le type peut être CS_REQ ou pas
            //compteur[mem_type]--;
            data_req_free(me, &req_data);
        }
        task_free(&task_sent);
    }

    xbt_free(cpy_brothers);
    cpy_brothers = NULL;

    display_sc(me, 'V');

    XBT_OUT();
    return ret;
}

/**
 * \brief Send a 'completed task' message back (acknowledgement)
 * \param me the current node
 * \param type the type of message to send back
 *             (must match the one that's acknowledged)
 * \param recipient_id the recipient of this message
 * \param new_node_id new involved coming node
 */
static void send_completed(node_t me, e_task_type_t type, int recipient_id, char* recipient_mailbox, int new_node_id) {

    XBT_IN();

    u_ans_data_t answer;
    answer.handle.val_ret = OK;
    answer.handle.val_ret_id = me->self.id;

    send_ans_sync(me, new_node_id, type, recipient_id, recipient_mailbox, answer);

    XBT_DEBUG("Node %d: '%s Completed' message sent to %d",
            me->self.id,
            debug_msg[type],
            recipient_id);

    XBT_OUT();
}

/**
 * \brief Initialize a new node
 * \param me the node to be initialized
 */
static void init(node_t me) {

    XBT_IN();
    XBT_INFO("Node %d: init() ...", me->self.id);
    int i;

    me->height = 1;
    me->comm_received = NULL;
    me->last_check_time = 0;

    // set mailbox
    set_mailbox(me->self.id, me->self.mailbox);
    MSG_mailbox_set_async(me->self.mailbox);

    // set process data
    proc_data_t proc_data = xbt_new0(s_proc_data_t, 1);

    // proc mailbox is the same as node's one
    set_mailbox(me->self.id, proc_data->proc_mailbox);
    proc_data->node = NULL;
    proc_data->task = NULL;

    MSG_process_set_data(MSG_process_self(), proc_data);
    MSG_process_set_data_cleanup(proc_data_cleanup);

    // set current state
    me->states = xbt_dynar_new(sizeof(state_t), &xbt_free_ref);
    set_state(me, me->self.id, 'b');    // building in progress

    proc_data->async_answers = xbt_dynar_new(sizeof(recp_rec_t), &xbt_free_ref);
    me->tasks_queue = xbt_dynar_new(sizeof(msg_task_t), &xbt_free_ref);
    me->delayed_tasks = xbt_dynar_new(sizeof(msg_task_t), &xbt_free_ref);
    proc_data->sync_answers = xbt_dynar_new(sizeof(recp_rec_t), &xbt_free_ref);
    me->prio = 0;
    me->cs_req = 0;
    me->cs_req_time = 0;
    me->cs_new_id = -1;
    me->cs_new_node_prio = 0;

    // DST infos initialization
    me->dst_infos.order = 0;
    me->dst_infos.node_id = me->self.id;
    me->dst_infos.routing_table = NULL;
    me->dst_infos.attempts = 0;
    //me->dst_infos.nb_messages = 0;
    me->dst_infos.add_stage = 0;
    me->dst_infos.nbr_split_stages = 0;
    me->dst_infos.load = xbt_new0(int, me->height);
    me->dst_infos.size = NULL;
    me->dst_infos.nb_cs_req_fail = 0;
    me->dst_infos.nb_cs_req_success = 0;
    me->dst_infos.nb_set_update_fail = 0;
    me->dst_infos.nb_set_update_success = 0;
    me->dst_infos.nb_task_remove = 0;
    me->dst_infos.nb_chg_contact = 0;

    // routing table initialization
    me->bro_index = xbt_new0(int, me->height);
    me->brothers = xbt_new0(node_rep_t, me->height);

    for (i = 0; i < me->height; i++) {

        me->brothers[i] = xbt_new0(s_node_rep_t, b);
    }

    // predecessors table initilization
    me->pred_index = xbt_new0(int, me->height);
    me->preds = xbt_new0(node_rep_t, me->height);

    for (i = 0; i < me->height; i++) {

        me->preds[i] = xbt_new0(s_node_rep_t, b);
    }

    // set first elements
    add_pred(me, 0, me->self.id);
    add_brother(me, 0, me->self.id);

    display_rout_table(me, 'V');

    // set and store DST infos
    set_n_store_infos(me);

    // create fork process in charge of processing remain tasks
    call_run_tasks_queue(me, me->self.id);

    display_preds(me, 'D');

    XBT_DEBUG("Node %d: end of init() - mailbox = '%s' - proc_mailbox = '%s'",
            me->self.id,
            me->self.mailbox,
            ((proc_data_t)MSG_process_get_data(MSG_process_self()))->proc_mailbox);

    XBT_OUT();
}

/**
 * \brief This function lets a new node join the DST
 * \param me the new node
 * \param contact_id the node 'me' can contact to get into the DST
 * \return 1 if join succeeded, 0 otherwise
 */
static int join(node_t me, int contact_id) {

    XBT_IN();
    s_state_t state = get_state(me);
    XBT_VERB("Node %d: '%c'/%d - join() ...",
            me->self.id,
            state.active,
            state.new_id);

    // prepare data to exchange
    u_req_args_t u_req_args;
    u_req_args.cnx_req.new_node_id = me->self.id;
    u_req_args.cnx_req.cs_new_node_prio = me->prio;
    //u_req_args.cnx_req.try = me->dst_infos.attempts;
    u_req_args.cnx_req.try = 0;

    ans_data_t answer_data = NULL;

    // send connection request
    msg_error_t res = send_msg_sync(me,
            TASK_CNX_REQ,
            contact_id,
            u_req_args,
            &answer_data);

    XBT_DEBUG("Node %d: Back to join - val_ret = %d - contact = %d",
            me->self.id,
            answer_data->answer.cnx_req.val_ret,
            answer_data->answer.cnx_req.new_contact.id);

    if (res == MSG_OK) {

        // records number of attempts
        me->dst_infos.attempts += answer_data->answer.cnx_req.try;
    }

    if (res != MSG_OK || answer_data->answer.cnx_req.val_ret == UPDATE_NOK) {

        // join failure
        XBT_VERB("Node %d failed to join the DST", me->self.id);

        data_ans_free(me, &answer_data);
        return 0;
    }

    xbt_assert(answer_data->answer.cnx_req.val_ret != FAILED,
            "[%s:%d] STOP",
            __FUNCTION__,
            __LINE__);

    /* get task answer data: new contact and its routing table */

    state = get_state(me);
    XBT_INFO("Node %d: '%c'/%d - **** GETTING INFOS FROM ITS NEW CONTACT %d (needed %d attempts) ... ****",
            me->self.id,
            state.active,
            state.new_id,
            answer_data->answer.cnx_req.new_contact.id,
            me->dst_infos.attempts);

    // discard the current tables
    int stage;
    for (stage = 0; stage < me->height; stage++) {

        xbt_free(me->brothers[stage]);
        xbt_free(me->preds[stage]);
    }

    xbt_free(me->brothers);
    xbt_free(me->preds);

    // get the new contact and its height
    s_node_rep_t contact = answer_data->answer.cnx_req.new_contact;
    me->height = answer_data->answer.cnx_req.height;

    // get DST infos
    me->dst_infos.add_stage = answer_data->answer.cnx_req.add_stage;
    me->dst_infos.nbr_split_stages = answer_data->answer.cnx_req.nbr_split_stages;

    state = get_state(me);
    XBT_VERB("Node %d: [%s:%d] '%c'/%d - contact %d has %d stages -"
            " add_stage = %d - nbr_split_stages = %d - its state was '%c'/%d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            state.active,
            state.new_id,
            contact.id,
            me->height,
            me->dst_infos.add_stage,
            me->dst_infos.nbr_split_stages,
            answer_data->answer.cnx_req.contact_state.active,
            answer_data->answer.cnx_req.contact_state.new_id);

    // get contact's tables content
    me->brothers = xbt_new0(node_rep_t, me->height);
    me->preds = xbt_new0(node_rep_t, me->height);

    int brother;
    for (stage = 0; stage < me->height; stage++) {

        me->brothers[stage] = xbt_new0(s_node_rep_t, b);
        me->preds[stage] = xbt_new0(s_node_rep_t, b);

        for (brother = 0; brother < b; brother++) {

            me->brothers[stage][brother] =
                answer_data->answer.cnx_req.brothers[stage][brother];

                XBT_DEBUG("[%s] cnx_req_brothers[%d][%d] = %d",
                __FUNCTION__,
                stage,
                brother,
                answer_data->answer.cnx_req.brothers[stage][brother].id);
        }

        xbt_free(answer_data->answer.cnx_req.brothers[stage]);
    }
    xbt_free(answer_data->answer.cnx_req.brothers);

    // set the stage 0 predecessors
    for (brother = 0; brother < b; brother++ ) {

        me->preds[0][brother] = me->brothers[0][brother];
    }

    // get other data
    xbt_free(me->bro_index);
    xbt_free(me->pred_index);
    xbt_free(me->dst_infos.load);
    me->bro_index = xbt_new0(int, me->height);
    me->pred_index = xbt_new0(int, me->height);
    me->dst_infos.load = xbt_new0(int, me->height);

    for (stage = 0; stage < me->height; stage++) {

        me->bro_index[stage] = answer_data->answer.cnx_req.bro_index[stage];
    }
    me->pred_index[0] = me->bro_index[0];
    xbt_free(answer_data->answer.cnx_req.bro_index);

    // check if received table has been modified since its sending
    node_rep_t diff_nodes = compare_tables(me, &(answer_data->answer.cnx_req.cur_table), &(answer_data->answer.cnx_req.cur_index));
    if (diff_nodes != NULL) {
        XBT_VERB("Node %d: [%s:%d] DIFF NODES FOUND !!",
                me->self.id,
                __FUNCTION__,
                __LINE__);

        xbt_free(diff_nodes);
    }

    // set DST infos
    me->dst_infos.load[0] = me->pred_index[0];

    // answer_data is not needed anymore
    data_ans_free(me, &answer_data);

    // set and store DST infos
    set_n_store_infos(me);

    display_preds(me, 'D');
    XBT_INFO("Node %d: My new contact is %d", me->self.id, contact.id);

    display_rout_table(me, 'I');
    state = get_state(me);
    XBT_INFO("Node %d: '%c'/%d - **** DONE (before load_balance) ***",
            me->self.id,
            state.active,
            state.new_id);

    // find other representatives for load balancing
    load_balance(me, contact.id);

    // remove 'p' state from contact
    //u_req_args.remove_state.new_node_id = me->self.id;
    u_req_args.remove_state.new_node_id = -1;
    u_req_args.remove_state.active = 'p';

    XBT_VERB("Node %d: [%s:%d] Tell %d to remove 'p' state",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            contact.id);

    res = send_msg_async(me,
            TASK_REMOVE_STATE,
            contact.id,
            u_req_args);

    display_rout_table(me, 'I');
    state = get_state(me);
    XBT_INFO("Node %d: '%c'/%d - **** DONE (after load_balance) ***",
            me->self.id,
            state.active,
            state.new_id);

    //me->dst_infos.nb_messages = tot_msg_number(me->self.id);

    // set and store DST infos
    set_n_store_infos(me);

    display_preds(me, 'D');
    XBT_OUT();
    return 1;           // join success
}

/**
 * \brief Get a node representative for a given stage.
 *        The less loaded one will be elected.
 * \param me the current node
 * \param stage the stage for which a representative is requested
 * \param new_node_id the node that's being inserted
 * \return Answer data containing the new representative
 */
static u_ans_data_t get_rep(node_t me, int stage, int new_node_id) {

    XBT_IN();

    u_ans_data_t answer;
    answer.get_rep.new_rep = me->self;

    // get current state
    s_state_t state = get_state(me);

    /* don't run get_rep if current node is being updated
       don't allow cascading calls (get_rep interrupted by get_rep) */
    //if (state.active == 'u' || state.active == 'g') {
    if (state.active == 'u' || state_search(me, 'g', -1) > -1) {

        XBT_OUT();
        return answer;
    }

    // push state 'g' if current node is active
    if (state.active == 'a' || state.active == 'p') {       //TODO : à vérifier

        set_state(me, new_node_id, 'g');
    }

    // set self state to 'p' right now in case me would be chosen
    //set_state(me, new_node_id, 'p');

    XBT_VERB("Node %d: [%s:%d] '%c'/%d - new_node = %d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            state.active,
            state.new_id,
            new_node_id);

    display_states(me, 'V');

    int load = me->pred_index[stage];
    int load_f = 0;

    ans_data_t rcv_ans_data = NULL;

    u_req_args_t u_req_args;
    u_req_args.nb_pred.stage = stage;
    u_req_args.nb_pred.new_node_id = new_node_id;

    int f = 0;
    //for (f = 0; f < me->bro_index[0]; f+=2) {       // take only brothers that will stay for sure (in case of SPLIT)

    //    if (me->brothers[0][f].id == me->self.id) {

    //        load_f = me->pred_index[stage];
    //    } else {
    //        msg_error_t res = send_msg_sync(me,
    //                TASK_NB_PRED,
    //                me->brothers[0][f].id,
    //                u_req_args,
    //                &rcv_ans_data);

    //        // TODO : s'assurer qu'on est toujours à l'état 'g' à ce stade

    //        if (res != MSG_OK) {

    //            // nb pred failure
    //            XBT_WARN("Node %d: failed to get the number of predecessors"
    //                    " for stage %d from node %d",
    //                    me->self.id,
    //                    stage, me->brothers[0][f].id);

    //            // defaults to b in this case   //TODO: vérifier si c'est une bonne idée
    //            rcv_ans_data->answer.nb_pred.load = b;  //TODO : Attention, pointeur pas initialisé ?
    //        }
    //        load_f = rcv_ans_data->answer.nb_pred.load;
    //        /* load_f will be negative if me->brothers[0][f] is in 'b' state */

    //        XBT_DEBUG("Node %d: load of brothers[0][%d](%d) = %d",
    //                me->self.id,
    //                f,
    //                me->brothers[0][f].id,
    //                load_f);
    //    }

    //    if (load_f >= 0 && load_f < load) {
    //        load = load_f;
    //        answer.get_rep.new_rep = me->brothers[0][f];
    //    }

    //    data_ans_free(me, &rcv_ans_data);
    //}

    // choose rep randomly
    srand(time(NULL));
    do {
        f = rand() % (me->bro_index[0]);
    } while (f % 2 != 0);                       // take only brothers that will stay for sure (in case of SPLIT)
    xbt_assert(f < me->bro_index[0], "STOP !!");
    answer.get_rep.new_rep = me->brothers[0][f];

    // remove 'g' state
    int idx = state_search(me, 'g', new_node_id);
    if (idx > -1) {

        XBT_DEBUG("Node %d: [%s:%d] idx = %d",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                idx);

        xbt_dynar_remove_at(me->states, idx, NULL);

        XBT_VERB("Node %d: [%s:%d] 'g' state removed for new_id %d",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                new_node_id);
    }

    /*
    // remove 'p' state
    if (answer.get_rep.new_rep.id != me->self.id) {

        idx = state_search(me, 'p', new_node_id);
        if (idx > -1) {

            XBT_DEBUG("Node %d: [%s:%d] idx = %d",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    idx);

            xbt_dynar_remove_at(me->states, idx, NULL);

            XBT_VERB("Node %d: [%s:%d] 'p' state removed for new_id %d",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    new_node_id);
        }
    }
    */

    display_states(me, 'V');

    xbt_assert(xbt_dynar_length(me->states) > 0,
            "Node %d: (in %s:%d) dynar states is empty !",
            me->self.id,
            __FUNCTION__,
            __LINE__);

    XBT_OUT();
    return answer;
}

/**
 * \brief This function (run by new node's contact) makes room for a new joining node
 *        if necessary and inserts it.
 * \param me the current node
 * \param new_node_id the involved new coming node
 * \param cs_new_node_prio new node's priority
 * \param try number of joining attempts
 * \return Answer data (to the new node) containing the contact's routing table
 */
static u_ans_data_t connection_request(node_t me, int new_node_id, int cs_new_node_prio, int try) {

    XBT_IN();

    s_state_t state = get_state(me);
    XBT_VERB("Node %d: [%s:%d] '%c'/%d - Start connection request for new_node %d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            state.active,
            state.new_id,
            new_node_id);

    u_ans_data_t answer;

    // value included in the final answer
    e_val_ret_t val_ret = OK;

    display_rout_table(me, 'V');

    // to set DST infos
    answer.cnx_req.add_stage = 0;
    node_rep_t cpy_brothers = NULL;

    if (me->self.id != me->brothers[0][0].id) {

        // me isn't the leader: transfer the request to the leader

        //set_update(me, new_node_id);

        state = get_state(me);  //TODO : inutile ?
        XBT_INFO("Node %d: [%s:%d] '%c'/%d -Transfering 'Connection request' to the leader %d - try = %d",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                state.active,
                state.new_id,
                me->brothers[0][0].id,
                try);

        u_req_args_t args;
        args.cnx_req.new_node_id = new_node_id;
        args.cnx_req.cs_new_node_prio = cs_new_node_prio;
        args.cnx_req.try = try;

        ans_data_t answer_data = NULL;
        msg_error_t res;

        res = send_msg_sync(me,
                TASK_CNX_REQ,
                me->brothers[0][0].id,
                args,
                &answer_data);

        // transfer failure
        xbt_assert(res == MSG_OK, "Node %d: Transfer to leader node %d failed",
                me->self.id,
                me->brothers[0][0].id);

        // transfer success
        answer = answer_data->answer;
        val_ret = answer.cnx_req.val_ret;

        data_ans_free(me, &answer_data);

        // can be active now
        set_active(me, new_node_id);

    } else {

        // me is the leader
        int n = 0;

        XBT_INFO("Node %d: [%s:%d] '%c'/%d - I am the leader",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                state.active,
                state.new_id);

        XBT_DEBUG("Node %d: [%s:%c] start try = %d",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                try);

        display_sc(me, 'D');

        /*** Ask for permission to get into the Critical Section ***/
        val_ret = cs_req(me, me->self.id, new_node_id, cs_new_node_prio);

        XBT_DEBUG("Node %d: back to [%s:%d]", me->self.id, __FUNCTION__, __LINE__);

        /*
           if ((me->cs_req == 1 && me->cs_new_id != new_node_id) ||
           (state.active == 'u' && state.new_id != new_node_id) ||
           (state.active != 'u' && state.active != 'a'))
        */

        if (val_ret == UPDATE_NOK) {

            // if current node isn't available, reject request
            XBT_VERB("Node %d: [%s:%d] '%c'/%d - not available",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    state.active,
                    state.new_id);

            answer.cnx_req.new_contact.id = -1;
        } else {

            XBT_VERB("Node %d: [%s:%d] '%c'/%d - available",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    state.active,
                    state.new_id);

            u_req_args_t args;
            msg_task_t task_sent = NULL;

            // how many stages have to be splitted ?
            while ((n < me->height) && (me->bro_index[n] == b)) {

                n++;
            }

            // set DST infos
            answer.cnx_req.nbr_split_stages = n;

            // splits will be required
            if (n > 0) {

                state = get_state(me);
                XBT_INFO("Node %d: [%s:%d] '%c'/%d - **** MAKING ROOM FOR NODE %d ... ****",
                        me->self.id,
                        __FUNCTION__,
                        __LINE__,
                        state.active,
                        state.new_id,
                        new_node_id);

                // set DST infos
                if (n == me->height) {
                    answer.cnx_req.add_stage = 1;
                }

                // broadcast a cs_req to all concerned leaders
                XBT_VERB("Node %d: [%s:%d] broadcast a cs_req to all concerned leaders",
                        me->self.id,
                        __FUNCTION__,
                        __LINE__);

                args.broadcast.type = TASK_CS_REQ;

                /* broadcast starts one level higher than highest splitted stage
                   because of connect_splitted_groups */
                if (n == me->height) {

                    args.broadcast.stage = n - 1;
                } else {

                    args.broadcast.stage = n;
                }
                args.broadcast.first_call = 1;
                args.broadcast.source_id = me->self.id;
                args.broadcast.new_node_id = new_node_id;
                args.broadcast.lead_br = 1;     // broadcast only to leaders

                args.broadcast.args = xbt_new0(u_req_args_t, 1);
                args.broadcast.args->cs_req.new_node_id = new_node_id;
                args.broadcast.args->cs_req.sender_id = me->self.id;
                args.broadcast.args->cs_req.cs_new_node_prio = cs_new_node_prio;

                make_broadcast_task(me, args, &task_sent);

                val_ret = handle_task(me, &task_sent);

                // counting
                if (val_ret == UPDATE_NOK) {
                    me->dst_infos.nb_cs_req_fail++;
                } else {
                    me->dst_infos.nb_cs_req_success++;
                }

                xbt_free(args.broadcast.args);
                args.broadcast.args = NULL;

                if (val_ret != UPDATE_NOK) {

                    // set all concerned nodes as 'u' if cs_req succeeded
                    XBT_VERB("Node %d: set all concerned leaders as 'u'",
                            me->self.id);

                    args.broadcast.type = TASK_SET_UPDATE;

                    /* broadcast starts one level higher than highest splitted stage
                       because of connect_splitted_groups */
                    if (n == me->height) {

                        args.broadcast.stage = n - 1;
                    } else {

                        args.broadcast.stage = n;
                    }
                    args.broadcast.first_call = 1;
                    args.broadcast.source_id = me->self.id;
                    args.broadcast.new_node_id = new_node_id;
                    args.broadcast.lead_br = 1;     // broadcast only to leaders

                    args.broadcast.args = xbt_new0(u_req_args_t, 1);
                    args.broadcast.args->set_update.new_node_id = new_node_id;
                    args.broadcast.args->set_update.new_node_prio = cs_new_node_prio;
                    make_broadcast_task(me, args, &task_sent);

                    val_ret = handle_task(me, &task_sent);

                    // counting
                    if (val_ret == UPDATE_NOK) {
                        me->dst_infos.nb_set_update_fail++;
                    } else {
                        me->dst_infos.nb_set_update_success++;
                    }

                    xbt_free(args.broadcast.args);
                    args.broadcast.args = NULL;

                    if (val_ret == UPDATE_NOK) {

                        /* Set_Update broadcast failed (probably because a cs_req has been reset meanwhile)
                           'u' leaders have to be reset to their former state */

                        XBT_VERB("Node %d: Set_Update failed : reset all concerned leaders",
                                me->self.id);

                        args.broadcast.type = TASK_REMOVE_STATE;

                        /* broadcast starts one level higher than highest splitted stage
                           because of connect_splitted_groups */
                        if (n == me->height) {

                            args.broadcast.stage = n - 1;
                        } else {

                            args.broadcast.stage = n;
                        }
                        args.broadcast.first_call = 1;
                        args.broadcast.source_id = me->self.id;
                        args.broadcast.new_node_id = new_node_id;
                        args.broadcast.lead_br = 1;     // broadcast only to leaders

                        args.broadcast.args = xbt_new0(u_req_args_t, 1);
                        args.broadcast.args->remove_state.new_node_id = new_node_id;
                        args.broadcast.args->remove_state.active = 'u';
                        make_broadcast_task(me, args, &task_sent);

                        handle_task(me, &task_sent);

                        // counts
                        me->dst_infos.nb_task_remove++;

                        xbt_free(args.broadcast.args);
                        args.broadcast.args = NULL;

                        answer.cnx_req.new_contact.id = -1;
                    } else {

                        // splits stages
                        int k;
                        for (k = n; k > 0; k--) {

                            split_request(me, k, new_node_id);
                        }

                        XBT_VERB("Node %d: Back to connection request for new node %d",
                                me->self.id,
                                new_node_id);

                        // reset all concerned nodes as 'a'
                        XBT_VERB("Node %d: reset all concerned nodes as 'a'",
                                me->self.id);

                        args.broadcast.type = TASK_SET_ACTIVE;

                        /* broadcast starts one level higher than highest splitted stage
                           because of connect_splitted_groups */
                        if (n == me->height) {

                            args.broadcast.stage = n - 1;
                        } else {

                            args.broadcast.stage = n;
                        }
                        args.broadcast.first_call = 1;
                        args.broadcast.source_id = me->self.id;
                        args.broadcast.new_node_id = new_node_id;
                        args.broadcast.lead_br = 0;             // broadcast to everyone (leaders only don't work)

                        args.broadcast.args = xbt_new0(u_req_args_t, 1);
                        args.broadcast.args->set_active.new_node_id = new_node_id;
                        make_broadcast_task(me, args, &task_sent);

                        handle_task(me, &task_sent);

                        xbt_free(args.broadcast.args);
                        args.broadcast.args = NULL;

                        state = get_state(me);
                        XBT_INFO("Node %d: [%s:%d] '%c'/%d -  **** ROOM MADE FOR NODE %d ****",
                                me->self.id,
                                __FUNCTION__,
                                __LINE__,
                                state.active,
                                state.new_id,
                                new_node_id);
                    }
                } else {

                    // cs_req returned NOK

                    state = get_state(me);
                    //TODO : comptage des essais pas bon. A modifier ?
                    XBT_INFO("Node %d: '%c'/%d - **** FAILED TO MAKE ROOM FOR NODE %d (try = %d) ****",
                            me->self.id,
                            state.active,
                            state.new_id,
                            new_node_id,
                            try);

                    display_sc(me, 'V');

                    answer.cnx_req.new_contact.id = -1;
                }
            }
        }

        // insert new node if request has not been rejected
        if (val_ret != UPDATE_NOK) {

            set_update(me, new_node_id, cs_new_node_prio);    // stay at 'u' until the end

            state = get_state(me);
            XBT_INFO("Node %d: [%s:%d] '%c'/%d - **** INSERTING NODE %d ... ****",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    state.active,
                    state.new_id,
                    new_node_id);

            /* tells every first stage concerned node to let the new node in */

            // works on a copy of first stage brothers      //TODO : utile ? on a fait un set_update avant qui devrait protéger la table ...
            int cpy_bro_index = me->bro_index[0];
            cpy_brothers = xbt_new0(s_node_rep_t, b);
            int i,j;
            for (i = 0; i < b; i++) {

                cpy_brothers[i] = me->brothers[0][i];
            }

            int cpt = 0;
            recp_rec_t elem = NULL;
            u_req_args_t args;
            msg_error_t res = MSG_OK;
            proc_data_t proc_data = MSG_process_get_data(MSG_process_self());

            for (i = 0; i < cpy_bro_index; i++) {

                if (cpy_brothers[i].id != me->self.id) {

                    /* sends a 'New Brother Receive' task to every brother other
                       than 'me' */
                    args.new_brother_rcv.new_node_id = new_node_id;

                    res = send_msg_async(me,
                            TASK_NEW_BROTHER_RCV,
                            cpy_brothers[i].id,
                            args);

                    xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                            me->self.id,
                            debug_msg[TASK_NEW_BROTHER_RCV],
                            cpy_brothers[i].id);

                    // record recipient
                    elem = xbt_new0(s_recp_rec_t, 1);

                    elem->type = TASK_NEW_BROTHER_RCV;
                    elem->br_type = TASK_NULL;
                    elem->recp = cpy_brothers[i];
                    elem->new_node_id = new_node_id;
                    elem->answer_data = NULL;
                    xbt_dynar_push(proc_data->async_answers, &elem);

                    xbt_assert(elem->recp.id > - 1,
                            "Node %d: #2# recp.id is %d !!",
                            me->self.id,
                            elem->recp.id);

                    cpt++;
                } else {

                    // local call for 'me'
                    new_brother_received(me, new_node_id);
                }
            }

            // synchro (5)
            if (cpt > 0) {

                wait_for_completion(me, cpt, new_node_id);
            }

            // answer construction

            /* send a copy of routing table to new node since it may be modified
               meanwhile ( see also load_balance() ) */
            int *cpy_bro_index2 = NULL;
            s_node_rep_t **cpy_brothers2 = NULL;
            make_copy_brothers(me, &cpy_brothers2, &cpy_bro_index2);

            answer.cnx_req.cur_table = me->brothers;
            answer.cnx_req.cur_index = me->bro_index;
            answer.cnx_req.new_contact = me->self;
            answer.cnx_req.brothers = cpy_brothers2;
            answer.cnx_req.bro_index = cpy_bro_index2;
            answer.cnx_req.height = me->height;         //TODO : modifier make_copy_brothers pour récupérer aussi la hauteur ?
            state = get_state(me);
            answer.cnx_req.contact_state = state;

            // now, me can be active
            set_active(me, new_node_id);

            // prevent me from running connect_splitted_groups
            // (will be reset in join(), after load_balance())
            set_state(me, -1, 'p');

            state = get_state(me);
            XBT_INFO("Node %d: [%s:%d] '%c'/%d - **** NODE %d INSERTED ****",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    state.active,
                    state.new_id,
                    new_node_id);

            // all preds will change during load_balance
            u_req_args_t u_req_args;
            u_req_args.set_state.new_node_id = new_node_id;
            u_req_args.set_state.state = 'p';

            for (i = 1; i < me->height; i++) {    // not needed for stage 0
                for (j = 0; j < cpy_bro_index2[i]; j++) {

                    /* Current node doesn't have to add new node as a pred. It'll
                       be replaced by new_node during load_balancing */
                    if (cpy_brothers2[i][j].id != me->self.id) {

                        res = send_msg_async(me,
                                TASK_SET_STATE,
                                cpy_brothers2[i][j].id,
                                u_req_args);
                    }
                }
            }
            //TODO : faut-il un wait_for_completion ici ? si oui, il faut reprendre SET_STATE qui n'envoie pas de ack
            me->cs_req = 0;
            me->cs_req_time = MSG_get_clock();
        }
        answer.cnx_req.val_ret = val_ret;
        answer.cnx_req.try = try;
    }

    state = get_state(me);
    XBT_INFO("Node %d: [%s:%d] '%c'/%d - end - val_ret = '%s' - nb_attempts = %d, contact = %d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            state.active,
            state.new_id,
            debug_ret_msg[val_ret],
            answer.cnx_req.try,
            answer.cnx_req.new_contact.id);

    XBT_DEBUG("Node %d: [%s:%d] answer.cnx_req.add_stage = %d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            answer.cnx_req.add_stage);

    xbt_free(cpy_brothers);
    XBT_OUT();
    return answer;
}

    /**
     * \brief Handle the arrival of a new node in first stage node
     * \param me the current node
     * \param new_node the new coming node
     */
    static void new_brother_received(node_t me, int new_node_id) {

        XBT_IN();

        add_brother(me, 0, new_node_id);
        add_pred(me, 0, new_node_id);

    XBT_OUT();
}

/**
 * \brief Handle a split request
 * \param me the current node
 * \param stage_nbr number of stages that have to be splitted
 * \param new_node_id new node that's being inserted
 */
static void split_request(node_t me, int stage_nbr, int new_node_id) {

    XBT_IN();

    /* current node is being updated (normaly already at 'u' by broadcast from
       connection_request(), but can be stored and delayed) */
    set_update(me, new_node_id, -1);

    s_state_t state = get_state(me);
    XBT_VERB("Node %d: '%c'/%d - split_request() ... - new_node = %d",
            me->self.id,
            state.active,
            state.new_id,
            new_node_id);

    XBT_VERB("Node %d: In split_request - number of stages to be splitted: %d",
            me->self.id, stage_nbr);

    display_rout_table(me, 'D');

    u_req_args_t broadcast_args;
    ans_data_t answer_data = NULL;
    int stage = stage_nbr - 1;
    int chk = 0;
    int nb_loop = 0;

    if (me->brothers[0][0].id != me->self.id) {

        // me isn't the leader: transfer the split request to the leader

        /* TODO : est-ce qu'il arrive qu'on passe dans cette branche ? C'est
           déjà le leader de contact qui lance cette fonction ...  */
        XBT_VERB("Node %d: Transfering 'Split Request' to the leader %d",
                me->self.id,
                me->brothers[0][0].id);

        u_req_args_t args;
        args.split_req.stage_nbr = stage_nbr;
        args.split_req.new_node_id = new_node_id;
        answer_data = NULL;

        msg_error_t res = send_msg_sync(me,
                TASK_SPLIT_REQ,
                me->brothers[0][0].id,
                //me->brothers[stage][0].id,
                args,
                &answer_data);

        // transfer failure
        xbt_assert(res == MSG_OK, "Node %d: Transfer to leader node %d failed",
                me->self.id,
                me->brothers[0][0].id);

        data_ans_free(me, &answer_data);
    } else {

        // me is the leader
        XBT_VERB("Node %d: I am the leader", me->self.id);

        msg_task_t task_sent = NULL;
        if (stage_nbr == me->height) {

            // add a stage to every node since the root has to be splitted
            XBT_VERB("Node %d: add a stage", me->self.id);

            broadcast_args.broadcast.type = TASK_ADD_STAGE;
            broadcast_args.broadcast.stage = me->height - 1;
            broadcast_args.broadcast.first_call = 1;
            broadcast_args.broadcast.source_id = me->self.id;
            broadcast_args.broadcast.new_node_id = new_node_id;
            broadcast_args.broadcast.lead_br = 0;

            // there are no arguments for add_stage
            broadcast_args.broadcast.args = NULL;

            make_broadcast_task(me, broadcast_args, &task_sent);
            handle_task(me, &task_sent);
        }

        XBT_VERB("Node %d: In split_request - stage = %d",
                me->self.id,
                stage);

        // checks node's consistency before broadcasting TASK_SPLIT
        chk = 0;
        nb_loop = 10;
        do {
            chk = check(me);
            nb_loop --;
            if (chk != 0 && nb_loop > 0) {

                XBT_INFO("Node %d: check not ok (loop %d) Waits a moment ...",
                        me->self.id,
                        10-nb_loop);

                MSG_process_sleep(10.0);
            }
        } while (chk != 0 && nb_loop > 0);

        display_rout_table(me, 'V');
        xbt_assert(chk == 0,
                "Node %d consistency check error",
                me->self.id);

        XBT_VERB("Node %d: Check OK - Split stage %d start",
                me->self.id,
                stage);

        // broadcasts a split task to all concerned nodes
        task_sent = NULL;

        broadcast_args.broadcast.type = TASK_SPLIT;
        broadcast_args.broadcast.stage = stage;
        broadcast_args.broadcast.first_call = 1;
        broadcast_args.broadcast.source_id = me->self.id;
        broadcast_args.broadcast.new_node_id = new_node_id;
        broadcast_args.broadcast.lead_br = 0;

        broadcast_args.broadcast.args = xbt_new0(u_req_args_t, 1);
        broadcast_args.broadcast.args->split.stage_nbr = stage;
        broadcast_args.broadcast.args->split.new_node_id = new_node_id;
        make_broadcast_task(me, broadcast_args, &task_sent);

        handle_task(me, &task_sent);

        xbt_free(broadcast_args.broadcast.args);
        broadcast_args.broadcast.args = NULL;
    }
    XBT_OUT();
}

/**
 * \brief Adds a new stage (every node has to run this function via a broadcast)
 * \param me the current node
 */
static void add_stage(node_t me) {

    XBT_IN();
    me->height++;

    // add stage to brothers
    me->bro_index = realloc(me->bro_index, me->height * sizeof(int));
    me->brothers = realloc(me->brothers, me->height * sizeof(node_rep_t));

    xbt_assert((me->brothers != NULL) && (me->bro_index != NULL),
            "Node %d: Can't add stage %d to routing table",
            me->self.id,
            me->height);

    me->bro_index[me->height-1] = 0;
    me->brothers[me->height-1] = xbt_new0(s_node_rep_t, b);
    add_brother(me, me->height-1, me->self.id);

    // add stage to predecessors  
    me->pred_index = realloc(me->pred_index, me->height * sizeof(int));
    me->preds = realloc(me->preds, me->height * sizeof(node_rep_t));

    xbt_assert((me->preds != NULL) && (me->pred_index != NULL),
            "Node %d: Can't add stage %d to predecessors table",
            me->self.id,
            me->height);

    // add stage to DST infos
    me->dst_infos.load = realloc(me->dst_infos.load, me->height * sizeof(int));

    xbt_assert(me->dst_infos.load != NULL,
            "Node %d: Can't add stage %d to load table",
            me->self.id,
            me->height);

    me->pred_index[me->height-1] = 0;
    //me->dst_infos.load[me->height-1] = 0;
    me->preds[me->height-1] = xbt_new0(s_node_rep_t, b);
    add_pred(me, me->height-1, me->self.id);

    XBT_VERB("Node %d: New stage %d added for me", me->self.id, me->height-1);
    XBT_OUT();
}

/**
 * \brief Adds a predecessor for the current node.
 * \param me the current node
 * \param stage stage in which to add a new predecessor (0 to height - 1)
 * \param id id to set for this new predecessor
 */
static void add_pred(node_t me, int stage, int id) {

    XBT_IN();

    s_state_t state = get_state(me);
    XBT_VERB("Node %d: '%c'/%d - add_pred(%d) ...",
            me->self.id,
            state.active,
            state.new_id,
            id);

    xbt_assert(stage < me->height,
            "Node %d: height error - stage = %d - height = %d",
            me->self.id,
            stage,
            me->height);

    // nothing to do (pred already exists)
    if (index_pred(me, stage, id) != -1) return;

    me->preds[stage][me->pred_index[stage]].id = id;
    set_mailbox(id, me->preds[stage][me->pred_index[stage]].mailbox);

    me->pred_index[stage]++;

    // set DST infos
    me->dst_infos.load[stage] = me->pred_index[stage];

    // set the predecessors array bigger (by steps of b) if needed
    if ((me->pred_index[stage] % b == 0) && (me->pred_index[stage] > 0)) {

        //node_rep_t backup = me->preds[stage];
        me->preds[stage] = realloc(me->preds[stage],
                (me->pred_index[stage] + b) * sizeof(s_node_rep_t));

        xbt_assert(me->preds[stage] != NULL,
                "Node %d: Realloc of preds[%d] failed",
                me->self.id,
                stage);

        int i;
        for (i = me->pred_index[stage]; i < me->pred_index[stage] + b; i++) {

            me->preds[stage][i].id = 0;
            set_mailbox(0, me->preds[stage][i].mailbox);
        }

        XBT_DEBUG("Node %d: [%s:%d] Predecessors array has been set bigger",
                me->self.id,
                __FUNCTION__,
                __LINE__);
    }

    /* if current state is 'p', then it's an add_pred coming from load_balance() :
       just pops out this state */

    XBT_DEBUG("Node %d: [%s:%d] Before remove",
            me->self.id,
            __FUNCTION__,
            __LINE__);

    display_states(me, 'D');
    state = get_state(me);

    // remove 'p' state
    int idx = state_search(me, 'p', id);
    if (idx > -1) {

        XBT_DEBUG("Node %d: [%s:%d] idx = %d",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                idx);

        xbt_dynar_remove_at(me->states, idx, NULL);

        XBT_VERB("Node %d: [%s:%d] 'p' state removed for new_id %d",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                id);
    }

    display_states(me, 'V');

    /*
    // only an add_pred sent by new coming node will pop out current state
    if (state.active == 'p' && me->self.id != state.new_id) { // && state.new_id == id) {

        state_t state_ptr = NULL;
        xbt_dynar_pop(me->states, &state_ptr);
        xbt_free(state_ptr);

        s_state_t new_state = get_state(me);

        XBT_VERB("Node %d: in add_pred(), restore from state '%c'/%d to state '%c'/%d",
                me->self.id,
                state.active,
                state.new_id,
                new_state.active,
                new_state.new_id);
    }
    */

    XBT_OUT();
}

/**
 * \brief Deletes a predecessor for the current node
 * \param me the current node
 * \param stage the stage in which to delete a predecessor
 * \param pred2del the predecessor to delete
 */
static void del_pred(node_t me, int stage, int pred2del) {

    XBT_IN();

    display_preds(me, 'D');
    XBT_VERB("Node %d: stage = %d, pred2del = %d", me->self.id, stage, pred2del);

    int idx = index_pred(me, stage, pred2del);

    // if pred2del exist
    if (idx > -1 && pred2del != me->self.id) {

        int size = me->pred_index[stage];
        if (idx < size - 1) {

            int i;
            for (i = idx; i < size-1; i++) {

                me->preds[stage][i] = me->preds[stage][i+1];
            }
            idx = i;
        }

        me->preds[stage][idx].id = -1;
        set_mailbox(-1, me->preds[stage][idx].mailbox);
        me->pred_index[stage]--;

        // set DST infos
        me->dst_infos.load[stage] = me->pred_index[stage];

        /*
        // remove 'p' state
        idx = state_search(me, 'p', pred2del);
        if (idx > -1) {

            XBT_DEBUG("Node %d: [%s:%d] idx = %d",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    idx);

            xbt_dynar_remove_at(me->states, idx, NULL);

            XBT_VERB("Node %d: [%s:%d] 'p' state removed for new_id %d",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    pred2del);
        }
        */
    }

    XBT_OUT();
}

/**
 * \brief Delete a part of current group at a given stage
 * \param me the current node
 * \param stage the concerned stage
 * \param start
 * \param end start and end bound the deleted part
 */
static void del_member(node_t me, int stage, int start, int end) {

    XBT_IN();

    int i, j, pos2del;
    int nb_del = end - start + 1;
    if (nb_del == 0) return;

    xbt_assert(nb_del > 0,
            "Node %d: del_member ERROR !! stage = %d, end = %d, start = %d, nb_del = %d%s",
            me->self.id,
            stage,
            end,
            start,
            nb_del,
            routing_table(me));

    int* id_del = xbt_new0(int, nb_del);

    // fill local id array (nodes to be deleted) before current node is modified
    for (i = 0; i < nb_del; i++) {

        id_del[i] = me->brothers[stage][start + i].id;
    }

    msg_error_t res;
    u_req_args_t args;
    args.del_pred.stage = stage;
    args.del_pred.pred2del_id = me->self.id;
    args.del_pred.new_node_id = me->self.id;    //TODO : solution temporaire à reprendre

    // deleting loop
    for (i = 0; i < nb_del; i++) {

        if (id_del[i] != me->self.id) {     //can't delete 'me'

            pos2del = index_bro(me, stage, id_del[i]);

            XBT_VERB("Node %d: in del_member() - stage = %d - id_del = %d - pos2del = %d",
                    me->self.id,
                    stage,
                    id_del[i],
                    pos2del);

            // delete brother
            if (pos2del < me->bro_index[stage] - 1) {

                for (j = pos2del; j < me->bro_index[stage] - 1; j++) {

                    me->brothers[stage][j] = me->brothers[stage][j + 1];
                }
            } else {

                j = pos2del;
            }

            me->brothers[stage][j].id = -1;
            set_mailbox(0, me->brothers[stage][j].mailbox);
            me->bro_index[stage]--;

            XBT_VERB("Node %d: node %d deleted - %s",
                    me->self.id,
                    id_del[i],
                    routing_table(me));

            /*
            XBT_VERB("Node %d: node %d exist as pred in stage %d ? %s",
                    me->self.id,
                    id_del[i],
                    stage,
                    (index_pred(me, stage, id_del[i]) == -1 ? "No" : "Yes"));

            // delete pred (if id_del[i] is a predecessor of mine)
            del_pred(me, stage, id_del[i]);

            XBT_VERB("Node %d: pred %d deleted",
                    me->self.id,
                    id_del[i]);

            display_preds(me, 'D');
            */

            // 'me' is no longer a predecessor of id_del[i]
            res = send_msg_async(me,
                    TASK_DEL_PRED,
                    id_del[i],
                    args);

            xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                    me->self.id,
                    debug_msg[TASK_DEL_PRED],
                    id_del[i]);
        }
    }

    xbt_free(id_del);
    XBT_OUT();
}

/**
 * \brief Cut node during a transfer
 * \param me the current node
 * \param stage stage where cutting takes place
 * \param right leaving side (1 if right part leaves)
 * \param cut_pos cut position (cut_pos included in the leaving part)
 * \param new_node_id the involved leaving node
 */
static void cut_node(node_t me, int stage, int right, int cut_pos, int new_node_id) {

    XBT_IN();

    int start, end;
    int pos_me = index_bro(me, stage, me->self.id);

    // needed by shift_bro to update upper stage (see further)
    s_node_rep_t new_node;

    /*** update leaving nodes's upper stage ***/

    // start and end bound the leaving part
    switch (right) {

        case 0:
            start = 0;
            end = cut_pos;
            /* TODO : voir s'il ne faut pas plutôt transmettre le new_node depuis le
               noeud appelant (47 dans l'exemple) */
            new_node = me->brothers[stage][cut_pos + 1];
            break;

        case 1:
            start = cut_pos;
            end = me->bro_index[stage] - 1;
            new_node = me->brothers[stage][0];
            break;
    }

    // TODO : voir ce qu'il se passe lorsq'on est sur la racine
    XBT_VERB("Node %d: update upper stage - start = %d - end = %d"
            " - stage = %d - new_node = %d - right = %d",
            me->self.id,
            start,
            end,
            stage + 1,
            new_node.id,
            right);

    // only the leaving part has to update its upper stage
    if (pos_me >= start && pos_me <= end) {

        XBT_VERB("Node %d: call to shift_bro()"
                " - stage = %d - new_node = %d - right = %d",
                me->self.id,
                stage + 1,
                new_node.id,
                right);

        // local call
        shift_bro(me,
                stage + 1,
                new_node,
                right,
                new_node_id);
    }

    /*** split nodes at given stage with del_member() ***/

    // start and end bound the leaving part
    switch (right) {

        case 0:
            if (pos_me <= cut_pos) {

                start = cut_pos + 1;
                end = me->bro_index[stage] - 1;
            } else {

                start = 0;
                end = cut_pos;
            }
            break;

        case 1:
            if (pos_me < cut_pos) {

                start = cut_pos;
                end = me->bro_index[stage] - 1;
            } else {

                start = 0;
                end = cut_pos - 1;
            }
            break;
    }

    XBT_VERB("Node %d: call del_member()"
            " - stage = %d - start = %d - end = %d - right = %d - pos_me = %d - cut_pos = %d",
            me->self.id,
            stage,
            start,
            end,
            right,
            pos_me,
            cut_pos);

    del_member(me, stage, start, end);

    XBT_OUT();
}

/**
 * \brief Add a brother at a given stage
 * \param me the current node
 * \param stage the given stage
 * \param id the new brother's id
 */
static void add_brother(node_t me, int stage, int id) {

    XBT_IN();

    // nothing to do (brother already exists)
    if (index_bro(me, stage, id) != -1) return;

    xbt_assert(me->bro_index[stage] < b,
            "Node %d: Can't have more brothers for stage %d%s",
            me->self.id,
            stage,
            routing_table(me));

    me->brothers[stage][me->bro_index[stage]].id = id;
    set_mailbox(id, me->brothers[stage][me->bro_index[stage]].mailbox);
    me->bro_index[stage]++;

    XBT_OUT();
}

/**
 * \brief insert a new brother at the begining of a list
 * \param me the current node
 * \param stage the concerned stage
 * \param id the id of the node to be inserted
 */
// TODO : est-il possible d'y inclure les ADD_PRED ?
static void insert_bro(node_t me, int stage, int id) {

    XBT_IN();
    //display_rout_table(me, 'V');
    // nothing to do (brother already exists)
    if (index_bro(me, stage, id) != -1) return;

    xbt_assert(me->bro_index[stage] < b,
            "Node %d: Can't have more brothers for stage %d%s",
            me->self.id,
            stage,
            routing_table(me));

    int i;
    for (i = me->bro_index[stage] - 1; i >= 0; i--) {

        me->brothers[stage][i + 1] = me->brothers[stage][i];
    }

    me->brothers[stage][0].id = id;
    set_mailbox(id, me->brothers[stage][0].mailbox);
    me->bro_index[stage]++;
    display_rout_table(me, 'V');
    XBT_OUT();
}

/* \brief Add an array of brothers to the current routing table, at a given
 *        stage, to a given side
 * \param me the current node
 * \param stage add new nodes at this stage
 * \param bro array of coming brothers
 * \param array_size number of coming brothers
 * \param right side where to add the given array
 * \param new_node_id the new involved coming node
 */
static void add_bro_array(node_t me, int stage, node_rep_t bro, int array_size, int right, int new_node_id) {
    XBT_IN();

    int i;
    msg_error_t res;
    u_req_args_t args;

    XBT_VERB("Node %d: in add_bro_array() - stage = %d - size = %d - right = %d",
            me->self.id,
            stage,
            array_size,
            right);

    args.add_pred.stage = stage;
    args.add_pred.new_pred_id = me->self.id;
    args.add_pred.new_node_id = new_node_id;
    args.add_pred.w_ans = 0;

    if (right == 0) {

        for (i = array_size - 1; i >= 0; i--) {

            XBT_VERB("Node %d: in add_bro_array() - i = %d - id = %d",
                    me->self.id,
                    i,
                    bro[i].id);

            insert_bro(me,
                    stage,
                    bro[i].id);

            res = send_msg_async(me,
                    TASK_ADD_PRED,
                    bro[i].id,
                    args);

            xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                    me->self.id,
                    debug_msg[TASK_ADD_PRED],
                    bro[i].id);
        }

    } else {

        for (i = 0; i < array_size; i++) {

            add_brother(me,
                    stage,
                    bro[i].id);

            res = send_msg_async(me,
                    TASK_ADD_PRED,
                    bro[i].id,
                    args);

            xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                    me->self.id,
                    debug_msg[TASK_ADD_PRED],
                    bro[i].id);
        }
    }

    XBT_OUT();
}

/* \brief Broadcast an add_bro_array task from given stage
 * \param me the current node
 * \param stage add new nodes at this stage
 * \param bro array of coming brothers
 * \param array_size number of coming brothers
 * \param right coming side
 * \param new_node_id the new involved coming node
 */
static void br_add_bro_array(node_t me, int stage, node_rep_t bro, int array_size, int right, int new_node_id) {

    XBT_IN();

    msg_error_t res;
    u_req_args_t args;
    msg_task_t task_sent = NULL;

    args.broadcast.type = TASK_ADD_BRO_ARRAY;
    args.broadcast.stage = stage;
    args.broadcast.first_call = 1;
    args.broadcast.source_id = me->self.id;
    args.broadcast.new_node_id = new_node_id;
    args.broadcast.lead_br = 0;

    args.broadcast.args = xbt_new0(u_req_args_t, 1);

    args.broadcast.args->add_bro_array.stage = stage;
    args.broadcast.args->add_bro_array.bro = bro;
    args.broadcast.args->add_bro_array.array_size = array_size;
    args.broadcast.args->add_bro_array.right = right;
    args.broadcast.args->add_bro_array.new_node_id = new_node_id;

    make_broadcast_task(me, args, &task_sent);
    handle_task(me, &task_sent);

    xbt_free(args.broadcast.args);
    args.broadcast.args = NULL;

    XBT_OUT();
}

/**
 * \brief After a transfer has occured at a certain stage, one of the upper
 *        stage rep points to the part that left and has to be replaced by
 *        another one pointing to the staying part.
 * \param me the current node
 * \param stage the stage where the transfer took place
 * \param pos2repl position of the rep to be replaced
 * \param new_id the rep pointing to the staying part
 * \param new_node_id the involved leaving node
 */
static void update_upper_stage(node_t me, int stage, int pos2repl, int new_id, int new_node_id) {

    XBT_IN();

    // TODO : new_id à -1 plus nécessaire (à vérifier)
    if (new_id > -1) {

        XBT_VERB("Node %d: update upper stage. pos2repl = %d - new_id = %d - transfer stage = %d%s",
                me->self.id,
                pos2repl,
                new_id,
                stage,
                routing_table(me));

        if (me->brothers[stage + 1][pos2repl].id != me->self.id) {

            // for logging
            int mem_id = me->brothers[stage + 1][pos2repl].id;

            replace_bro(me,
                    stage + 1,
                    pos2repl,
                    new_id,
                    new_node_id);

            XBT_VERB("Node %d: node %d replaced by node %d in stage %d",
                    me->self.id,
                    mem_id,
                    new_id,
                    stage + 1);
        }
    }

    XBT_OUT();
}

/**
 * \brief delete brother 'bro2del' from stage 'stage'
 * \param me the current node
 * \param stage the stage where to delete a brother
 * \param bro2del the brother to delete
 */
static void del_bro(node_t me, int stage, int bro2del) {

    XBT_IN();
    // 'me' can't be deleted
    xbt_assert(bro2del != me->self.id,
            "Node %d : [%s:%c] Can't delete 'me' !! - stage = %d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            stage);

    // log
    if (XBT_LOG_ISENABLED(msg_dst, xbt_log_priority_trace)) {

        XBT_DEBUG("Node %d: [%s:%d] Before deletion - bro to del : %d",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                bro2del);

        printf("me.brothers[%d] = { ", stage);

        int j = 0;
        for ( j = 0; j < me->bro_index[stage] - 1; j++ ) {

            printf("%d , ", me->brothers[stage][j].id);
        }
        printf("%d }\n\n", me->brothers[stage][j].id);
    }

    int size = me->bro_index[stage];
    int idx = index_bro(me, stage, bro2del);

    if (idx < size - 1) {

        int i = 0;
        for (i = idx; i < size - 1; i++) {

            me->brothers[stage][i] = me->brothers[stage][i + 1];
        }
    }

    me->brothers[stage][size - 1].id = -1;
    set_mailbox(-1, me->brothers[stage][size - 1].mailbox);
    me->bro_index[stage]--;

    // log
    if (XBT_LOG_ISENABLED(msg_dst, xbt_log_priority_trace)) {

        XBT_DEBUG("Node %d: [%s:%d] After deletion",
                me->self.id,
                __FUNCTION__,
                __LINE__);

        printf("me.brothers[%d] = { ", stage);

        int j = 0;
        for ( j = 0; j < me->bro_index[stage] - 1; j++ ) {

            printf("%d , ", me->brothers[stage][j].id);
        }
        printf("%d }\n\n", me->brothers[stage][j].id);
    }

    XBT_OUT();
}

/**
 * \brief This function tells a node that one of his sons of a given stage has
 *        split. As their father, it must connect to those two new sons.
 * \param me the current node
 * \param stage the stage that owns a new son
 * \param pos_init the position of init_rep in the calling node's stage above the splitted one
 * \param pos_new the position of new_rep (the size of this upper stage)
 * \param init_rep_id a representative of the former node
 * \param new_rep_id a representative of the new node
 * \param new_node_id the new node that's being inserted
 */
static void connect_splitted_groups(node_t me,
        int stage,
        int pos_init,
        int pos_new,
        int init_rep_id,
        int new_rep_id,
        int new_node_id) {

    XBT_IN();

    XBT_VERB("Node %d: In connect_splitted_groups:\nstage = %d, init_rep_id = %d,"
            " pos_init = %d, pos_new = %d, new_rep_id = %d",
            me->self.id,
            stage,
            init_rep_id,
            pos_init,
            pos_new,
            new_rep_id);

    xbt_assert(pos_init >= 0 && pos_new >= 0,
            "Node %d: Error - pos_init = %d - pos_new = %d",
            me->self.id,
            pos_init,
            pos_new);

    /* Stops if pos_new is too big
       (
       could happen if 'me' had been added too late as a pred of a new coming node,
       i.e. after CNX_GROUPS sendings during split() occurred
       )
       */

    xbt_assert(pos_new <= me->bro_index[stage],
            "Node %d: Error - pos_new = %d - bro_index[%d] = %d\n%s",
            me->self.id,
            pos_new,
            stage,
            me->bro_index[stage],
            routing_table(me));

    display_rout_table(me, 'V');
    display_preds(me, 'D');

    // function already called: do nothing
    if (pos_new < me->bro_index[stage]) return;

    /* current node is being updated (normaly already at 'u' by broadcast from
       connection_request(), but can be stored and delayed) */
    set_update(me, new_node_id, -1);   //TODO : à vérifier

    s_state_t state = get_state(me);
    XBT_VERB("Node %d: '%c'/%d - connect_splitted_groups() ... - new_node = %d",
            me->self.id,
            state.active,
            state.new_id,
            new_node_id);

    // if pos_init is greater than index, no node is replaced
    int rep_id;
    if (pos_init < me->bro_index[stage]) {

        rep_id = me->brothers[stage][pos_init].id;
    } else {

        rep_id = init_rep_id;
    }

    // eventually use 'me' instead of init_rep
    if (index_bro(me, stage, me->self.id) == pos_init) {
        if (new_rep_id != me->self.id) {
            init_rep_id = me->self.id;
        }
    }

    // both rep are already known: do nothing
    if (index_bro(me, stage, new_rep_id) != -1 &&
            index_bro(me, stage, init_rep_id) != -1) return;

    int cpt = 0;
    recp_rec_t elem = NULL;

    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());
    msg_error_t res;
    u_req_args_t args;
    args.add_pred.stage = stage;
    args.add_pred.new_pred_id = me->self.id;
    args.add_pred.new_node_id = new_node_id;
    args.add_pred.w_ans = 1;

    // insert init_rep and new_rep
    XBT_DEBUG("Node %d: insert %d in brothers[%d][%d]",
            me->self.id,
            init_rep_id,
            stage,
            pos_init);

    me->brothers[stage][pos_init].id = init_rep_id;
    set_mailbox(init_rep_id, me->brothers[stage][pos_init].mailbox);

    XBT_DEBUG("Node %d: add %d at the end",
            me->self.id,
            new_rep_id);

    add_brother(me, stage, new_rep_id);

    if (rep_id != init_rep_id) {

        // rep_id has been replaced
        XBT_DEBUG("Node %d: former brother[%d][%d] (%d) has been replaced by %d",
                me->self.id,
                stage,
                pos_init,
                rep_id,
                init_rep_id);

        if (rep_id != me->self.id) {
            res = send_msg_async(me,
                    TASK_DEL_PRED,
                    rep_id,
                    args);          //TODO: les args sont positionnés avec add_pred. Pb ?

            xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                    me->self.id,
                    debug_msg[TASK_DEL_PRED],
                    rep_id);

            // record recipient
            elem = xbt_new0(s_recp_rec_t, 1);

            elem->type = TASK_DEL_PRED;
            elem->br_type = TASK_NULL;
            elem->recp.id = rep_id;
            elem->new_node_id = new_node_id;
            set_mailbox(rep_id, elem->recp.mailbox);
            elem->answer_data = NULL;
            xbt_dynar_push(proc_data->async_answers, &elem);

            xbt_assert(elem->recp.id > - 1,
                    "Node %d: #3# recp.id is %d !!",
                    me->self.id,
                    elem->recp.id);

            cpt++;
        }

        if (init_rep_id != me->self.id) {

            res = send_msg_async(me,
                    TASK_ADD_PRED,
                    init_rep_id,
                    args);

            xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                    me->self.id,
                    debug_msg[TASK_ADD_PRED],
                    init_rep_id);

            elem = xbt_new0(s_recp_rec_t, 1);

            elem->type = TASK_ADD_PRED;
            elem->br_type = TASK_NULL;
            elem->recp.id = init_rep_id;
            elem->new_node_id = new_node_id;
            set_mailbox(init_rep_id, elem->recp.mailbox);
            elem->answer_data = NULL;
            xbt_dynar_push(proc_data->async_answers, &elem);

            xbt_assert(elem->recp.id > - 1,
                    "Node %d: #4# recp.id is %d !!",
                    me->self.id,
                    elem->recp.id);

            cpt++;
        }
    }

    // tells new_rep that he's referenced by the current node
    if (new_rep_id != me->self.id) {

        res = send_msg_async(me,
                TASK_ADD_PRED,
                new_rep_id,
                args);

        xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                me->self.id,
                debug_msg[TASK_ADD_PRED],
                new_rep_id);

        // record recipient
        /*
           xbt_assert(new_rep_id == me->brothers[stage][me->bro_index[stage] - 1].id,
           "Node %d: record recipient error - new_rep_id = %d - record_id = %d",
           me->self.id,
           new_rep_id,
           me->brothers[stage][me->bro_index[stage] - 1].id);
           */

        elem = xbt_new0(s_recp_rec_t, 1);

        elem->type = TASK_ADD_PRED;
        elem->br_type = TASK_NULL;
        elem->recp.id = new_rep_id;
        elem->new_node_id = new_node_id;
        set_mailbox(new_rep_id, elem->recp.mailbox);
        elem->answer_data = NULL;
        xbt_dynar_push(proc_data->async_answers, &elem);

        xbt_assert(elem->recp.id > - 1,
                "Node %d: #5# recp.id is %d !!",
                me->self.id,
                elem->recp.id);

        cpt++;
    }

    //synchro (4)
    if (cpt > 0) {

        wait_for_completion(me, cpt, new_node_id);
    }

    /*
     * Don't restore initial state here, it would be too early. It'll be done
     * in connection_request() by broadcasting SET_ACTIVE.
     */     //TODO : Si, parce que SET_ACTIVE n'est plus diffusé qu'aux leaders
    /*
    int idx = state_search(me, 'u', new_node_id);
    if (idx > -1) {

        XBT_DEBUG("Node %d: idx = %d", me->self.id, idx);
        xbt_dynar_remove_at(me->states, idx, NULL);
    }
    */

    state = get_state(me);
    XBT_VERB("Node %d: '%c'/%d - end of connect_splitted_groups() ... - new_node = %d",
            me->self.id,
            state.active,
            state.new_id,
            new_node_id);

    display_rout_table(me, 'V');
    display_preds(me, 'D');
    XBT_OUT();
}

/**
 * \brief When a node splits, each of his members has to run this function.
 *        Half of them will leave the node to create a new one.
 * \param me the current node
 * \param stage the stage number that has to be splitted (0 ... me->height-1)
 * \param new_node_id the new node that's being inserted
 */
static void split(node_t me, int stage, int new_node_id) {

    XBT_IN();

    // splitting node isn't available for requests
    set_update(me, new_node_id, -1);
    s_state_t state = get_state(me);
    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());

    XBT_VERB("Node %d: [%s:%d] '%c'/%d - splitting stage %d ... - new_node = %d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            state.active,
            state.new_id,
            stage,
            new_node_id);

    XBT_DEBUG("Node %d: [%s:%d] routing table before split", me->self.id, __FUNCTION__, __LINE__);
    display_rout_table(me, 'D');
    display_preds(me, 'D');

    /* copy local clock into first brother's (it will become a leader after split) */
    //TODO : à faire ??

    // finds a representative of the other node (after splitting)
    int pos = index_bro(me, stage, me->self.id);
    int stay = (pos + 1) % 2;

    int other_node_id = 0;
    if (stay == 0) {

        other_node_id = me->brothers[stage][pos - 1].id;
    } else {

        if (pos == me->bro_index[stage] - 1) {

            other_node_id = me->brothers[stage][pos - 1].id;
        } else {

            other_node_id = me->brothers[stage][pos + 1].id;
        }
    }

    // splits
    node_rep_t new_grp = xbt_new0(s_node_rep_t, b);
    int i = 0;
    int idx = 0;
    int init_rep_id, new_rep_id = 0;
    msg_error_t res = MSG_OK;
    u_req_args_t args;

    // prepare the dynar of all async messages recipients
    int cpt = 0;
    recp_rec_t elem = NULL;

    if (stay == 1) {

        // current node stays
        for (i = 0; i < me->bro_index[stage]; i++) {

            if (i % 2 == 0) {

                new_grp[idx] = me->brothers[stage][i];
                idx++;
            } else {

                args.del_pred.stage = stage;
                args.del_pred.pred2del_id = me->self.id;
                args.del_pred.new_node_id = new_node_id;

                res = send_msg_async(me,
                        TASK_DEL_PRED,
                        me->brothers[stage][i].id,
                        args);

                xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                        me->self.id,
                        debug_msg[TASK_DEL_PRED],
                        me->brothers[stage][i].id);

                // record recipient
                elem = xbt_new0(s_recp_rec_t, 1);

                elem->type = TASK_DEL_PRED;
                elem->br_type = TASK_NULL;
                elem->recp = me->brothers[stage][i];
                elem->new_node_id = new_node_id;
                elem->answer_data = NULL;
                xbt_dynar_push(proc_data->async_answers, &elem);

                xbt_assert(elem->recp.id > - 1,
                        "Node %d: #6# recp.id is %d !!",
                        me->self.id,
                        elem->recp.id);

                cpt++;
            }
        }

        init_rep_id = me->self.id;
        new_rep_id = other_node_id;
    } else {

        // current node leaves
        for (i = 0; i < me->bro_index[stage]; i++) {

            if (i % 2 == 1) {

                new_grp[idx] = me->brothers[stage][i];
                idx++;
            } else {

                args.del_pred.stage = stage;
                args.del_pred.pred2del_id = me->self.id;
                args.del_pred.new_node_id = new_node_id;

                res = send_msg_async(me,
                        TASK_DEL_PRED,
                        me->brothers[stage][i].id,
                        args);

                xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                        me->self.id,
                        debug_msg[TASK_DEL_PRED],
                        me->brothers[stage][i].id);
                // record recipient
                elem = xbt_new0(s_recp_rec_t, 1);

                elem->type = TASK_DEL_PRED;
                elem->br_type = TASK_NULL;
                elem->recp = me->brothers[stage][i];
                elem->new_node_id = new_node_id;
                elem->answer_data = NULL;
                xbt_dynar_push(proc_data->async_answers, &elem);

                xbt_assert(elem->recp.id > - 1,
                        "Node %d: #7# recp.id is %d !!",
                        me->self.id,
                        elem->recp.id);

                cpt++;
            }
        }

        init_rep_id = other_node_id;
        new_rep_id = me->self.id;
    }

    xbt_assert(cpt <= me->bro_index[stage],
            "Node %d: [%s:%d] cpt too high (%d) - bro_index[%d] = %d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            cpt,
            stage,
            me->bro_index[stage]);

    XBT_DEBUG("Node %d: [%s:%d] init_rep_id = %d - new_rep_id = %d",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            init_rep_id,
            new_rep_id);

    xbt_free(me->brothers[stage]);
    me->brothers[stage] = new_grp;
    me->bro_index[stage] = idx;

    // synchro (2)
    if (cpt > 0) {

        wait_for_completion(me, cpt, new_node_id);
    }

    xbt_assert(idx <= b, "[%s:%d] DST out of bounds. idx = %d", __FUNCTION__, __LINE__, idx);

    XBT_VERB("Node %d: [%s:%d] routing table after split",
            me->self.id,
            __FUNCTION__,
            __LINE__);

    display_rout_table(me, 'V');

    // set and store DST infos
    set_n_store_infos(me);

    display_preds(me, 'D');

    // wait until state is not 'p' anymore to launch calls of connect_splitted_groups
    XBT_DEBUG("Node %d: [%s:%d] Wait for no 'p' state",
            me->self.id,
            __FUNCTION__,
            __LINE__);

    display_states(me, 'D');

    int found = -1;
    float max_wait = MSG_get_clock() + MAX_WAIT_COMPL / 2;
    do {
        found = state_search(me, 'p', -1);
        if (found > -1) {

            MSG_process_sleep(0.1);
        }
    } while (found > -1 && MSG_get_clock() < max_wait);

    // stops here if 'p' state is not removed
    if (MSG_get_clock() >= max_wait && found > -1) {

        XBT_INFO("Node %d: [%s:%d] 'p' state not removed yet",
                me->self.id,
                __FUNCTION__,
                __LINE__);
        display_states(me, 'I');
        xbt_assert(1 == 0);
    }

    XBT_DEBUG("Node %d: [%s:%d] no more state 'p'", me->self.id, __FUNCTION__, __LINE__);

    // tells every upper stage pred it's got a new member
    //int ans_cpt = me->pred_index[stage + 1];
    int ans_cpt = 0;
    args.cnx_groups.stage = stage + 1;
    args.cnx_groups.pos_init = index_bro(me, stage + 1, me->self.id);
    args.cnx_groups.pos_new = me->bro_index[stage + 1];
    args.cnx_groups.init_rep_id = init_rep_id;
    args.cnx_groups.new_rep_id = new_rep_id;
    args.cnx_groups.new_node_id = new_node_id;

    // works on a copy of upper stage preds
    int cpy_pred_index = me->pred_index[stage + 1];
    node_rep_t cpy_preds = xbt_new0(s_node_rep_t, me->pred_index[stage + 1]);

    int hist_cpy_pred_index = me->pred_index[stage + 1];
    node_rep_t hist_cpy_preds = xbt_new0(s_node_rep_t, me->pred_index[stage + 1]);

    XBT_DEBUG("Node %d: [%s:%d] make a copy of upper stage preds",
            me->self.id,
            __FUNCTION__,
            __LINE__);

    for (i = 0; i < cpy_pred_index ; i++) {

        cpy_preds[i] = me->preds[stage + 1][i];
        hist_cpy_preds[i] = me->preds[stage + 1][i];
        XBT_DEBUG("cpy_preds[%d] = %d", i, cpy_preds[i].id);
    }

    int cpy_pred_index2 = 0;
    node_rep_t cpy_preds2 = NULL;
    int cpt_loop = 2;           //TODO : pas forcément utile de le faire plus d'une fois ?
    int j = 0;
    int k = 0;
    do {
        cpy_pred_index2 = 0;

        // send CNX_GROUPS to each stage+1 pred
        for (i = 0; i < cpy_pred_index; i++) {
            if (cpy_preds[i].id == me->self.id) {

                // local call - see at the end (no answer is expected)
                //ans_cpt--;

            } else {

                // remote call
                res = send_msg_async(me,
                        TASK_CNX_GROUPS,
                        cpy_preds[i].id,
                        args);

                xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                        me->self.id,
                        debug_msg[TASK_CNX_GROUPS],
                        cpy_preds[i].id);


                elem = xbt_new0(s_recp_rec_t, 1);

                elem->type = TASK_CNX_GROUPS;
                elem->br_type = TASK_NULL;
                elem->recp = cpy_preds[i];
                elem->new_node_id = new_node_id;
                elem->answer_data = NULL;
                xbt_dynar_push(proc_data->async_answers, &elem);

                ans_cpt++;

                // elem->recp has to exist
                xbt_assert(elem->recp.id > - 1,
                        "Node %d: [%s:%d] #8# recp.id is %d !!",
                        me->self.id,
                        __FUNCTION__,
                        __LINE__,
                        elem->recp.id);
            }
        }

        // display all preds that have been contacted
        for (i = 0; i < cpy_pred_index; i++) {
            XBT_DEBUG("Node %d: [%s:%d] before : cpy_preds[%d] = %d - loop = %d",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    i,
                    cpy_preds[i].id,
                    cpt_loop);
        }

        // Check if new preds have been added meanwhile
        if (cpt_loop > 0) {

            cpt_loop--;
            cpy_preds2 = xbt_new0(s_node_rep_t, me->pred_index[stage + 1]);

            // build an array of new preds
            for (j = 0; j < me->pred_index[stage + 1]; j++) {

                for (k = 0; k < hist_cpy_pred_index; k++) {

                    if ((me->preds[stage + 1][j].id == hist_cpy_preds[k].id) ||
                            (me->preds[stage + 1][j].id == me->self.id)) {

                        break;
                    }
                }

                // if pred not found in cpy_preds, adds it
                if (k == hist_cpy_pred_index) {

                    cpy_preds2[cpy_pred_index2] = me->preds[stage + 1][j];
                    cpy_pred_index2++;
                }
            }

            // new pred(s) have been found : replace cpy_preds with them
            if (cpy_pred_index2 > 0) {

                XBT_VERB("Node %d: [%s:%d] new_preds have come",
                        me->self.id,
                        __FUNCTION__,
                        __LINE__);

                xbt_free(cpy_preds);
                cpy_pred_index = cpy_pred_index2;
                cpy_preds = xbt_new0(s_node_rep_t, cpy_pred_index);

                // hist_cpy_preds contains all preds that have be sent CNX_GROUPS
                hist_cpy_preds = realloc(hist_cpy_preds, (hist_cpy_pred_index + cpy_pred_index2) * sizeof(s_node_rep_t));

                for (i = 0; i < cpy_pred_index; i++) {

                    cpy_preds[i] = cpy_preds2[i];

                    hist_cpy_preds[hist_cpy_pred_index] = cpy_preds2[i];
                    hist_cpy_pred_index++;

                    XBT_DEBUG("Node %d: [%s:%d] after : cpy_preds[%d] = %d - loop = %d",
                            me->self.id,
                            __FUNCTION__,
                            __LINE__,
                            i,
                            cpy_preds[i].id,
                            cpt_loop);
                }
            }
            xbt_free(cpy_preds2);       //TODO : à sortir du if ?
        }

        /*
        xbt_assert(cpy_pred_index2 == 0 || cpt_loop != 0,
                "Node %d: [%s:%d] loop ERROR ! - cpy_pred_index2 = %d - loop = %d",
                me->self.id,
                __FUNCTION__,
                __LINE__,
                cpy_pred_index2,
                cpt_loop);
                */

    } while(cpy_pred_index2 > 0 && cpt_loop > 0);

    // synchro (3)
    if (ans_cpt > 0) {

        //wait_for_completion(me, ans_cpt, recp_array, cpy_pred_index);
        wait_for_completion(me, ans_cpt, new_node_id);
    }

    // wait until state is not 'p' anymore to launch local call of connect_splitted_groups
    found = -1;
    do {
        found = state_search(me, 'p', -1);
        if (found > -1) {

            MSG_process_sleep(0.1);
        }
    } while (found > -1);

    state = get_state(me);
    XBT_VERB("Node %d: [%s:%d] '%c'/%d - local call of connect_splitted_groups",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            state.active,
            state.new_id);

    // local call
    connect_splitted_groups(me,
            args.cnx_groups.stage,
            args.cnx_groups.pos_init,
            args.cnx_groups.pos_new,
            args.cnx_groups.init_rep_id,
            args.cnx_groups.new_rep_id,
            args.cnx_groups.new_node_id);

    xbt_free(cpy_preds);
    xbt_free(hist_cpy_preds);
    XBT_OUT();
}

/**
 * \brief Searches for a group with enough room to welcome nodes to be merged
 *        If found, returns the index of a rep of that group for the stage
 *        'stage' + 1. Returns -1 otherwise.
 *        This function is called by some leaving node's brother.
 * \param me the current node
 * \param stage the stage to merge to
 * \return The index of a representative of the found group
 */
static int merge_or_transfer(node_t me, int stage) {

    XBT_IN();

    xbt_assert(stage < me->height - 1,
            "Node %d: 'merge_or_transfer' wrong call - stage = %d - height = %d",
            me->self.id,
            stage,
            me->height);

    int idx_bro = 0;
    int merge = 0;
    msg_error_t res;

    u_req_args_t args;
    args.get_size.stage = stage;

    ans_data_t answer_data = NULL;
    u_ans_data_t answer;

    // searches in upper stage brothers
    display_rout_table(me, 'V');
    while ((merge == 0) && (idx_bro < me->bro_index[stage + 1])) {

        XBT_VERB("Node %d: merge = %d - idx_bro = %d - bro = %d",
                me->self.id,
                merge,
                idx_bro,
                me->brothers[stage + 1][idx_bro].id);

        if (me->brothers[stage + 1][idx_bro].id != me->self.id) {

            res = send_msg_sync(me,
                    TASK_GET_SIZE,
                    me->brothers[stage + 1][idx_bro].id,
                    args,
                    &answer_data);

            // send failure
            xbt_assert(res == MSG_OK, "Node %d: Failed to send task '%s' to %d",
                    me->self.id,
                    debug_msg[TASK_GET_SIZE],
                    me->brothers[stage + 1][idx_bro].id);

            answer = answer_data->answer;

            XBT_VERB("Node %d: size = %d - idx_bro = %d - stage = %d",
                    me->self.id,
                    answer.get_size.size,
                    idx_bro,
                    stage);

            if (answer.get_size.size <= b - me->bro_index[stage]) {
            //if (answer.get_size.size <= b - a) 

                merge = 1;
            }
        }
        idx_bro++;
    }

    data_ans_free(me, &answer_data);
    XBT_OUT();

    if (merge == 1) {

        return idx_bro - 1;
    } else {

        return -1;
    }
}

/**
 * \brief Merge 'orphan' nodes ('source') to the current group ('target')
 * \param me the current node (the target)
 * \param nodes_array array of 'orphan' nodes to be merged
 * \param nodes_array_size size of the array
 * \param stage stage where merging takes place
 * \param pos_me upper stage position of source node in its table
 * \param pos_contact upper stage position of target node in source node's table
 * \param right equals 1 (or 11) if 'source' comes from the right of 'target' --
 *              0 (or 10) otherwise.
 * \param new_node_id the new involved coming node
 */
static void merge(
     node_t me,
        int *nodes_array,
        int nodes_array_size,
        int stage,
        int pos_me,
        int pos_contact,
        int right,
        int new_node_id) {

    XBT_IN();

    // only run this function once
    if (nodes_array_size == me->bro_index[stage]) {

        XBT_VERB("Node %d : merge() already run", me->self.id);
        XBT_OUT();
        return;
    }

    display_rout_table(me, 'V');

    // flip coming side if 'me' belongs to the source group
    if (me->bro_index[stage] < a) {

        right = (right + 1) % 2;
        XBT_VERB("Node %d: I belong to the 'source' group. right is now %d",
                me->self.id,
                right);
    }

    int i = 0;
    int *loc_nodes_array = NULL;
    int loc_nodes_array_size = 0;
    //int loc_right = 0;

    u_req_args_t args;
    msg_error_t res;
    args.add_pred.stage = stage;
    args.add_pred.new_pred_id = me->self.id;
    args.add_pred.new_node_id = new_node_id;
    args.add_pred.w_ans = 0;

    /* if merge() is broadcasted, right equals 0 or 1. If merge() is simply
     * called, it equals 10 or 11. This is to manage side flipping for coming
     * nodes during broadcasts.*/
    /*
    switch (right) {

        case 0:                 // new nodes come from the left
            if (index_bro(me, stage + 1, me->self.id) == 0) {

                xbt_assert(1 == 0, "Node %d: case 0", me->self.id);
                loc_right = 1;
            } else {

                loc_right = 0;
            }
            break;

        case 1:                 // new nodes come from the right

            if (index_bro(me, stage + 1, me->self.id) == me->bro_index[stage + 1] - 1) {

                xbt_assert(1 == 0, "Node %d: case 1", me->self.id);
                loc_right = 0;
            } else {

                loc_right = 1;
            }
            break;

        case 10:                // new nodes come from the left

            loc_right = 0;
            break;

        case 11:                // new nodes come from the right

            loc_right = 1;
            break;
    }
    */

    /*
       if (right == !loc_right) {

       XBT_DEBUG("Node %d : side flip - pos_me = %d - pos_contact = %d - stage = %d "
       "- right = %d - loc_right = %d",
       me->self.id,
       pos_me,
       pos_contact,
       stage,
       right,
       loc_right);
       }
       */

    // get number of nodes to be added
    if (nodes_array_size >= me->bro_index[stage]) {

        loc_nodes_array_size = nodes_array_size - me->bro_index[stage];
    } else {

        loc_nodes_array_size = nodes_array_size;
    }

    if (right == 0) {

        // take the left part of the given array
        XBT_VERB("Node %d: Source node comes from the left"
                " - pos_me = %d - pos_contact = %d"
                " - right = %d - right = %d - nodes_array[0] = %d",
                me->self.id,
                pos_me,
                pos_contact,
                right,
                right,
                nodes_array[0]);

        if (loc_nodes_array_size > 0) {

            loc_nodes_array = xbt_new0(int, loc_nodes_array_size);
            for (i = 0; i < loc_nodes_array_size; i++) {

                loc_nodes_array[i] = nodes_array[i];
            }
        }
    } else {

        // take the right part of the given array
        XBT_VERB("Node %d: Source node comes from the right"
                " - pos_me = %d - pos_contact = %d"
                " - right = %d - right = %d - nodes_array[max] = %d",
                me->self.id,
                pos_me,
                pos_contact,
                right,
                right,
                nodes_array[nodes_array_size - 1]);

        if (loc_nodes_array_size > 0) {

            loc_nodes_array = xbt_new0(int, loc_nodes_array_size);

            for (i = nodes_array_size - loc_nodes_array_size;
                    i < nodes_array_size; i++) {

                loc_nodes_array[i - (nodes_array_size - loc_nodes_array_size)] =
                    nodes_array[i];
            }
        }
    }

    XBT_VERB("Node %d: stage = %d - nodes_array_size = %d -"
            " loc_nodes_array_size = %d - pos_me = %d - pos_contact = %d  -",
            me->self.id,
            stage,
            nodes_array_size,
            loc_nodes_array_size,
            pos_me,
            pos_contact);

    XBT_VERB("Node %d: nodes_array", me->self.id);
    for (i = 0; i < nodes_array_size; i++) {

        XBT_VERB("nodes_array[%d] = %d",
                i,
                nodes_array[i]);
    }

    XBT_VERB("Node %d: loc_nodes_array", me->self.id);
    for (i = 0; i < loc_nodes_array_size; i++) {

        XBT_VERB("loc_nodes_array[%d] = %d",
                i,
                loc_nodes_array[i]);
    }

    if (loc_nodes_array_size > 0) {

        if (right == 0) {

            // insert new brothers at the begining (left)
            for (i = loc_nodes_array_size - 1; i >= 0; i--) {

                insert_bro(me, stage, loc_nodes_array[i]);

                res = send_msg_async(me,
                        TASK_ADD_PRED,
                        loc_nodes_array[i],
                        args);

                xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                        me->self.id,
                        debug_msg[TASK_ADD_PRED],
                        loc_nodes_array[i]);
            }
        } else {

            // add new brothers at the end (right)
            for (i = 0; i < loc_nodes_array_size; i++) {

                add_brother(me, stage, loc_nodes_array[i]);

                res = send_msg_async(me,
                        TASK_ADD_PRED,
                        loc_nodes_array[i],
                        args);

                xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                        me->self.id,
                        debug_msg[TASK_ADD_PRED],
                        loc_nodes_array[i]);
            }
        }

        /* After merge, upper stage contains two reps for the same group. One of
         * them has to be deleted */
        //clean_upper_stage(me, stage, pos_me, pos_contact);
    }

    if (loc_nodes_array != nodes_array) {

        xbt_free(loc_nodes_array);
    }

    XBT_OUT();
}

/**
 * \brief Broadcast a merge task from the same stage as the one where merges
 *        occur
 * \param me the current node
 * \param stage stage where merges take place
 * \param pos_me same as merge() (see this function)
 * \param pos_contact same as merge() (see this function)
 * \param right same as merge() (see this function)
 * \param lead_br broadcast only to leaders
 * \param new_node_id new involved coming node
 */
static void broadcast_merge(node_t me, int stage, int pos_me, int pos_contact, int right, int lead_br, int new_node_id) {

    XBT_IN();

    int i;
    u_req_args_t args;
    msg_task_t task_sent = NULL;

    // broadcast a 'merge' task
    args.broadcast.type = TASK_MERGE;
    args.broadcast.stage = stage;           // same stage as the merging one
    args.broadcast.first_call = 1;
    args.broadcast.source_id = me->self.id;
    args.broadcast.new_node_id = new_node_id;
    args.broadcast.lead_br = lead_br;       // TODO : peut-être pas utile (toujours à 0 dans cette fonction)

    args.broadcast.args = xbt_new0(u_req_args_t, 1);

    args.broadcast.args->merge.nodes_array_size = me->bro_index[stage];
    args.broadcast.args->merge.nodes_array = xbt_new0(int, me->bro_index[stage]);

    for (i = 0; i < me->bro_index[stage]; i++) {

        args.broadcast.args->merge.nodes_array[i] = me->brothers[stage][i].id;
    }

    args.broadcast.args->merge.stage = stage;
    args.broadcast.args->merge.pos_me = pos_me;
    args.broadcast.args->merge.pos_contact = pos_contact;
    args.broadcast.args->merge.right = right;
    args.broadcast.args->merge.new_node_id = new_node_id;
    make_broadcast_task(me, args, &task_sent);

    handle_task(me, &task_sent);

    xbt_free(args.broadcast.args->merge.nodes_array);
    args.broadcast.args->merge.nodes_array = NULL;

    xbt_free(args.broadcast.args);
    args.broadcast.args = NULL;

    XBT_OUT();
}

/**
 * \brief This function is run by a leaving node
 * \param me the leaving node
 */
static void leave(node_t me) {
    XBT_IN();

    int stage, brother, pred, idx;
    msg_error_t res;
    u_req_args_t args;
    int new_rep_id = 0;
    int cpt = 0;
    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());

    // prepare the array of all async messages recipients
    recp_rec_t elem = NULL;

    // make a copy of all brothers      //TODO: utile ?
    int *cpy_bro_index = NULL;
    s_node_rep_t **cpy_brothers = NULL;
    make_copy_brothers(me, &cpy_brothers, &cpy_bro_index);

    // make a copy of all predecessors  //TODO: utile ?
    int *cpy_pred_index = NULL;
    s_node_rep_t **cpy_preds = NULL;
    make_copy_preds(me, &cpy_preds, &cpy_pred_index);

    // start leaving
    for (stage = 0; stage < me->height; stage++) {

        // tell all preds I'm leaving
        for (pred = 0; pred < cpy_pred_index[stage]; pred++) {

            XBT_DEBUG("Node %d: current pred = %d",
                    me->self.id, cpy_preds[stage][pred].id);

            if (cpy_preds[stage][pred].id != me->self.id) {

                if (stage == 0) {

                    args.del_bro.stage = stage;
                    args.del_bro.bro2del = me->self.id;
                    args.del_bro.new_node_id = me->self.id,

                    res = send_msg_async(me,
                            TASK_DEL_BRO,
                            cpy_preds[stage][pred].id,
                            args);

                    xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to  %d",
                            me->self.id,
                            debug_msg[TASK_DEL_BRO],
                            cpy_preds[stage][pred].id);

                    // record the recipient
                    elem = xbt_new0(s_recp_rec_t, 1);

                    elem->type = TASK_DEL_BRO;
                    elem->br_type = TASK_NULL;
                    elem->recp = cpy_preds[stage][pred];
                    elem->new_node_id = me->self.id;
                    elem->answer_data = NULL;
                    xbt_dynar_push(proc_data->async_answers, &elem);

                    xbt_assert(elem->recp.id > - 1,
                            "Node %d: #9# recp.id is %d !!",
                            me->self.id,
                            elem->recp.id);

                    cpt++;
                } else {

                    // randomly choose a new rep to replace 'me'
                    srand(time(NULL));
                    do {
                        idx = rand() % (cpy_bro_index[0]);
                        new_rep_id = cpy_brothers[0][idx].id;
                    } while (new_rep_id == me->self.id);

                    XBT_DEBUG("Node %d: new_rep_id = %d - idx = %d - stage = %d",
                            me->self.id,
                            new_rep_id,
                            idx,
                            stage);

                    // replace predecessor
                    if (me->self.id != new_rep_id) {

                        args.repl_bro.stage = stage;
                        args.repl_bro.new_id = new_rep_id;
                        args.repl_bro.new_node_id = me->self.id;

                        XBT_DEBUG("stage = %d - new_rep_id = %d",
                                args.repl_bro.stage,
                                args.repl_bro.new_id);

                        res = send_msg_async(me,
                                TASK_REPL_BRO,
                                cpy_preds[stage][pred].id,
                                args);

                        xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                                me->self.id,
                                debug_msg[TASK_REPL_BRO],
                                cpy_preds[stage][pred].id);

                        // record the recipient
                        elem = xbt_new0(s_recp_rec_t, 1);

                        elem->type = TASK_REPL_BRO;
                        elem->br_type = TASK_NULL;
                        elem->recp = cpy_preds[stage][pred];
                        elem->new_node_id = me->self.id;
                        elem->answer_data = NULL;
                        xbt_dynar_push(proc_data->async_answers, &elem);

                        xbt_assert(elem->recp.id > - 1,
                                "Node %d: #10# recp.id is %d !!",
                                me->self.id,
                                elem->recp.id);

                        cpt++;
                    }
                }
            }
        }

        // synchro
        if (cpt > 0) {

            wait_for_completion(me, cpt, me->self.id);
        }

        cpt = 0;

        // tell all my brothers I'm leaving
        for (brother = 0; brother < cpy_bro_index[stage]; brother++) {

            if (cpy_brothers[stage][brother].id != me->self.id) {

                args.del_pred.stage = stage;
                args.del_pred.pred2del_id = me->self.id;
                args.del_pred.new_node_id = me->self.id;

                res = send_msg_async(me,
                        TASK_DEL_PRED,
                        cpy_brothers[stage][brother].id,
                        args);

                xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                        me->self.id,
                        debug_msg[TASK_DEL_PRED],
                        cpy_brothers[stage][brother].id);

                // record the recipient
                elem = xbt_new0(s_recp_rec_t, 1);

                elem->type = TASK_DEL_PRED;
                elem->br_type = TASK_NULL;
                elem->recp = cpy_brothers[stage][brother];
                elem->new_node_id = me->self.id;
                elem->answer_data = NULL;
                xbt_dynar_push(proc_data->async_answers, &elem);

                xbt_assert(elem->recp.id > - 1,
                        "Node %d: #11# recp.id is %d !!",
                        me->self.id,
                        elem->recp.id);

                cpt++;
            }
        }

        // synchro
        if (cpt > 0) {

            wait_for_completion(me, cpt, me->self.id);
        }
        cpt = 0;
    }
    //xbt_free(recp_array);

    // manage merges if necessary
    if (me->bro_index[0] <= a) {

        // choose any member other than 'me'
        idx = 0;
        while (me->brothers[0][idx].id == me->self.id && idx < me->bro_index[0]) {

            idx++;
        }

        xbt_assert(idx < me->bro_index[0],
                "Node %d: Merge index error - idx = %d - me->bro_index[0] = %d",
                me->self.id,
                idx,
                me->bro_index[0]);

        args.merge_req.new_node_id = me->self.id;
        ans_data_t answer_data = NULL;

        //TODO: Faut-il effacer args ici ?
        res = send_msg_sync(me,
                TASK_MERGE_REQ,
                me->brothers[0][idx].id,
                args,
                &answer_data);

        xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                me->self.id,
                debug_msg[TASK_MERGE_REQ],
                me->brothers[0][idx].id);

        data_ans_free(me, &answer_data);
    }

    xbt_free(cpy_bro_index);
    xbt_free(cpy_pred_index);

    for (stage = 0; stage < me->height; stage++) {

        xbt_free(cpy_brothers[stage]);
        xbt_free(cpy_preds[stage]);
    }
    xbt_free(cpy_brothers);
    xbt_free(cpy_preds);

    XBT_OUT();
}

/**
 * \brief Transfer some nodes from current group to the calling one
 *        (when merging is impossible)
 * \param me the current node
 * \param st stage where transfer takes place
 * \param right leaving side (1 if right part leaves)
 * \param cut_pos cut position (cut_pos included in the leaving part)
 * \param sender the caller of this function
 * \param new_node_id the involved leaving node
 * \return the array of extracted nodes (the leaving part)
 */
static u_ans_data_t transfer(node_t me, int st, int right, int cut_pos, s_node_rep_t sender, int new_node_id) {
    //TODO ôter l'argument sender qui ne sert plus à rien

    XBT_IN();

    int i, k, start, end, rep_idx;
    int cpt = 0;
    msg_error_t res;
    u_ans_data_t answer;
    u_req_args_t args;
    s_node_rep_t new_node;

    // to return the leaving nodes
    if (right == 1) {

        start = cut_pos;
        end = me->bro_index[st] - 1;

        xbt_assert(cut_pos > 0,
                "Node %d: cut_pos error = %d - right = %d",
                me->self.id,
                cut_pos,
                right);

        answer.transfer.stay_id = me->brothers[st][cut_pos - 1].id;
    } else {

        start = 0;
        end = cut_pos;

        xbt_assert(cut_pos < me->bro_index[st] - 1,
                "Node %d: cut_pos error = %d - right = %d",
                me->self.id,
                cut_pos,
                right);

        answer.transfer.stay_id = me->brothers[st][cut_pos + 1].id;
    }
    answer.transfer.rep_array = xbt_new0(s_node_rep_t, end - start + 1);
    answer.transfer.rep_array_size = end - start + 1;
    for (i = start; i <= end; i++) {

        answer.transfer.rep_array[i - start] = me->brothers[st][i];
    }

    // broadcast a CUT_NODE task to split all involved nodes
    XBT_VERB("Node %d: Broadcast a CUT_NODE task", me->self.id);

    args.broadcast.type = TASK_CUT_NODE;
    args.broadcast.stage = st;
    args.broadcast.first_call = 1;
    args.broadcast.source_id = me->self.id;
    args.broadcast.new_node_id = new_node_id;
    args.broadcast.lead_br = 0;

    args.broadcast.args = xbt_new0(u_req_args_t, 1);

    args.broadcast.args->cut_node.stage = st;
    args.broadcast.args->cut_node.right = right;
    args.broadcast.args->cut_node.cut_pos = cut_pos;
    args.broadcast.args->cut_node.new_node_id = new_node_id;

    msg_task_t task_sent = NULL;
    make_broadcast_task(me, args, &task_sent);
    handle_task(me, &task_sent);

    xbt_free(args.broadcast.args);
    args.broadcast.args = NULL;

    XBT_OUT();
    return answer;
}


/**
 * \brief Replace a brother by a new one
 * \param me the current node
 * \param stage the stage where to replace a brother
 * \param init_idx the brother index to be replaced
 *                 (if greater than current size then just add the new brother)
 * \param new_id the new brother id to set
 * \param new_node_id the involved leaving node
 */
static void replace_bro(node_t me, int stage, int init_idx, int new_id, int new_node_id) {

    XBT_IN();

    XBT_DEBUG("Node %d: stage = %d - init_idx = %d - new_id = %d",
            me->self.id,
            stage,
            init_idx,
            new_id);

    // nothing to do if new_id is already known at this stage
    if (me->brothers[stage][init_idx].id == new_id ||
        index_bro(me, stage, new_id) > -1) return;

    int bro_id;
    if (init_idx < me->bro_index[stage]) {

        // replace brother
        bro_id = me->brothers[stage][init_idx].id;
        me->brothers[stage][init_idx].id = new_id;
        set_mailbox(new_id, me->brothers[stage][init_idx].mailbox);
    } else {

        // add brother
        add_brother(me, stage, new_id);
    }

    u_req_args_t args;
    msg_error_t res;

    // delete predecessor
    // NOTE : si bro_id désigne le noeud qui s'en va, alors c'est inutile
    //        mais comment détecter ce cas à tous les coups ?
    //        Peut-être pas grave de le laisser
    if ((bro_id != me->self.id) && (init_idx < me->bro_index[stage])){

        args.del_pred.stage = stage;
        args.del_pred.pred2del_id = me->self.id;
        args.del_pred.new_node_id = new_node_id;

        res = send_msg_async(me,
                TASK_DEL_PRED,
                bro_id,
                args);

        xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                me->self.id,
                debug_msg[TASK_DEL_PRED],
                bro_id);
    }

    // add new predecessor
    if (new_id != me->self.id) {

        args.add_pred.stage = stage;
        args.add_pred.new_pred_id = me->self.id;
        args.add_pred.new_node_id = new_node_id;
        args.add_pred.w_ans = 0;

        res = send_msg_async(me,
                TASK_ADD_PRED,
                new_id,
                args);

        xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                me->self.id,
                debug_msg[TASK_ADD_PRED],
                new_id);
    }

    XBT_OUT();
}

//TODO : reprendre un peu le commentaire (on ne décale pas l'ensemble de
//l'étage)
/**
 * \brief Shift brothers to welcome a new one at 'me's position.
 *        The exceeded one is deleted.
 * \param me the current node
 * \param stage stage where shifting takes place
 * \param new_node the new coming node
 * \param right shift way (if 1, shift to the right)
 * \param new_node_id the involved leaving node
 */
static void shift_bro(node_t me, int stage, s_node_rep_t new_node, int right, int new_node_id) {

    XBT_IN();

    u_req_args_t args;
    msg_error_t res;
    int pos_me, i, lost_id;

    // only run once (this function is broadcasted with cut_node)
    int pos_new_node = index_bro(me, stage, new_node.id);
    if (pos_new_node > -1) {

        XBT_VERB("Node %d: new_node %d already known %s",
                me->self.id,
                new_node.id,
                routing_table(me));

        return;
    }

    pos_me = index_bro(me, stage, me->self.id);

    if (right == 1) {

        /*
        if (pos_me <= b) {

            // to update predecessors
            lost_id = me->brothers[stage][me->bro_index[stage] - 1].id;

            for (i = me->bro_index[stage] - 2; i >= pos_me; i--) {

                me->brothers[stage][i + 1] = me->brothers[stage][i];
            }
        } else {

            xbt_assert(1 == 0,
            "Node %d: ERROR : 'me' can't be deleted at stage %d in shift_bro()%s",
            me->self.id,
            stage,
            routing_table(me));
        }
        */

        if (pos_me < b) {

            // to update predecessors
            lost_id = me->brothers[stage][pos_me + 1].id;

            me->brothers[stage][pos_me + 1] = me->brothers[stage][pos_me];
        } else {

            xbt_assert(1 == 0,
            "Node %d: ERROR : 'me' can't be deleted at stage %d in shift_bro()%s",
            me->self.id,
            stage,
            routing_table(me));
        }

    } else {

        /*
        if (pos_me > 0) {

            // to update predecessors
            lost_id = me->brothers[stage][0].id;

            for (i = 1; i <= pos_me; i++) {

                me->brothers[stage][i - 1] = me->brothers[stage][i];
            }
        } else {

            xbt_assert(1 == 0,
                    "Node %d: ERROR : 'me' can't be deleted at stage %d in shift_bro()%s",
                    me->self.id,
                    stage,
                    routing_table(me));
        }
        */

        if (pos_me > 0) {

            // to update predecessors
            lost_id = me->brothers[stage][pos_me - 1].id;

            me->brothers[stage][pos_me - 1] = me->brothers[stage][pos_me];
        } else {

            xbt_assert(1 == 0,
                    "Node %d: ERROR : 'me' can't be deleted at stage %d in shift_bro()%s",
                    me->self.id,
                    stage,
                    routing_table(me));
        }
    }
    me->brothers[stage][pos_me] = new_node;

    // 'me' is now a predecessor of new_node
    args.add_pred.stage = stage;
    args.add_pred.new_pred_id = me->self.id;
    args.add_pred.new_node_id = new_node.id;
    args.add_pred.w_ans = 0;

    res = send_msg_async(me,
            TASK_ADD_PRED,
            new_node.id,
            args);

    xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
            me->self.id,
            debug_msg[TASK_ADD_PRED],
            new_node.id);

    // 'me' is no longer a predecessor of lost_id
    args.del_pred.stage = stage;
    args.del_pred.pred2del_id = me->self.id;
    args.del_pred.new_node_id = new_node_id;

    res = send_msg_async(me,
            TASK_DEL_PRED,
            lost_id,
            args);

    xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
            me->self.id,
            debug_msg[TASK_DEL_PRED],
            lost_id);

    XBT_VERB("Node %d: end of shift_bro() - %s",
            me->self.id,
            routing_table(me));

    XBT_OUT();
}

/**
 * \brief Delete the root stage (when a merging requires it)
 * \param me the current node
 * \param init_height initial height to prevent this function from executing
 *                    more thant once
 */
static void del_root(node_t me, int init_height) {

    XBT_IN();

    // only run this function once
    if (me->height == init_height) {

        xbt_free(me->brothers[me->height - 1]);
        me->brothers[me->height - 1] = NULL;
        xbt_free(me->preds[me->height - 1]);
        me->preds[me->height - 1] = NULL;

        me->height--;

        me->bro_index = realloc(me->bro_index, me->height * sizeof(int));
        me->pred_index = realloc(me->pred_index, me->height * sizeof(int));

        me->brothers = realloc(me->brothers, me->height * sizeof(node_rep_t));
        me->preds = realloc(me->preds, me->height * sizeof(node_rep_t));

        xbt_assert((me->preds != NULL) && (me->pred_index != NULL),
                "Node %d: Can't delete root from predecessors table - height = %d",
                me->self.id,
                me->height);
        xbt_assert((me->brothers != NULL) && (me->bro_index != NULL),
                "Node %d: Can't delete root from routing table - height = %d",
                me->self.id,
                me->height);
    }
    XBT_OUT();
}

/**
 * \brief When a merge occured in current stage, upper stage contains two reps
 * for the same group. Deletes one of them.
 * \param me the current node
 * \param stage the current stage
 * \param pos_me upper stage position of source node in its table
 * \param pos_contact upper stage position of target node in source node's table
 * \param new_node_id the involved leaving node
 */
static void clean_upper_stage(node_t me, int stage, int pos_me, int pos_contact, int new_node_id) {

    XBT_IN();

    u_req_args_t args;
    msg_error_t res;

    // computes recp_id : the node to be deleted
    int recp_id = -1;

    XBT_DEBUG("Node %d: pos_me = %d - pos_contact = %d - stage = %d",
            me->self.id,
            pos_me,
            pos_contact,
            stage);

    // the rep to be deleted is the one that juste came ...
    recp_id = me->brothers[stage + 1][pos_me].id;

    // ... unless it's 'me', in which case 'me' and 'contact' have to be swapped
    if (recp_id == me->self.id) {

        XBT_DEBUG("Node %d: swap %d and %d - recp_id = %d - %s",
                me->self.id,
                me->brothers[stage + 1][pos_me].id,
                me->brothers[stage + 1][pos_contact].id,
                recp_id,
                routing_table(me));

        // swaps reps
        s_node_rep_t buf = me->brothers[stage + 1][pos_me];
        me->brothers[stage + 1][pos_me] = me->brothers[stage + 1][pos_contact];
        me->brothers[stage + 1][pos_contact] = buf;

        recp_id = me->brothers[stage + 1][pos_me].id;

        XBT_DEBUG("Node %d: rep swapped - recp_id = %d - %s",
                me->self.id,
                recp_id,
                routing_table(me));
    }

    /*
    xbt_assert(recp_id > -1,
            "Node %d: recp_id compute error "
            "- pos_me = %d, pos_contact = %d, stage = %d - %s",
            me->self.id,
            pos_me,
            pos_contact,
            stage,
            routing_table(me));
    */

    if (recp_id > -1) {

        XBT_DEBUG("Node %d: recp_id = %d",
                me->self.id,
                recp_id);

        // delete rep
        del_bro(me, stage + 1, recp_id);

        // delete predecessor
        args.del_pred.stage = stage + 1;
        args.del_pred.pred2del_id = me->self.id;
        args.del_pred.new_node_id = new_node_id;

        res = send_msg_async(me,
                TASK_DEL_PRED,
                recp_id,
                args);

        xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                me->self.id,
                debug_msg[TASK_DEL_PRED],
                recp_id);
    } else {

        XBT_VERB("Node %d: clean_upper_stage already run%s",
                me->self.id,
                routing_table(me));
    }

    display_rout_table(me, 'V');
    XBT_OUT();
}

/**
 * \brief Manage merges when a node leaves the DST
 *        (has to be run by one of leaving node's brothers)
 * \param me the current node
 * \param new_node_id new involved leaving node
 */
static void merge_request(node_t me, int new_node_id) {

    XBT_IN();

    // manage merges
    int size_last_stage = 0;
    int pos_me, pos_contact;
    int stage = 0;
    int i = 0;
    msg_error_t res;
    u_req_args_t args;
    node_rep_t current_bro = NULL;
    int current_size = 0;
    int transfer = 0;

    ans_data_t answer_data = NULL;
    ans_data_t ans_data_2 = NULL;

    while ((me->bro_index[stage] < a) && (stage < me->height - 1)) {

        XBT_VERB("Node %d: bro_index[%d] = %d - stage = %d",
                me->self.id,
                stage,
                me->bro_index[stage],
                stage);

        pos_contact = merge_or_transfer(me, stage);
        if (pos_contact > - 1) {

            // Merge
            transfer = 0;
            pos_me = index_bro(me, stage + 1, me->self.id);

            // send a 'merge' task to contact
            args.merge.nodes_array = xbt_new0(int, me->bro_index[stage]);
            args.merge.nodes_array_size = me->bro_index[stage];

            for (i = 0; i < me->bro_index[stage]; i++) {

                args.merge.nodes_array[i] = me->brothers[stage][i].id;
            }
            args.merge.stage = stage;
            args.merge.pos_me = pos_me;
            args.merge.pos_contact = pos_contact;
            //args.merge.right = (pos_me > pos_contact ? 11 : 10);
            args.merge.right = (pos_me > pos_contact ? 1 : 0);
            args.merge.new_node_id = new_node_id;

            XBT_DEBUG("Node %d: Merge Request: send TASK_MERGE to %d - "
                    "pos_contact = %d - pos_me = %d - current stage = %d",
                    me->self.id,
                    me->brothers[stage + 1][pos_contact].id,
                    pos_contact,
                    pos_me,
                    stage);

            answer_data = NULL;
            res = send_msg_sync(me,
                    TASK_MERGE,
                    me->brothers[stage + 1][pos_contact].id,
                    args,
                    &answer_data);

            xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                    me->self.id,
                    debug_msg[TASK_MERGE],
                    me->brothers[stage + 1][pos_contact].id);

            data_ans_free(me, &answer_data);

            // send a 'broadcast_merge' task to contact
            args.broad_merge.stage = stage;
            args.broad_merge.pos_me = pos_me;
            args.broad_merge.pos_contact = pos_contact;
            args.broad_merge.right = (pos_me > pos_contact ? 1 : 0);
            args.broad_merge.lead_br = 0;
            args.broad_merge.new_node_id = new_node_id;

            res = send_msg_sync(me,
                    TASK_BROADCAST_MERGE,
                    me->brothers[stage + 1][pos_contact].id,
                    args,
                    &answer_data);

            xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                    me->self.id,
                    debug_msg[TASK_BROADCAST_MERGE],
                    me->brothers[stage + 1][pos_contact].id);

            data_ans_free(me, &answer_data);

            /* After merge, upper stage contains two reps for the same group. One of
             * them has to be deleted */

            // local call
            clean_upper_stage(me, stage, pos_me, pos_contact, new_node_id);

            xbt_assert(stage < me->height,
                    "Node %d: clean stage error - stage = %d - height = %d",
                    me->self.id,
                    stage,
                    me->height);

            // broadcast clean_upper_stage
            args.broadcast.type = TASK_CLEAN_STAGE;

            args.broadcast.stage = stage + 1;
            args.broadcast.first_call = 1;
            args.broadcast.source_id = me->self.id;
            args.broadcast.new_node_id = new_node_id;
            args.broadcast.lead_br = 0;

            args.broadcast.args = xbt_new0(u_req_args_t, 1);

            args.broadcast.args->clean_stage.stage = stage;
            args.broadcast.args->clean_stage.pos_me = pos_me;
            args.broadcast.args->clean_stage.pos_contact = pos_contact;
            args.broadcast.args->clean_stage.new_node_id = new_node_id;

            msg_task_t task_sent = NULL;
            make_broadcast_task(me, args, &task_sent);
            handle_task(me, &task_sent);

            xbt_free(args.broadcast.args);
            args.broadcast.args = NULL;

            data_ans_free(me, &answer_data);  // TODO : tester avec et sans
        } else {

            //Transfer
            XBT_VERB("Node %d: Do a transfer rather than a merge", me->self.id);
            transfer = 1;
            int cut_pos, right;
            int pos_me_up = index_bro(me, stage + 1, me->self.id);

            // contact should be the neighbour
            if (pos_me_up == 0) {

                pos_contact = 1;
                right = 0;
                cut_pos = b - a - 1;
            } else {

                pos_contact = pos_me_up - 1;
                right = 1;
                cut_pos = a;
            }

            int contact_id = me->brothers[stage + 1][pos_contact].id;

            XBT_VERB("Node %d: pos_contact = %d - contact_id = %d - stage = %d",
                    me->self.id,
                    pos_contact,
                    contact_id,
                    stage);

            args.transfer.st = stage;
            args.transfer.right = right;
            args.transfer.cut_pos = cut_pos;
            args.transfer.sender = me->self;
            args.transfer.new_node_id = new_node_id;

            answer_data = NULL;
            res = send_msg_sync(me,
                    TASK_TRANSFER,
                    contact_id,
                    args,
                    &answer_data);

            XBT_VERB("Node %d: back to merge_request()",
                    me->self.id);

            XBT_VERB("Node %d: Reps received from transfer() :",
                      me->self.id);

            for (i = 0; i < answer_data->answer.transfer.rep_array_size; i++) {

                      XBT_VERB("rep[%d] = %d",
                      i,
                      answer_data->answer.transfer.rep_array[i].id);
            }

            /** append received nodes **/

            // records current stage before it is modified
            current_bro = xbt_new0(s_node_rep_t, me->bro_index[stage]);
            current_size = me->bro_index[stage];
            for (i = 0; i < current_size; i++) {

                current_bro[i] = me->brothers[stage][i];
            }

            // every member of current branch append received nodes
            XBT_VERB("Node %d : Local ADD_BRO_ARRAY broadcast",
                    me->self.id);

            br_add_bro_array(me,
                    stage,
                    answer_data->answer.transfer.rep_array,
                    answer_data->answer.transfer.rep_array_size,
                    (right + 1) % 2,
                    new_node_id);

            // every member of neighbour branch append current nodes
            XBT_VERB("Node %d : Ask %d to broadcast an ADD_BRO_ARRAY task",
                    me->self.id,
                    answer_data->answer.transfer.rep_array[0].id);

            args.br_add_bro_array.stage = stage;
            args.br_add_bro_array.bro = current_bro;
            args.br_add_bro_array.array_size = current_size;
            args.br_add_bro_array.right = right;
            args.br_add_bro_array.new_node_id = new_node_id;

            res = send_msg_sync(me,
                    TASK_BR_ADD_BRO_ARRAY,
                    answer_data->answer.transfer.rep_array[0].id,
                    args,
                    &ans_data_2);        // TODO: vérifier l'utilité de ans_data_2

            xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                    me->self.id,
                    debug_msg[TASK_BR_ADD_BRO_ARRAY],
                    answer_data->answer.transfer.rep_array[0].id);

            data_ans_free(me, &ans_data_2);

            /* local upper stage update (so that the following broadcast behaves
               correctly) */
            XBT_VERB("Node %d: Local UPDATE_UPPER_STAGE - stage = %d - right = %d - pos2repl = %d",
                    me->self.id,
                    stage,
                    right,
                    pos_contact);
            /*
                    answer_data->answer.transfer.rep_array_size - 1,
                    me->bro_index[stage] - answer_data->answer.transfer.rep_array_size);
            */

            update_upper_stage(me, stage, pos_contact, answer_data->answer.transfer.stay_id, new_node_id);

            // update upper stage for all concerned members
            msg_task_t task_sent = NULL;

            XBT_VERB("Node %d: Broadcast UPDATE_UPPER_STAGE ",
                    me->self.id);

            /*
            pos2repl = (right == 0 ?
                    answer_data->answer.transfer.rep_array_size - 1 :
                    me->bro_index[stage] - answer_data->answer.transfer.rep_array_size);
            */

            args.broadcast.type = TASK_UPDATE_UPPER_STAGE;
            args.broadcast.stage = stage + 1;
            args.broadcast.first_call = 1;
            args.broadcast.source_id = me->self.id;
            args.broadcast.new_node_id = new_node_id;
            args.broadcast.lead_br = 0;

            args.broadcast.args = xbt_new0(u_req_args_t, 1);

            args.broadcast.args->update_upper_stage.stage = stage;
            args.broadcast.args->update_upper_stage.pos2repl = pos_contact;
            args.broadcast.args->update_upper_stage.new_id = answer_data->answer.transfer.stay_id;
            args.broadcast.args->update_upper_stage.new_node_id = new_node_id;

            make_broadcast_task(me, args, &task_sent);
            handle_task(me, &task_sent);

            xbt_free(args.broadcast.args);
            args.broadcast.args = NULL;

            XBT_VERB("Node %d : End of ADD_BRO_ARRAY", me->self.id);

            xbt_free(current_bro);
            xbt_free(answer_data->answer.transfer.rep_array);
        }

        stage++;
    }

    XBT_VERB("Node %d: *** END OF MERGE-OR-TRANSFER ***",
            me->self.id);

    // get current root size
    i = 0;
    while (me->brothers[0][i].id == me->self.id) { i++; }

    xbt_assert(i < me->bro_index[0], "Node %d : get current root size error", me->self.id);

    XBT_VERB("Node %d: current bro = %d - i = %d",
            me->self.id,
            me->brothers[0][i].id,
            i);

    data_ans_free(me, &answer_data);
    //answer_data = NULL;

    args.get_size.stage = me->height - 1;

    display_rout_table(me, 'V');

    res = send_msg_sync(me,
            TASK_GET_SIZE,
            me->brothers[0][i].id,
            args,
            &answer_data);

    xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
            me->self.id,
            debug_msg[TASK_GET_SIZE],
            me->brothers[0][i].id);

    size_last_stage = answer_data->answer.get_size.size;

    XBT_DEBUG("Node %d: Merge Request: root size = %d",
            me->self.id,
            size_last_stage);

    data_ans_free(me, &answer_data);

    /*
    // if root stays, broadcast a clean_stage task
    XBT_DEBUG("Node %d : brodcast clean_stage ? - stage = %d - size_root = %d",
            me->self.id,
            stage,
            size_last_stage);
    */

    //TODO : voir ce qu'il se passe si des fusions et des transferts
    //TODO : utile ?

    /*
    if (size_last_stage > 1 && transfer == 0) { // && stage >= me->height - 2)

        args.broadcast.type = TASK_CLEAN_STAGE;

        */
        /* starts broadcast at current stage which is one stage upper the last
         * one where merge occured */
        /*
        args.broadcast.stage = stage;
        args.broadcast.first_call = 1;
        args.broadcast.source_id = me->self.id;

        args.broadcast.args = xbt_new0(u_req_args_t, 1);

        // the given stage here has to be the last one where merge occured
        args.broadcast.args->clean_stage.stage = stage - 1;
        args.broadcast.args->clean_stage.pos_me = pos_me;
        args.broadcast.args->clean_stage.pos_contact = pos_contact;

        msg_task_t task_sent = NULL;
        make_broadcast_task(me, args, &task_sent);
        handle_task(me, &task_sent);
        task_free(&task_sent);

        xbt_free(args.broadcast.args);

        //data_ans_free(me, &answer_data);
    }
    */

    // delete root stage
    if (size_last_stage == 1) {

        msg_task_t task_sent = NULL;

        args.broadcast.type = TASK_DEL_ROOT;
        args.broadcast.stage = me->height - 1;
        args.broadcast.first_call = 1;
        args.broadcast.source_id = me->self.id;
        args.broadcast.new_node_id = new_node_id;
        args.broadcast.lead_br = 0;

        args.broadcast.args = xbt_new0(u_req_args_t, 1);
        args.broadcast.args->del_root.init_height = me->height;
        args.broadcast.args->del_root.new_node_id = new_node_id;
        make_broadcast_task(me, args, &task_sent);

        handle_task(me, &task_sent);

        xbt_free(args.broadcast.args);
    }

    args.broadcast.args = NULL;

    XBT_OUT();
}

/**
 * \brief Function run by new node to balance load
 * \param me current node
 * \param contact_id the node that provided current routing table
 */
static void load_balance(node_t me, int contact_id) {

    XBT_IN();

    int idx = 0;
    int i, j, k, brother;
    node_rep_t new_nodes = NULL;
    int *get_rep_nodes = NULL;

    // to memorize nodes that will be asked to provide a new rep
    get_rep_nodes = xbt_new0(int, (me->height - 1) * b);
    k = 0;

    // load_balance state
    state_t *state_ptr = NULL;
    state_ptr = xbt_dynar_get_ptr(me->states,
            xbt_dynar_length(me->states) - 1);
    if ((*state_ptr)->active == 'b') {

        (*state_ptr)->active = 'l';
        (*state_ptr)->new_id = me->self.id;
    }
    s_state_t state = get_state(me);
    XBT_INFO("Node %d: '%c'/%d - **** START LOAD BALANCE ****",
            me->self.id,
            state.active,
            state.new_id);

    u_req_args_t u_req_args;
    msg_error_t res;

    // Add me as a pred for the whole table
    // (states 'p' have been sent at the end of connection_request())
    u_req_args.add_pred.new_pred_id = me->self.id;
    u_req_args.add_pred.new_node_id = me->self.id;
    u_req_args.add_pred.w_ans = 0;

    for (i = 1; i < me->height; i++) {
        for (j = 0; j < me->bro_index[i]; j++) {

            // don't send to contact since it'll be replaced by me in current
            // table
            if (me->brothers[i][j].id != contact_id) {

                u_req_args.add_pred.stage = i;
                res = send_msg_async(me,
                        TASK_ADD_PRED,
                        me->brothers[i][j].id,
                        u_req_args);

            }
        }
    }

    //TODO : faut-il mettre un wait for completion, ici ?

    // browse the whole routing table to find other representatives
    for (i = 1; i < me->height; i++) {

        new_nodes = xbt_new0(s_node_rep_t, b);
        for (j = 0; j < me->bro_index[i]; j++) {

            /* replace contact_id by me (in new_nodes array and in current
               routing table) */
            if (me->brothers[i][j].id == contact_id) {

                new_nodes[idx] = me->self;
                idx++;

                me->brothers[i][j] = me->self;
                add_pred(me, i, me->self.id);

                /*
                u_req_args.del_pred.stage = i;
                u_req_args.del_pred.pred2del_id = me->self.id;
                u_req_args.del_pred.new_node_id = me->self.id;

                res = send_msg_async(me,
                        TASK_DEL_PRED,
                        contact_id,
                        u_req_args);
                        */

                XBT_DEBUG("Node %d: contact %d replaced by me",
                        me->self.id,
                        contact_id);

            } else {

                /* me->self.id can be found if an ADD_STAGE occured during load
                   balance */
                if (me->brothers[i][j].id != me->self.id) {

                    // send a "Get Representative" request to current representative
                    ans_data_t answer_data = NULL;
                    u_req_args.get_rep.stage = i;
                    u_req_args.get_rep.new_node_id = me->self.id;

                    res = send_msg_sync(me,
                            TASK_GET_REP,
                            me->brothers[i][j].id,
                            u_req_args,
                            &answer_data);

                    res = MSG_OK;

                    // memorize recipient
                    get_rep_nodes[k] = me->brothers[i][j].id;
                    k++;

                    /* answer_data may be NULL if GET_REP didn't get any answer
                       before a certain time */
                    if (res != MSG_OK || answer_data == NULL) {

                        // get_rep failure
                        XBT_DEBUG("Node %d: failed to get a new representative for"
                                  " stage %d, brother %d. Nevermind",
                                  me->self.id, i, me->brothers[i][j].id);

                        // if current rep doesn't answer, keep it
                        new_nodes[idx] = me->brothers[i][j];
                    } else {

                        // get_rep success
                        new_nodes[idx] = answer_data->answer.get_rep.new_rep;

                    }
                    data_ans_free(me, &answer_data);

                    /* tells the new rep he's got a new predecessor: me */
                    if (new_nodes[idx].id != me->brothers[i][j].id) {

                        // important : add_pred before del_pred so that me is always the predecessor of a node
                        u_req_args.add_pred.stage = i;
                        u_req_args.add_pred.new_pred_id = me->self.id;
                        u_req_args.add_pred.new_node_id = me->self.id;
                        u_req_args.add_pred.w_ans = 1;

                        // synchro (6)
                        res = send_msg_sync(me,
                                TASK_ADD_PRED,
                                new_nodes[idx].id,
                                u_req_args,
                                &answer_data);

                        // add pred failure
                        xbt_assert(res == MSG_OK,
                                "Node %d: failed to add me as a new predecessor for"
                                " stage %d to node %d",
                                me->self.id,
                                i,
                                new_nodes[idx].id);

                        u_req_args.del_pred.stage = i;
                        u_req_args.del_pred.pred2del_id = me->self.id;
                        u_req_args.del_pred.new_node_id = me->self.id;

                        res = send_msg_async(me,
                                TASK_DEL_PRED,
                                me->brothers[i][j].id,
                                u_req_args);

                        data_ans_free(me, &answer_data);
                    }

                    idx++;

                } else {

                    //if me is found, keep it
                    new_nodes[idx] = me->brothers[i][j];
                    idx++;
                }
            }
        }               // next brother

        // new_nodes replaces current stage brothers
        XBT_DEBUG("Node %d: GET_REP stage %d - new_nodes",
                me->self.id,
                i);
        for (brother = 0; brother < b; brother++) {

            if (brother < idx) {

                XBT_DEBUG("brothers[%d][%d] = %d <-- new_nodes[%d] = %d",
                        i,
                        brother,
                        me->brothers[i][brother].id,
                        brother,
                        new_nodes[brother].id);
            }

            me->brothers[i][brother] = new_nodes[brother];
        }

        XBT_DEBUG("Node %d: new_nodes inserted", me->self.id);

        me->bro_index[i] = idx;
        idx = 0;

        xbt_free(new_nodes);
        new_nodes = NULL;
    }                   // next stage

    // remove 'g' state of asked nodes
    /*
    for (i = 0; i < k; i++) {

        u_req_args.end_get_rep.new_node_id = me->self.id;

        res = send_msg_async(me,
                TASK_END_GET_REP,
                get_rep_nodes[i],
                u_req_args);

        // end_get_rep failure
        xbt_assert(res == MSG_OK,
                "Node %d: failed to send TASK_END_GET_REP to node %d",
                me->self.id,
                get_rep_nodes[i]);
    }
    */

    xbt_free(get_rep_nodes);

    // change node state
    state = get_state(me);
    set_active(me, state.new_id);

    state = get_state(me);
    XBT_INFO("Node %d: '%c'/%d  **** LOAD BALANCE COMPLETED ****",
            me->self.id,
            state.active,
            state.new_id);
    XBT_OUT();
}

/**
 * \brief Provide a new contact for a coming node that failed to join too much
 * \param me the current node
 * \param new_node_id the new coming node
 */
static u_ans_data_t get_new_contact(node_t me, int new_node_id) {

    XBT_IN();

    u_ans_data_t answer;
    int idx = 0;

    s_state_t state = get_state(me);
    int stage = me->height - 1;
    int pos_me = index_bro(me, stage, me->self.id);

    xbt_assert(pos_me > -1 || state.active != 'a',
            "Node %d: is not found at stage %d !!",
            me->self.id,
            stage);

    if (me->bro_index[stage] > 1) {

        srand(time(NULL));
        do {
            idx = rand() % (me->bro_index[stage]);
        } while (idx == pos_me);

        XBT_VERB("Node %d: new contact will be node %d",
                me->self.id,
                me->brothers[stage][idx].id);

    } else {

        idx = pos_me;
        XBT_VERB("Node %d: can't provide a new contact", me->self.id);
    }

    answer.get_new_contact.id = me->brothers[stage][idx].id;

    XBT_OUT();
    return answer;
}

/**
 * \brief Node function
 * Arguments:
 * - self id
 * - id of another node known in the system (except for the first node)
 * - time to sleep before joining (except for the first node)
 * - deadline
 *   (see xml deployment files)
 */
int node(int argc, char *argv[]) {

    XBT_IN();
    XBT_VERB("Node %d: node() ...", atoi(argv[1]));

    xbt_assert(argc == 3 || argc == 5,
            "Node %d: Wrong number of arguments for this node",
            atoi(argv[1]));

    //double init_time = MSG_get_clock();

    // self init
    s_node_t node;
    node.self.id = atoi(argv[1]);

    int join_success = 1;       // success by default
    /*
    int tries = 0;
    int tries_mem = TRY_STEP;
    */

    init(&node);

    if (argc == 5) {            // all nodes but first one need to join

        double sleep_time = atof(argv[3]);
        node.prio = atoi(argv[3]);       // sleep time can be used to set priority
        node.deadline = atof(argv[4]);
        int contact_id = atoi(argv[2]);
        u_req_args_t u_req_args;
        u_req_args.get_new_contact.new_node_id = node.self.id;
        ans_data_t answer_data = NULL;
        msg_error_t res;
        int index = 0;

        do {

            // sleep before first attempt to join
            if (MSG_get_clock() < sleep_time) {

                XBT_INFO("Node %d: Let's sleep during %f",
                        node.self.id,
                        sleep_time);

                MSG_process_sleep(sleep_time);
            }

            XBT_INFO("Node %d: **** START JOINING DST *** (attempt number %d)",
                    node.self.id,
                    node.dst_infos.attempts + 1);

            XBT_INFO("Node %d: Let's join the system ! (via contact %d) -"
                    " deadline = %f",
                    node.self.id,
                    contact_id,
                    node.deadline);

            // set the dst infos for the current node
            if (node.dst_infos.order == 0) {

                // only one order number per node
                node.dst_infos.order = order++;
            }

            XBT_VERB("Node %d: order = %d",
                    node.self.id,
                    node.dst_infos.order);

            join_success = join(&node, contact_id);

            if (!join_success) {

                XBT_INFO("Node %d: [%s:%d] Join failure !",
                        node.self.id,
                        __FUNCTION__,
                        __LINE__);

                // randomly choose another contact
                srand(time(NULL));
                index = (double)rand() * (double)(nb_ins_nodes) / (double)RAND_MAX;

                xbt_assert(index < nb_ins_nodes, "index = %d - nb_ins_nodes = %d", index, nb_ins_nodes);

                contact_id = inserted_nodes[index];

                // counts
                node.dst_infos.nb_chg_contact++;

                XBT_INFO("Node %d: [%s:%d] Try with another contact : %d",
                        node.self.id,
                        __FUNCTION__,
                        __LINE__,
                        contact_id);
            } else {

                //xbt_assert(node.self.id != 1521, "Node %d", node.self.id);
            }
        } while (!join_success);
    } else {

        // first node
        order = 0;
        node.dst_infos.order = order++;
        node.deadline = atoi(argv[2]);
    }

    if (join_success) {

        XBT_INFO("Node %d: **** JOIN COMPLETED ****", node.self.id);

        // record inserted node id in global array
        inserted_nodes[nb_ins_nodes] = node.self.id;
        nb_ins_nodes++;

        //display inserted_nodes
        int iter = nb_ins_nodes - 10;
        if (iter < 0) iter = 0;
        XBT_VERB("Node %d: last ten inserted nodes :", node.self.id);
        for (; iter < nb_ins_nodes; iter++) {
            XBT_VERB("\tinserted_nodes[%d] = %d", iter, inserted_nodes[iter]);
        }

        // active state
        set_active(&node, node.self.id);
        set_n_store_infos(&node);

        XBT_DEBUG("Node %d: add stage = %d",
                node.self.id,
                node.dst_infos.add_stage);

        // run remaining tasks, if any
        //call_run_delayed_tasks(&node, node.self.id);
        //run_delayed_tasks(&node);

        // task receive loop
        msg_task_t task_received = NULL;
        msg_error_t res = MSG_TRANSFER_FAILURE;
        s_state_t state = get_state(&node);
        int nb_proc = 0;
        int done = 0;
        xbt_swag_t proc_set = NULL;
        msg_process_t elem = NULL;
        float wait = 0.0;

        //while (MSG_get_clock() < max_simulation_time && state.active != 'n') {
        do {

            state = get_state(&node);
            //if (state.active == 'n') break;

            proc_set = MSG_host_get_process_list(MSG_host_self());
            nb_proc = xbt_swag_size(proc_set);

            //if (nb_proc == 1) XBT_VERB("NB_PROC = 1");

            if (done == 0) {
                if (nb_proc > 1 || nb_ins_nodes < nb_nodes) {

                    done = 0;
                } else {

                    xbt_swag_foreach(elem, proc_set) {
                        XBT_DEBUG("swag elem = '%s'", MSG_process_get_name(elem));
                    }
                    XBT_DEBUG("Node %d: DONE !", node.self.id);
                    done = 1;
                    wait = MSG_get_clock() + WAIT_BEFORE_END;
                }
            }

            // prepare a new 'listening' phase
            if (node.comm_received == NULL) {

                task_received = NULL;
                node.comm_received = MSG_task_irecv(&task_received, node.self.mailbox);

                XBT_VERB("Node %d: [%s:%d] Waiting for a task...", node.self.id, __FUNCTION__, __LINE__);
            }

            //XBT_DEBUG("Node %d: comm test", node.self.id);

            if (!MSG_comm_test(node.comm_received)) {

                // no task was received: make some periodic calls
                state = get_state(&node);

                // set log details
                if ((nb_ins_nodes >= 0.95 * nb_nodes) && (mem_log == -1)) {

                    //xbt_assert(1 == 0, "nb_ins_nodes = %d, nb_nodes = %d", nb_ins_nodes, nb_nodes);
                    xbt_log_control_set("msg_dst.thres:verbose");
                    XBT_VERB("Node %d: Log Verbose - nb_ins_nodes = %d, nb_nodes = %d",
                            node.self.id, nb_ins_nodes, nb_nodes);
                    mem_log++;
                }

                // TODO: remplacer les get_clock() par une variable ?
                if (MSG_get_clock() >= node.deadline && state.active == 'a' && 1 == 0) {    //TODO : ne pas oublier

                    XBT_INFO("Node %d: deadline reached: time to leave !",
                            node.self.id);
                    state_t *state_ptr = NULL;
                    state_ptr = xbt_dynar_get_ptr(node.states, xbt_dynar_length(node.states) - 1);
                    (*state_ptr)->active = 'n';      // non active state
                    leave(&node);
                    set_n_store_infos(&node);
                    XBT_INFO("Node %d left ...", node.self.id);
                } else {

                    if (node.cs_req == 1 &&
                        MSG_get_clock() - node.cs_req_time >= MAX_CS_REQ &&
                        MSG_get_clock() - node.last_check_time >= MAX_CS_REQ) {

                        // checks if cs_req has to be reset (avoids deadlocks)
                        XBT_VERB("Node %d: asks node %d if cs_req has to be reset",
                                node.self.id,
                                node.cs_new_id);
                        display_sc(&node, 'V');

                        node.last_check_time = MSG_get_clock();

                        u_req_args_t args_chk;
                        args_chk.check_cs.new_node_id = node.cs_new_id; // not used

                        xbt_assert(node.cs_new_id != node.self.id, "Check_cs error !!");

                        msg_error_t res = send_msg_async(&node,
                                TASK_CHECK_CS,
                                node.cs_new_id,
                                args_chk);
                    }

                    //XBT_DEBUG("Node %d: nothing else to do: sleep for a while", node.self.id);

                    // simulation time will increase a lot if sleep time is too long
                    MSG_process_sleep(0.2);

                    //XBT_DEBUG("Node %d: wake up - comm = %p", node.self.id, node.comm_received);
                }
            } else {

                // some task has been received

                XBT_DEBUG("some task has been received");
                res = MSG_comm_get_status(node.comm_received);

                MSG_comm_destroy(node.comm_received);
                node.comm_received = NULL;

                XBT_VERB("Node %d: Task received", node.self.id);       //TODO : ajouter l'id de l'émetteur du message
                display_sc(&node, 'V');

                if (res == MSG_OK) {

                    req_data_t req = MSG_task_get_data(task_received);
                    ans_data_t ans = (ans_data_t)req;

                    if (strcmp(MSG_task_get_name(task_received), "ans") != 0) {

                        // Received request
                        // push the received CNX_REQ task onto the dynar ...
                        if (req->type == TASK_CNX_REQ) {

                            xbt_dynar_push(node.tasks_queue, &task_received);
                            task_received = NULL;

                            XBT_VERB("Node %d: task %s(%d) pushed",
                                    node.self.id,
                                    debug_msg[req->type],
                                    req->args.cnx_req.new_node_id);

                        } else {

                            // ... otherwise, handle it with a fork process
                            launch_fork_process(&node, task_received);
                        }
                    } else {

                        // ignore answers, only process requests
                        if (ans != NULL){

                            XBT_VERB("Node %d: ignore answer from %d", node.self.id, ans->sender_id);
                            xbt_free(ans);
                        }
                        task_free(&task_received);
                    }
                } else {

                    // reception failure
                    XBT_WARN("Node %d: Failed to receive a task",
                            node.self.id);
                }
            }
            state = get_state(&node);
        } while (done == 0 || MSG_get_clock() < wait);

        XBT_DEBUG("Node %d: End While loop - will quit node() - nb_ins_nodes = %d", node.self.id, nb_ins_nodes);

    } else {

        XBT_INFO("Node %d: **** JOIN ABORT ... ****", node.self.id);

        // self state must be 'n' since its joining failed
        state_t *state_ptr = NULL;
        state_ptr = xbt_dynar_get_ptr(node.states,
                xbt_dynar_length(node.states) - 1);
        (*state_ptr)->active = 'n';

        // count failed nodes
        failed_nodes[nb_abort].id = node.self.id;
        failed_nodes[nb_abort].f_time = MSG_get_clock();
        nb_abort++;
        failed_nodes =
            (s_f_node_t*)realloc(failed_nodes, (nb_abort + 1) * sizeof(s_f_node_t));

        set_n_store_infos(&node);
    }

    //MSG_process_sleep(1000.0);          //TODO : attente que les autres aient terminé. Peut mieux faire ?
    node_free(&node);
    XBT_OUT();
    return (!join_success);
}

/**
 * \brief This function is called by the current node when it receives a task
 * \param me the current node
 * \param task pointer to the task to be handled
 * \return A value indicating if things went right or not
 */
static e_val_ret_t handle_task(node_t me, msg_task_t* task) {

    XBT_IN();

    xbt_assert(task != NULL, "Node %d: task must not be NULL !", me->self.id);

    e_val_ret_t val_ret = OK;

    // don't handle answers
    if (!strcmp(MSG_task_get_name(*task), "ans")) {

        XBT_DEBUG("Node %d: only requests accepted in handle_task !",
                me->self.id);

        ans_data_t rcv = (ans_data_t) MSG_task_get_data(*task);
        data_ans_free(me, &rcv);
        task_free(task);

        return val_ret;
    }

    s_state_t state = get_state(me);
    req_data_t rcv_req = (req_data_t) MSG_task_get_data(*task);
    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());
    e_task_type_t type = rcv_req->type;
    char is_contact = 0;
    char is_leader = 0;

    XBT_VERB("Node %d: [%s:%d] '%c'/%d: Handling task '%s - %s' received from node %d - answer to '%s'",
            me->self.id,
            __FUNCTION__,
            __LINE__,
            state.active,
            state.new_id,
            MSG_task_get_name(*task),
            debug_msg[type],
            rcv_req->sender_id,
            rcv_req->answer_to);

    XBT_DEBUG("task : %p", *task);

    u_req_args_t rcv_args;
    if (rcv_req != NULL) {

        rcv_args = rcv_req->args;
    }
    u_ans_data_t answer;
    msg_error_t res = MSG_OK;
    int init_idx, idx;
    int test = 0;

    switch (type) {

        case TASK_NULL:
            break;

        case TASK_GET_REP:
            if (state.active == 'b') {

                // store task (GET_REP isn't broadcasted)
                XBT_VERB("Node %d - active = '%c': store task for later"
                        " execution",
                        me->self.id,
                        state.active);

                xbt_dynar_push(me->delayed_tasks, task);
                //xbt_dynar_push(me->tasks_queue, task);
                *task = NULL;
                val_ret = STORED;
            } else {

                // run task now
                answer = get_rep(me, rcv_args.get_rep.stage, rcv_args.get_rep.new_node_id);
                res = send_ans_sync(me,
                        rcv_args.get_rep.new_node_id,
                        type,
                        rcv_req->sender_id,
                        rcv_req->answer_to,
                        answer);

                data_req_free(me, &rcv_req);
                task_free(task);

                XBT_VERB("Node %d: TASK_GET_REP done", me->self.id);
            }
            break;

        case TASK_CNX_REQ:

            // CNX_REQ has to be run by a fork process. If it's not the case, push it on tasks_queue
            if (strstr(MSG_process_get_name(MSG_process_self()), "Cnx") != NULL) {

                is_contact = (rcv_req->sender_id == rcv_req->args.cnx_req.new_node_id);
                is_leader = me->self.id == me->brothers[0][0].id;

                /*
                   if me isn't contact's leader any more, (because it had been splitted meanwhile)
                   rejects the request
                 */
                if (is_leader && !is_contact && index_bro(me, 0, rcv_req->sender_id) == -1) {

                    /*
                    if (is_contact) {

                        // TODO : ce cas ne se produit jamais ici

                        // let the request in tasks_queue
                        answer.cnx_req.val_ret = UPDATE_NOK;
                    } else {
                    */

                        answer.cnx_req.val_ret = UPDATE_NOK;
                    //}
                    answer.cnx_req.new_contact.id = -1;

                    XBT_VERB("Node %d: [%s:%d] isn't leader of %d anymore - request fails",
                            me->self.id,
                            __FUNCTION__,
                            __LINE__,
                            rcv_req->sender_id);
                } else {

                    // run task only if me is the right leader or the contact
                    answer = connection_request(me,
                            rcv_args.cnx_req.new_node_id,
                            rcv_args.cnx_req.cs_new_node_prio,
                            rcv_args.cnx_req.try);
                }

                XBT_DEBUG("Node %d: [%s:%d] CNX_REQ returned val_ret = %s - contact = %d",
                        me->self.id,
                        __FUNCTION__,
                        __LINE__,
                        debug_ret_msg[answer.cnx_req.val_ret],
                        answer.cnx_req.new_contact.id);

                // In case of failure, let task in tasks_queue only if me is new node's contact
                // (UPDATE_NOK lets the request in queue, whereas FAILED removes it)
                if (answer.cnx_req.val_ret == UPDATE_NOK) {

                    val_ret = (is_contact ? UPDATE_NOK : FAILED);
                } else {

                    val_ret = answer.cnx_req.val_ret;
                }

                XBT_DEBUG("Node %d: [%s:%d] CNX_REQ modified val_ret : %s - is_contact : %d",
                        me->self.id,
                        __FUNCTION__,
                        __LINE__,
                        debug_ret_msg[val_ret],
                        is_contact);

                // Answer sender in case of success or FAILED (so that it may re-send its request to
                // another contact)
                if ((val_ret != FAILED && val_ret != UPDATE_NOK) || val_ret == FAILED) {

                    res = send_ans_sync(me,
                            rcv_args.cnx_req.new_node_id,
                            type,
                            rcv_req->sender_id,
                            rcv_req->answer_to,
                            answer);

                    //data_req_free(me, &rcv_req);
                    //task_free(task);              //TODO : ne pas oublier (libérer dans run_tasks_queue ?)
                }
            } else {


                // push CNX_REQ task on queue if not run by dedicated process
                XBT_VERB("Node %d: [%s:%d] push CNX_REQ task on tasks_queue",
                        me->self.id,
                        __FUNCTION__,
                        __LINE__);

                xbt_dynar_push(me->tasks_queue, task);

                *task = NULL;
                val_ret = STORED;
                //TODO : vérifier si on passe par là.
            }

            XBT_VERB("Node %d: [%s:%d] TASK_CNX_REQ done for new node %d - val_ret = %s",
                    me->self.id,
                    __FUNCTION__,
                    __LINE__,
                    rcv_args.cnx_req.new_node_id,
                    debug_ret_msg[val_ret]);

            display_sc(me, 'V');

            break;

        case TASK_NEW_BROTHER_RCV:
            //if (state.active == 'l' || state.active == 'u') {
            if (state.active == 'u' || state_search(me, 'l', -1) > -1) {

                // store task
                XBT_VERB("Node %d - active = '%c': store task for later"
                        " execution",
                        me->self.id,
                        state.active);

                xbt_dynar_push(me->delayed_tasks, task);
                *task = NULL;
                val_ret = STORED;
            } else {

                new_brother_received(me, rcv_args.new_brother_rcv.new_node_id);
                res = send_ans_sync(me,
                        rcv_args.new_brother_rcv.new_node_id,
                        type,
                        rcv_req->sender_id,
                        rcv_req->answer_to,
                        answer);          //TODO : answer n'est pas initialisé

                data_req_free(me, &rcv_req);
                task_free(task);

                XBT_VERB("Node %d: TASK_NEW_BROTHER_RCV done", me->self.id);
            }
            break;

        case TASK_SPLIT_REQ:
            if (state.active == 'b') {

                // store task (SPLIT_REQ isn't broadcasted)
                XBT_VERB("Node %d - active = '%c': store task for later"
                        " execution",
                        me->self.id,
                        state.active);

                xbt_dynar_push(me->delayed_tasks, task);
                *task = NULL;
                val_ret = STORED;
            } else {

                // run task
                split_request(me,
                        rcv_args.split_req.stage_nbr,
                        rcv_args.split_req.new_node_id);

                res = send_ans_sync(me,
                        rcv_args.split_req.new_node_id,
                        type,
                        rcv_req->sender_id,
                        rcv_req->answer_to,
                        answer);      //TODO : answer n'est pas initialisé

                data_req_free(me, &rcv_req);
                task_free(task);

                XBT_VERB("Node %d: TASK_SPLIT_REQ done", me->self.id);
            }
            break;

        case TASK_ADD_STAGE:
            add_stage(me);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_ADD_STAGE done", me->self.id);
            break;

        case TASK_CNX_GROUPS:

            if (state_search(me, 'u', rcv_args.cnx_groups.new_node_id) == -1 &&
                (state.active == 'b' ||
                 state_search(me, 'l', -1) > -1 ||
                 state_search(me, 'p', -1) > -1)) {

            /*
            if (state.active == 'b' || state.active == 'l' || state.active == 'p') {
            */

                display_states(me, 'V');
                // store task (CNX_GROUPS isn't broadcasted)
                XBT_VERB("Node %d - active = '%c': store task for later execution",
                        me->self.id,
                        state.active);

                xbt_dynar_push(me->delayed_tasks, task);
                *task = NULL;
                val_ret = STORED;
            } else {

                // run task now
                connect_splitted_groups(me,
                        rcv_args.cnx_groups.stage,
                        rcv_args.cnx_groups.pos_init,
                        rcv_args.cnx_groups.pos_new,
                        rcv_args.cnx_groups.init_rep_id,
                        rcv_args.cnx_groups.new_rep_id,
                        rcv_args.cnx_groups.new_node_id);

                // send a 'completed task' message back
                if (rcv_req->sender_id != me->self.id) {

                    XBT_VERB("Node %d: [%s:%d] send ack", me->self.id, __FUNCTION__, __LINE__);
                    send_completed(me, type, rcv_req->sender_id, rcv_req->answer_to, rcv_args.cnx_groups.new_node_id);
                }

                data_req_free(me, &rcv_req);
                task_free(task);

                XBT_VERB("Node %d: TASK_CNX_GROUPS done", me->self.id);
            }
            break;

        case TASK_SPLIT:
            //if (state.active == 'b' || state.active == 'l' || state.active == 'g') {
            if (state_search(me, 'u', rcv_args.cnx_groups.new_node_id) == -1 &&
                    (state.active == 'b' || state_search(me, 'l', -1) > -1 || state_search(me, 'g', -1) > -1)) {

                // store broadcasted task
                val_ret = STORED;
            } else {

                split(me, rcv_args.split.stage_nbr, rcv_args.split.new_node_id);

                data_req_free(me, &rcv_req);
                task_free(task);

                XBT_VERB("Node %d: TASK_SPLIT done for new node %d",
                        me->self.id,
                        rcv_args.split.new_node_id);
            }
            break;

        case TASK_NB_PRED:
            XBT_VERB("Node %d: active = '%c' - stage = %d, height = %d",
                    me->self.id,
                    state.active,
                    rcv_args.nb_pred.stage,
                    me->height);

            if (state.active == 'b') {

                /* set load to a negative value so that this node won't be
                   chosen as a representative during load balance */
                answer.nb_pred.load = -1;
            } else {

                xbt_assert(rcv_args.nb_pred.stage < me->height,
                        "Node %d in TASK_NB_PRED: stage too high !",
                        me->self.id);

                answer.nb_pred.load = me->pred_index[rcv_args.nb_pred.stage];
            }

            res = send_ans_sync(me,
                    rcv_args.nb_pred.new_node_id,
                    type,
                    rcv_req->sender_id,
                    rcv_req->answer_to,
                    answer);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_NB_PRED done", me->self.id);
            break;

        case TASK_ADD_PRED:
            if (state.active == 'b') {

                // store task (ADD_PRED isn't broadcasted)
                XBT_VERB("Node %d - active = '%c': store task for later"
                        " execution",
                        me->self.id,
                        state.active);

                xbt_dynar_push(me->delayed_tasks, task);
                *task = NULL;
                val_ret = STORED;
            } else {

                // run task
                add_pred(me, rcv_args.add_pred.stage, rcv_args.add_pred.new_pred_id);

                // send a 'completed task' message back
                if (rcv_req->sender_id != me->self.id &&
                    rcv_args.add_pred.w_ans == 1) {

                    send_completed(me, type, rcv_req->sender_id, rcv_req->answer_to, rcv_args.add_pred.new_node_id);
                }

                data_req_free(me, &rcv_req);
                task_free(task);

                XBT_VERB("Node %d: TASK_ADD_PRED done", me->self.id);
            }
            break;

        case TASK_DEL_PRED:
            del_pred(me, rcv_args.del_pred.stage, rcv_args.del_pred.pred2del_id);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id, rcv_req->answer_to, rcv_args.del_pred.new_node_id);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_DEL_PRED done", me->self.id);
            break;

        case TASK_BROADCAST:
            if (
                    /* TODO : je laisse finalement passer tous les SET_ACTIVE
                       modifier les commentaires en conséquence
                       Set_active ne doit fonctionner que si aucun ACK de
                       SET_ACTIVE n'est attendu. */

                    // state 'b': don't accept any broadcast
                    state.active == 'b' ||

                    /* state 'u': only accept broadcasts of:
                       - SET_UPDATE
                       (if it's been triggered by the same new node as current one)

                       - SET_ACTIVE
                       - ADD_STAGE
                       - SPLIT
                       - CS_REQ
                       - POP_STATE
                       */
                    (state.active == 'u' &&
                     (
                      !(rcv_args.broadcast.type == TASK_SET_UPDATE &&
                        rcv_args.broadcast.args->set_update.new_node_id == state.new_id) &&
                      rcv_args.broadcast.type != TASK_SET_ACTIVE &&
                      rcv_args.broadcast.type != TASK_ADD_STAGE &&
                      rcv_args.broadcast.type != TASK_SPLIT &&
                      rcv_args.broadcast.type != TASK_CS_REQ &&
                      rcv_args.broadcast.type != TASK_REMOVE_STATE
                     )
                    )) {

                        /* for refused broadcasted tasks, either send an answer back ... */
                        if (rcv_args.broadcast.type == TASK_SET_ACTIVE ||
                            rcv_args.broadcast.type == TASK_SET_UPDATE) {

                            XBT_VERB("Don't accept '%s' here - current new_id = %d"
                                    " - rcv_new_id = %d",
                                    debug_msg[rcv_args.broadcast.type],
                                    state.new_id,
                                    (rcv_args.broadcast.type == TASK_SET_UPDATE ?
                                     rcv_args.broadcast.args->set_update.new_node_id :
                                     rcv_args.broadcast.args->set_active.new_node_id));

                            /* if set_update is refused, tell the caller.
                               if set_active is refused, nevermind, it'll work another
                               time */
                            val_ret = (rcv_args.broadcast.type == TASK_SET_UPDATE ?
                                    UPDATE_NOK : OK);

                            if (rcv_req->sender_id != me->self.id) {

                                /* send an answer back (stop broadcast) */
                                answer.handle.val_ret = val_ret;
                                answer.handle.val_ret_id = me->self.id;
                                answer.handle.br_type = rcv_args.broadcast.type;

                                res = send_ans_sync(me,
                                        rcv_args.broadcast.new_node_id,
                                        type,
                                        rcv_req->sender_id,
                                        rcv_req->answer_to,
                                        answer);

                                XBT_VERB("Node %d: answer '%s' sent to %d",
                                        me->self.id,
                                        (val_ret == UPDATE_NOK ? "UPDATE_NOK" : "OK"),
                                        rcv_req->sender_id);
                            }

                            //data_ans_free(me, &rcv);

                            data_req_free(me, &rcv_req);
                            task_free(task);

                        } else {

                            /* ... either store task */
                            XBT_VERB("Node %d - active = '%c' - new_id = %d:"
                                    " store task '%s' for later execution",
                                    me->self.id,
                                    state.active,
                                    state.new_id,
                                    debug_msg[rcv_args.broadcast.type]);

                            xbt_dynar_push(me->delayed_tasks, task);
                            *task = NULL;
                            val_ret = STORED;
                        }
                    } else {

                        /* start call: forward broadcast to the leader ? */
                        //TODO : est-ce que ça a vraiment un intérêt de déléguer la diffusion au leader ?
                        if (rcv_args.broadcast.first_call == 1) {

                            if (rcv_args.broadcast.type == TASK_SET_UPDATE) {

                                XBT_VERB("Node %d: Run broadcast of Set Update for new_id = %d - state.new_id = %d",
                                        me->self.id,
                                        rcv_args.broadcast.args->set_update.new_node_id,
                                        state.new_id);
                            }

                            XBT_VERB("Node %d: broadcast first call - stage = %d -"
                                    " height = %d - broadcasted task = '%s' - lead_br = %d",
                                    me->self.id,
                                    rcv_args.broadcast.stage,
                                    me->height - 1,
                                    debug_msg[rcv_args.broadcast.type],
                                    rcv_args.broadcast.lead_br);

                            /* - don't forward a MERGE task to the leader
                             *   (the leaving node may have left the leader, so it wouldn't
                             *    be able to broadcast a request properly)
                             *
                             * - don't forward a CLEAN_UPPER_STAGE task
                             * - don't forward an UPDATE_UPPER_STAGE task */
                            if (me->self.id ==
                                    //me->brothers[rcv_args.broadcast.stage][0].id ||
                                    me->brothers[0][0].id ||
                                    rcv_args.broadcast.type == TASK_MERGE ||
                                    rcv_args.broadcast.type == TASK_CLEAN_STAGE ||
                                    rcv_args.broadcast.type == TASK_UPDATE_UPPER_STAGE) {

                                // I am the leader
                                XBT_DEBUG("Node %d: Broadcast '%s' start",
                                        me->self.id,
                                        debug_msg[rcv_args.broadcast.type]);

                                task_free(task);                            //TODO : vérifier cette libération

                                rcv_args.broadcast.first_call = 0;
                                val_ret = broadcast(me, rcv_args);
                            } else {

                                // forward broadcast request to the leader
                                XBT_VERB("Node %d: forward broadcast request to leader %d",
                                        me->self.id,
                                        me->brothers[0][0].id);

                                /* TODO : cette partie peut probablement
                                   être supprimée depuis le mécanisme de cs_req */

                                /* If a Set Active task is broacasted, it
                                 * mustn't be interrupted by another
                                 * broadcast (of Set Update, for instance)
                                 */
                                /*
                                   if (rcv_args.broadcast.type == TASK_SET_ACTIVE) {

                                   set_update(me,
                                   rcv_args.broadcast.args->set_active.new_id);
                                   }
                                   */

                                ans_data_t answer_data = NULL;
                                res = send_msg_sync(me,
                                        TASK_BROADCAST,
                                        me->brothers[0][0].id,
                                        rcv_args,
                                        &answer_data);

                                xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                                        me->self.id,
                                        debug_msg[TASK_BROADCAST],
                                        me->brothers[rcv_args.broadcast.stage][0].id);

                                // get the return value
                                val_ret = answer_data->answer.handle.val_ret;
                                data_ans_free(me, &answer_data);
                            }

                            /* send a message back (in case of sync call of TASK_BROADCAST) */
                            if (rcv_req->sender_id != me->self.id) {

                                answer.handle.val_ret = val_ret;
                                answer.handle.val_ret_id = me->self.id;
                                answer.handle.br_type = rcv_args.broadcast.type;

                                res = send_ans_sync(me,
                                        rcv_args.broadcast.new_node_id,
                                        type,
                                        rcv_req->sender_id,
                                        rcv_req->answer_to,
                                        answer);

                                XBT_DEBUG("Node %d: answer '%s' sent to %d",
                                        me->self.id,
                                        (val_ret == UPDATE_NOK ? "UPDATE NOK" : "OK"),
                                        rcv_req->sender_id);
                            }

                            task_free(task);
                        } else {

                            // next broadcast calls
                            if (rcv_args.broadcast.stage > 0) {

                                XBT_VERB("Node %d: Received Broadcast '%s' - lead_br = %d",
                                        me->self.id,
                                        debug_msg[rcv_args.broadcast.type],
                                        rcv_args.broadcast.lead_br);

                                task_free(task);

                                // Transmit the message to lower stage.
                                rcv_args.broadcast.stage--;
                                val_ret = broadcast(me, rcv_args);
                            } else {

                                /* stage 0 reached: handle the broadcasted task */
                                XBT_VERB("Node %d: stage 0 - run broadcasted task - '%s'",
                                        me->self.id,
                                        debug_msg[rcv_args.broadcast.type]);

                                XBT_DEBUG("Node %d: active = '%c' - new_id = %d",
                                        me->self.id,
                                        state.active,
                                        state.new_id);

                                // only if me is active
                                if (state.active != 'n') {

                                    req_data_t req_data = xbt_new0(s_req_data_t, 1);
                                    req_data->type = rcv_args.broadcast.type;
                                    req_data->sender_id = me->self.id;
                                    req_data->recipient_id = me->self.id;
                                    set_proc_mailbox(req_data->answer_to);
                                    //set_mailbox(me->self.id, req_data->answer_to);
                                    set_mailbox(me->self.id, req_data->sent_to);

                                    if (rcv_args.broadcast.args != NULL) {

                                        req_data->args = *(rcv_args.broadcast.args);
                                    } else {

                                        /* XBT_DEBUG("Node %d: broadcast.args = NULL",
                                           me->self.id); */
                                    }

                                    msg_task_t br_task = MSG_task_create("async",
                                            COMP_SIZE,
                                            COMM_SIZE,
                                            req_data);
                                    //MSG_task_set_name(br_task, "async");

                                    val_ret = handle_task(me, &br_task);

                                    /* if br_task has to be delayed, store the whole
                                       broadcasted task */
                                    if (val_ret == STORED) {

                                        // store task
                                        XBT_VERB("Node %d - active = '%c': store "
                                                "broadcasted task for later execution",
                                                me->self.id,
                                                state.active);

                                        xbt_dynar_push(me->delayed_tasks, task);
                                        *task = NULL;     //TODO : utile ?

                                        data_req_free(me, &req_data);
                                        task_free(&br_task);
                                    } else {

                                        task_free(task);
                                    }
                                }

                                XBT_VERB("Node %d: [%s:%d] End of run broadcasted task - '%s'",
                                        me->self.id,
                                        __FUNCTION__,
                                        __LINE__,
                                        debug_msg[rcv_args.broadcast.type]);
                            }
                        }
                        // send an answer back if task hasn't been stored
                        if (val_ret != STORED) {

                            if (strcmp(proc_data->proc_mailbox, rcv_req->answer_to) != 0) {

                                answer.handle.val_ret = val_ret;
                                answer.handle.val_ret_id = me->self.id;
                                answer.handle.br_type = rcv_args.broadcast.type;

                                res = send_ans_sync(me,
                                        rcv_args.broadcast.new_node_id,
                                        type,
                                        rcv_req->sender_id,
                                        rcv_req->answer_to,
                                        answer);

                                XBT_DEBUG("Node %d: answer '%s' sent to %d",
                                        me->self.id,
                                        (val_ret == UPDATE_NOK ? "UPDATE_NOK" : "OK"),
                                        rcv_req->sender_id);
                            } else {

                                XBT_DEBUG("Node %d: No answer after broadcast - answer_to: %s",
                                        me->self.id,
                                        rcv_req->answer_to);
                            }
                        }

                        if (val_ret != STORED) {

                            //compteur[rcv_args.broadcast.type]--;
                            data_req_free(me, &rcv_req);
                        }
                        // TODO : voir si cette libération ne pose pas de
                        // problème avec les données stockées dans le dynar
                    }
            break;

        case TASK_DISPLAY_VAR:
            display_var(me);

            data_req_free(me, &rcv_req);
            task_free(task);

            break;

        case TASK_GET_SIZE:
            answer.get_size.size = me->bro_index[rcv_args.get_size.stage];

            res = send_ans_sync(me,
                    rcv_args.get_size.new_node_id,
                    type,
                    rcv_req->sender_id,
                    rcv_req->answer_to,
                    answer);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_GET_SIZE done", me->self.id);
            break;

        case TASK_DEL_BRO:
            del_bro(me,
                    rcv_args.del_bro.stage,
                    rcv_args.del_bro.bro2del);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                //TODO: le premier envoi n'est pas reçu par wait_for_completion. bug ?
                //on peut tenter d'en ôter un
                send_completed(me, type, rcv_req->sender_id, rcv_req->answer_to, rcv_args.del_bro.new_node_id);
                send_completed(me, type, rcv_req->sender_id, rcv_req->answer_to, rcv_args.del_bro.new_node_id);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_DEL_BRO done", me->self.id);
            break;

        case TASK_REPL_BRO:
            init_idx = index_bro(me, rcv_args.repl_bro.stage, rcv_req->sender_id);

            XBT_DEBUG("Node %d: stage = %d - init_idx = %d - new_id = %d",
                    me->self.id,
                    rcv_args.repl_bro.stage,
                    init_idx,
                    rcv_args.repl_bro.new_id);

            replace_bro(me,
                    rcv_args.repl_bro.stage,
                    init_idx,
                    rcv_args.repl_bro.new_id,
                    rcv_args.repl_bro.new_node_id);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id, rcv_req->answer_to, rcv_args.repl_bro.new_node_id);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_REPL_BRO done", me->self.id);
            break;

        case TASK_MERGE:
            merge(me,
                    rcv_args.merge.nodes_array,
                    rcv_args.merge.nodes_array_size,
                    rcv_args.merge.stage,
                    rcv_args.merge.pos_me,
                    rcv_args.merge.pos_contact,
                    rcv_args.merge.right,
                    rcv_args.merge.new_node_id);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id, rcv_req->answer_to, rcv_args.merge.new_node_id);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_MERGE done", me->self.id);
            break;

        case TASK_BROADCAST_MERGE:
            broadcast_merge(me,
                    rcv_args.broad_merge.stage,
                    rcv_args.broad_merge.pos_me,
                    rcv_args.broad_merge.pos_contact,
                    rcv_args.broad_merge.right,
                    rcv_args.broad_merge.lead_br,
                    rcv_args.broad_merge.new_node_id);

            res = send_ans_sync(me,
                    rcv_args.broad_merge.new_node_id,
                    type,
                    rcv_req->sender_id,
                    rcv_req->answer_to,
                    answer);          //TODO : answer pas initialisé

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_BROADCAST_MERGE done", me->self.id);
            break;

        case TASK_DEL_ROOT:
            del_root(me, rcv_args.del_root.init_height);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_DEL_ROOT done", me->self.id);
            break;

        case TASK_CLEAN_STAGE:
            clean_upper_stage(me,
                    rcv_args.clean_stage.stage,
                    rcv_args.clean_stage.pos_me,
                    rcv_args.clean_stage.pos_contact,
                    rcv_args.clean_stage.new_node_id);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_CLEAN_STAGE done", me->self.id);
            break;

        case TASK_MERGE_REQ:
            merge_request(me, rcv_args.merge_req.new_node_id);

            res = send_ans_sync(me,
                    rcv_args.merge_req.new_node_id,
                    type,
                    rcv_req->sender_id,
                    rcv_req->answer_to,
                    answer);          //TODO : answer pas initialisé

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_MERGE_REQ done", me->self.id);
            break;

        case TASK_SET_ACTIVE:
            set_active(me, rcv_args.set_active.new_node_id);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id, rcv_req->answer_to, rcv_args.set_active.new_node_id);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_SET_ACTIVE done", me->self.id);
            break;

        case TASK_SET_UPDATE:
            val_ret = set_update(me, rcv_args.set_update.new_node_id, rcv_args.set_update.new_node_prio);

            // send the answer back
            if (rcv_req->sender_id != me->self.id) {

                XBT_DEBUG("Node %d: send TASK_SET_UPDATE answer - active = '%c'",
                        me->self.id,
                        state.active);

                answer.handle.val_ret = val_ret;
                answer.handle.val_ret_id = me->self.id;

                res = send_ans_sync(me,
                        rcv_args.set_update.new_node_id,
                        type,
                        rcv_req->sender_id,
                        rcv_req->answer_to,
                        answer);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            state = get_state(me);
            XBT_VERB("Node %d: '%c'/%u - TASK_SET_UPDATE done - val_ret = '%s'",
                    me->self.id,
                    state.active,
                    state.new_id,
                    (val_ret == UPDATE_NOK ?  "UPDATE_NOK" : "UPDATE_OK"));
            break;

        case TASK_SET_STATE:
            XBT_DEBUG("Node %d: before TASK_SET_STATE - active = '%c'/%u",
                    me->self.id,
                    state.active,
                    state.new_id);
            display_states(me, 'D');

            set_state(me,
                    rcv_args.set_state.new_node_id,
                    rcv_args.set_state.state);

            state = get_state(me);
            XBT_VERB("Node %d: TASK_SET_STATE done - active = '%c'/%u",
                    me->self.id,
                    state.active,
                    state.new_id);
            display_states(me, 'V');

            data_req_free(me, &rcv_req);
            task_free(task);

            break;

        case TASK_IS_BROTHER:
            answer = is_brother(me, rcv_args.is_brother.id, rcv_args.is_brother.new_node_id);

            res = send_ans_sync(me,
                    rcv_args.is_brother.new_node_id,
                    type,
                    rcv_req->sender_id,
                    rcv_req->answer_to,
                    answer);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_IS_BROTHER done", me->self.id);
            break;

        case TASK_TRANSFER:
            answer = transfer(me,
                    rcv_args.transfer.st,
                    rcv_args.transfer.right,
                    rcv_args.transfer.cut_pos,
                    rcv_args.transfer.sender,
                    rcv_args.transfer.new_node_id);

            res = send_ans_sync(me,
                    rcv_args.transfer.new_node_id,
                    type,
                    rcv_req->sender_id,
                    rcv_req->answer_to,
                    answer);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_TRANSFER done", me->self.id);
            break;

            /*
               case TASK_DEL_MEMBER:
               del_member(me,
               rcv_args.del_member.stage,
               rcv_args.del_member.start,
               rcv_args.del_member.end);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

            send_completed(me, type, rcv_req->sender_id);
            }

            XBT_VERB("Node %d: TASK_DEL_MEMBER done", me->self.id);
            break;
            */

        case TASK_ADD_BRO_ARRAY:
            add_bro_array(me,
                    rcv_args.add_bro_array.stage,
                    rcv_args.add_bro_array.bro,
                    rcv_args.add_bro_array.array_size,
                    rcv_args.add_bro_array.right,
                    rcv_args.add_bro_array.new_node_id);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id, rcv_req->answer_to, rcv_args.add_bro_array.new_node_id);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_ADD_BRO_ARRAY done", me->self.id);
            break;

        case TASK_SHIFT_BRO:
            shift_bro(me,
                    rcv_args.shift_bro.stage,
                    rcv_args.shift_bro.new_node,
                    rcv_args.shift_bro.right,
                    rcv_args.shift_bro.new_node_id);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id, rcv_req->answer_to, rcv_args.shift_bro.new_node_id);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_SHIFT_BRO done", me->self.id);
            break;

        case TASK_ADD_BRO:
            add_brother(me,
                    rcv_args.add_bro.stage,
                    rcv_args.add_bro.new_id);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id, rcv_req->answer_to, rcv_args.add_bro.new_node_id);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_ADD_BRO done", me->self.id);
            break;

        case TASK_CUT_NODE:
            cut_node(me,
                    rcv_args.cut_node.stage,
                    rcv_args.cut_node.right,
                    rcv_args.cut_node.cut_pos,
                    rcv_args.cut_node.new_node_id);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id, rcv_req->answer_to, rcv_args.cut_node.new_node_id);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_CUT_NODE done", me->self.id);
            break;

        case TASK_BR_ADD_BRO_ARRAY:
            br_add_bro_array(me,
                    rcv_args.br_add_bro_array.stage,
                    rcv_args.br_add_bro_array.bro,
                    rcv_args.br_add_bro_array.array_size,
                    rcv_args.br_add_bro_array.right,
                    rcv_args.br_add_bro_array.new_node_id);

            res = send_ans_sync(me,
                    rcv_args.br_add_bro_array.new_node_id,
                    type,
                    rcv_req->sender_id,
                    rcv_req->answer_to,
                    answer);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_BR_ADD_BRO_ARRAY done", me->self.id);
            break;

        case TASK_UPDATE_UPPER_STAGE:
            update_upper_stage(me,
                    rcv_args.update_upper_stage.stage,
                    rcv_args.update_upper_stage.pos2repl,
                    rcv_args.update_upper_stage.new_id,
                    rcv_args.update_upper_stage.new_node_id);

            res = send_ans_sync(me,
                    rcv_args.update_upper_stage.new_node_id,
                    type,
                    rcv_req->sender_id,
                    rcv_req->answer_to,
                    answer);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_UPDATE_UPPER_STAGE done", me->self.id);
            break;

        case TASK_GET_NEW_CONTACT:
            answer = get_new_contact(me, rcv_args.get_new_contact.new_node_id);

            res = send_ans_sync(me,
                    rcv_args.get_new_contact.new_node_id,
                    type,
                    rcv_req->sender_id,
                    rcv_req->answer_to,
                    answer);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_GET_NEW_CONTACT done", me->self.id);
            break;

        case TASK_CS_REQ:
            val_ret = cs_req(me,
                    rcv_args.cs_req.sender_id,
                    rcv_args.cs_req.new_node_id,
                    rcv_args.cs_req.cs_new_node_prio);

            // send the answer back
            state = get_state(me);
            if (rcv_req->sender_id != me->self.id) {

                XBT_DEBUG("Node %d: send TASK_CS_REQ answer - active = '%c'",
                        me->self.id,
                        state.active);

                answer.handle.val_ret = val_ret;
                answer.handle.val_ret_id = me->self.id;

                res = send_ans_sync(me,
                        rcv_args.cs_req.new_node_id,
                        type,
                        rcv_req->sender_id,
                        rcv_req->answer_to,
                        answer);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: '%c'/%u - TASK_CS_REQ done for new node %d - val_ret = %s",
                    me->self.id,
                    state.active,
                    state.new_id,
                    rcv_args.cs_req.new_node_id,
                    (val_ret == UPDATE_NOK ? "NOK" : "OK"));
            break;

        case TASK_END_GET_REP:
            idx = state_search(me, 'g', rcv_args.end_get_rep.new_node_id);
            if (idx > -1) {

                XBT_DEBUG("Node %d: idx = %d", me->self.id, idx);
                xbt_dynar_remove_at(me->states, idx, NULL);
            }

            XBT_VERB("Node %d: in END_GET_REP for new_id %d",
                    me->self.id,
                    rcv_args.end_get_rep.new_node_id);
            display_states(me, 'V');

            xbt_assert(xbt_dynar_length(me->states) > 0,
                    "Node %d: (in END_GET_REP) dynar states is empty !",
                    me->self.id);

            /*
               if (rcv_req->sender_id != me->self.id) {

               res = send_ans_sync(me,
               rcv_args.end_get_rep.new_node_id,
               type,
               rcv_req->sender_id,
               answer);
               }
            */

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_END_GET_REP done", me->self.id);
            break;

        case TASK_REMOVE_STATE:
            remove_state(me, rcv_args.remove_state.new_node_id, rcv_args.remove_state.active);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_REMOVE_STATE done", me->self.id);
            break;

        case TASK_CS_REL:
            cs_rel(me, rcv_args.cs_rel.new_node_id);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_CS_REL done", me->self.id);
            break;

        case TASK_CHECK_CS:
            check_cs(me, rcv_req->sender_id);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_CHECK_CS done", me->self.id);
            break;
    }

    if (type != TASK_BROADCAST) {

        display_rout_table(me, 'V');

        // set and store DST infos
        set_n_store_infos(me);

        display_preds(me, 'D');
    }

    XBT_DEBUG("Node %d end of handle_task(): val_ret = '%s'",
            me->self.id,
            debug_ret_msg[val_ret]);

    /*
    if (val_ret == OK) {

        xbt_assert(*task == NULL,
                "Node %d: task should be NULL at the end of handle_task !! type = %s",
                me->self.id,
                debug_msg[type]);
    }
    */

    XBT_OUT();
    return val_ret;
}

/**
 * \brief Main function
 */
int main(int argc, char *argv[]) {

    XBT_IN();

    if (argc < 3) {

        printf("Usage: %s platform_file deployment_file"
                " --log=msg_dst.thres:info 2>&1 | tools/MSG_visualization/colorize.pl\n",
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
    infos_dst = xbt_dynar_new(sizeof(dst_infos_t), &elem_free);

    // init array of failed nodes
    failed_nodes = xbt_new0(s_f_node_t, 1);

    // create timer
    xbt_os_timer_t timer = xbt_os_timer_new();

    const char* platform_file = argv[1];
    const char* deployment_file = argv[2];
    //if (argc == 4) max_simulation_time = atoi(argv[3]);

    nb_nodes = count_lines_of_file(deployment_file);
    XBT_INFO("START BUILDING A DST OF %d NODES", nb_nodes);

    //MSG_set_channel_number(0);         //TODO: je n'ai pas compris l'utilité de set_channel_number
    MSG_create_environment(platform_file);

    MSG_function_register("node", node);
    MSG_launch_application(deployment_file);

    xbt_os_walltimer_start(timer);
    msg_error_t res = MSG_main();
    xbt_os_walltimer_stop(timer);


    // print all routing tables
    XBT_INFO("************************************     PRINT ALL  (nb_ins_nodes = %d)    "
            "************************************\n", nb_ins_nodes);
    unsigned int cpt = 0, loc_nb_nodes = 0, loc_nb_nodes_tot = 0;

    XBT_VERB("compt_proc_rest = %d - compt_proc = %d", compt_proc_rest, compt_proc);

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

    xbt_dynar_foreach(infos_dst, cpt, elem) {
        if (elem != NULL) {

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
                tot_msg      += nb_messages[elem->node_id][i];
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
                xbt_assert(size < 100, "Non_active array too small !");
            }
            xbt_free(elem->routing_table);
            elem->routing_table = NULL;

            xbt_free(elem->load);
            elem->load = NULL;

            /*
            // has been freed in node_free
            XBT_INFO("Size - %p", elem->size);
            xbt_free(elem->size);
            elem->size = NULL;
            */

            xbt_free(elem);
            elem = NULL;

        } else {
            XBT_VERB("cpt: %d, elem = NULL", cpt);
        }
    }

    //fclose(fp);

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

    XBT_INFO("compteur");
    int z = 0;
    for (z = 0; z < TYPE_NBR; z++) {
        if (compteur[z] != 0) {

                XBT_INFO("\tcpt[%s] = %d", debug_msg[z], compteur[z]);
        }
    }

    xbt_os_timer_free(timer);

    //MSG_clean();
    XBT_OUT();

    if (res == MSG_OK)
        return 0;
    else
        return 1;
}

/*
   ========================  Functions run by fork processes  ==================================
*/

/*
 * \brief Function run by one of Handle_task process (Cnx_req, Br_Split, Br_Cs_Req)
 */
static int proc_handle_task(int argc, char* argv[]) {

    setId_proc_mailbox();
    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());
    req_data_t  req_data  = MSG_task_get_data(proc_data->task);

    msg_task_t  proc_task = proc_data->task;

    XBT_VERB("Node %d: [%s:%d] Start proc_handle_task - proc_mailbox = %s - Attempt number %d",
            proc_data->node->self.id,
            __FUNCTION__,
            __LINE__,
            proc_data->proc_mailbox,
            (req_data->type == TASK_CNX_REQ ? req_data->args.cnx_req.try : -1));

    XBT_DEBUG("[%s:%d] - mailbox = '%s' - task = %p - %p",
            __FUNCTION__,
            __LINE__,
            proc_data->proc_mailbox,
            proc_data->task,
            proc_task);

    //task_free(&proc_data->task);

    if (req_data->type == TASK_CNX_REQ) {

        proc_data->node->run_task.run_state = RUNNING;

        XBT_VERB("Node %d: [%s:%d] set run_state = {%s - %s}",
                proc_data->node->self.id,
                __FUNCTION__,
                __LINE__,
                debug_run_msg[proc_data->node->run_task.run_state],
                debug_ret_msg[proc_data->node->run_task.last_ret]);

        proc_data->node->run_task.last_ret = handle_task(proc_data->node, &proc_task);
        proc_data->node->run_task.run_state = IDLE;

        XBT_VERB("Node %d: [%s:%d] set run_state = {%s - %s}",
                proc_data->node->self.id,
                __FUNCTION__,
                __LINE__,
                debug_run_msg[proc_data->node->run_task.run_state],
                debug_ret_msg[proc_data->node->run_task.last_ret]);
    } else {

        handle_task(proc_data->node, &proc_task);
    }

    XBT_VERB("Node %d: [%s:%d] process '%s' dies",
            proc_data->node->self.id,
            __FUNCTION__,
            __LINE__,
            proc_data->proc_mailbox);

    XBT_DEBUG("task = %p", proc_task);

    MSG_process_set_data_cleanup(proc_data_cleanup);
    MSG_process_kill(MSG_process_self());

    return 0;
}

/*
 * \brief Function run by Tasks_queue process in charge of processing the queue of requested tasks
 */
static int proc_run_tasks(int argc, char* argv[]) {

    // Attach process Id to the mailbox to make it unique
    setId_proc_mailbox();

    proc_data_t proc_data = MSG_process_get_data(MSG_process_self());

    XBT_VERB("Node %d: [%s:%d] mailbox = '%s'",
            proc_data->node->self.id,
            __FUNCTION__,
            __LINE__,
            proc_data->proc_mailbox);

    run_tasks_queue(proc_data->node);

    xbt_assert(proc_data != NULL, "proc_data = NULL !!");

    /*
    XBT_VERB("Node %d: fork process (run_tasks_queue) dies node = %p",
    //TODO : chercher pourquoi node a été libéré avant cet appel (pas node_free, semble-t-il)

            proc_data->node->self.id,
            proc_data->node);
            */

    MSG_process_set_data_cleanup(proc_data_cleanup);
    MSG_process_kill(MSG_process_self());

    return 0;
}

/*
 * \brief Cleanup function run when a process dies
 * \param arg data to destroy
 */
static void proc_data_cleanup(void* arg) {
    XBT_IN();

    proc_data_t proc_data = (proc_data_t)arg;

    xbt_dynar_free(&(proc_data->async_answers));
    xbt_dynar_free(&(proc_data->sync_answers));

    xbt_free(proc_data);

    arg = NULL;

    XBT_OUT();
}
