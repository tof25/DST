
/*
 *  dst.c
 *
 *  Written by Christophe Enderlin on 2013/02/16
 *
 */

/* TODO: gérer la non-réponse d'un contact lors de l'arrivée d'un nœud.
 *       D'une façon générale, il faut le faire pour toutes les communications
 *       synchrones et voir aussi ce qu'on fait si pas de réponse à wait_for_completion().
 *       En clair, introduire de la tolérance aux pannes. */

/* TODO: voir si tout est OK pour les timers */

/* TODO: il reste des problèmes de gestion mémoire. On les voit apparaître vers
 *       b >= 66 */

#define MSG_USE_DEPRECATED
#include <stdio.h>                          // printf and friends
#include "msg/msg.h"                        // to use MSG API of Simgrid
#include "xbt/log.h"                        // to get nice outputs
#include "xbt/asserts.h"                    // to use xbt_assert()
#include "xbt/xbt_os_time.h"                /* to use a timer (located in
                                               simgrid-3.8.1/src/include/xbt) */
#include "xbt/ex.h"                         // to use exceptions
#include <time.h>
#include <stdlib.h>                         // to use rand()

XBT_LOG_NEW_DEFAULT_CATEGORY(msg_dst, "Messages specific for the DST");

#define COMM_SIZE 10                        /* message size when creating a
                                               new task */
//TODO: (comment la calcule-t-on ?)
#define COMP_SIZE 0                         /* compute duration when creating a
                                               new task */
#define MAILBOX_NAME_SIZE 15                // name size of a mailbox
#define TYPE_NBR 34                         // number of task types
#define MAX_WAIT_COMPL 1550                 /* won't wait longer for broadcast
                                               completion */
#define MAX_WAIT_GET_REP 20                 /* won't wait longer an answer to a
                                               GET_REP request */
#define MAX_JOIN 100                         // number of joining attempts

static int nb_calls1 = 0;
static int nb_calls2 = 0;
static const int a = 2;                     /* min number of brothers in a node
                                               (except for the root node) */
static const int b = 4;                     // max number of brothers in a node
//static int timeout = 100;                   // timeout for communications
static double max_simulation_time = 10500;  // max simulation time
static xbt_dynar_t infos_dst;               /* to store all the routing tables
                                               and other global DST infos */
static int nb_messages[TYPE_NBR] = {0};     /* total number of messages used by
                                               each task type */
static int nb_br_messages[TYPE_NBR] = {0};  /* total number of broadcasted
                                               messages used by each task type */
static int order = 0;                       // order number of nodes arrival

typedef struct f_node {                     /* struct for a node that failed to
                                               join */
    int   id;
    float f_time;
} s_f_node_t;

static int nb_abort = 0;                    /* number of join abortions */
static s_f_node_t *failed_nodes = NULL;     /* array of nodes id that couldn't
                                               join the DST */



/**
 * Infos about the DST to be printed at the end
 */
typedef struct s_dst_info {
    int   order;                            // arrival order
    int   node_id;                          // node id
    char  active;                           // node state
    char *routing_table;                    /* string representation of a
                                               routing table */
    int   nb_messages;                      /* number of messages needed for the
                                               node to join */
    int   add_stage;                        /* boolean: was it necessary to add
                                               a stage to insert this node ? */
    int   nbr_split_stages;                 /* number of splitted stages to make
                                               room for this node */
    int  *load;                             /* load table (number of predecessors
                                               per stage) */
    int  *size;                             /* number of brothers for each stage */
} s_dst_infos_t, *dst_infos_t;

/**
 * A node representative
 */
typedef struct node_rep {

    int id;                                 // representative id
    char mailbox[MAILBOX_NAME_SIZE];        /* representative mailbox name
                                               (string representation of the id) */
} s_node_rep_t, *node_rep_t;

/**
 * Types of request/answer tasks exchanged
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
    TASK_NB_PRED,           /* get the number of predecessors of a node,
                               for a given stage */
    TASK_ADD_PRED,          // add a predecessor
    TASK_DEL_PRED,          // delete a predecessor
    TASK_BROADCAST,         // broadcast a message
    TASK_DISPLAY_VAR,       /* display a variable of a remote node
                               (for debugging purposes) */
    TASK_GET_SIZE,          // return the number of brothers on a given stage
    TASK_DEL_BRO,           // delete a brother in a given stage
    TASK_REPL_BRO,          // replace a brother by a new one
    TASK_MERGE,             // merge groups when a node leaves
    TASK_BROADCAST_MERGE,   // broadcast a merge task
    TASK_DEL_ROOT,          // delete the root when a merge occurs
    TASK_CLEAN_STAGE,       /* clean useless nodes in a stage when a merge
                               occured in lower ones */
    TASK_MERGE_REQ,         // request merges tasks
    TASK_SET_ACTIVE,        // set active state
    TASK_SET_UPDATE,        // set update state
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
    TASK_GET_NEW_CONTACT,   // get a new contact for a new coming node that failed to join
    TASK_FLAG_REQ           // ask leaders if a set update broadcast is possible
} e_task_type_t;

/**
 * Values returned by handle_task()
 */
typedef enum {
    OK,                     // no problem
    STORED,                 // task stored
    UPDATE_OK,              // set_update ok
    UPDATE_NOK              // set_update not ok
} e_val_ret_t;

typedef struct ans_data s_ans_data_t, *ans_data_t;

/**
 * A recipient answer record                // see wait_for_completion()
 */
typedef struct recp_rec {

    e_task_type_t   type;                   // type of expected answer ...
    s_node_rep_t    recp;                   // ... from this recipient
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
 * Node data
 **/
typedef struct node {

    s_node_rep_t    self;                   // node's id and mailbox
    s_node_rep_t    **brothers;             // routing table of size b * height
    s_node_rep_t    **preds;                // predecessors table
    int             *pred_index;            /* for each stage, the last
                                               predecessor index */
    int             *bro_index;             /* for each stage, the last brother
                                               index */
    int             height;                 // height of the DST
    msg_comm_t      comm_received;          // current communication
    double          deadline;               // time to leave the DST
    xbt_dynar_t     states;                 // node states
    //struct state    state;                  // current node state
    //struct state    old_state;              // former node state

    s_dst_infos_t   dst_infos;              // infos about the DST
    //recp_rec_t      recp_array;
    //int             recp_size;

    xbt_dynar_t     expected_answers;       /* recipients whose answers are
                                               expected (see wait_for_completion)*/
    xbt_dynar_t     remain_tasks;           // delayed tasks
    xbt_dynar_t     sync_answers;           /* answers to sync requests (see
                                               send_sync_msg) */
    xbt_dynar_t     cnx_req_queue;          // new nodes to be inserted into the DST
} s_node_t, *node_t;

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
    "Flag Request"
};

/**
 * Requests args
 */
typedef struct {

    int stage;
    int new_node_id;
} s_task_get_rep_t;             // get a node representative

typedef struct {

    s_node_rep_t new_node;
    int          try;
} s_task_cnx_req_t;             // get the DST ready to receive a new node

typedef struct {

    s_node_rep_t new_node;
} s_task_new_brother_rcv_t;     // insert a new node in a node's first stage

typedef struct {

    int stage_nbr;
    int new_node_id;
} s_task_split_req_t;           // request a node to split

typedef struct {

    int stage,
        pos_init,
        pos_new,
        init_rep_id,
        new_rep_id,
        new_node_id;
} s_task_cnx_groups_t;          // make a node connect to his newly splitted sons

typedef struct {

    int stage_nbr; 
    int new_node_id;
} s_task_split_t;               // execute the splitting

typedef struct {

    int stage;
} s_task_nb_pred_t;             /* get the number of predecessors of a node,
                                   given its stage */

typedef struct {

    int stage;
    int new_pred_id;
} s_task_add_pred_t;            // add a predessessor

typedef struct {

    int stage;
    int pred2del_id;
} s_task_del_pred_t;            // delete a predecessor

typedef union req_args u_req_args_t, *req_args_t;

typedef struct {

    e_task_type_t type;
    int stage;
    int first_call;
    int source_id;
    int lead_br;
    req_args_t args;
} s_task_broadcast_t;           // broadcast a message of type 'type'

typedef struct {

    int stage;
} s_task_get_size_t;            // get the number of brothers on 'stage'

typedef struct {

    int stage;
    int bro2del;
} s_task_del_bro_t;             // delete a brother for a given stage

typedef struct {

    int stage;
    int new_id;
} s_task_repl_bro_t;            // replace brother by a new one

typedef struct {

    int *nodes_array;
    int nodes_array_size;
    int stage;
    int pos_me;
    int pos_contact;
    int right;
} s_task_merge_t;               // execute a merging of groups

typedef struct {

    int stage;
    int pos_me;
    int pos_contact;
    int right;
    int lead_br;
} s_task_broadcast_merge_t;     // broadcast a merge task

typedef struct {

    int init_height;
} s_task_del_root_t;            // delete the root stage

typedef struct {

    int stage;
    int pos_me;
    int pos_contact;
} s_task_clean_stage_t;         /* clean useless nodes in a stage when a merge
                                   occured in lower ones */

typedef struct {
    int new_id;
} s_task_set_active_t;          // set node state as 'a' (active)

typedef struct {
    int new_id;
} s_task_set_update_t;          // set node state as 'u' (update)

typedef struct {
    int new_id;
    char state;
} s_task_set_state_t;           // set node state as given state

typedef struct {
    int id;
} s_task_is_brother_t;          // check if id is a brother of mine

typedef struct {
    int st;
    int right;
    int cut_pos;
    s_node_rep_t sender;
} s_task_transfer_t;            // transfer some nodes to another group

typedef struct {
    int  stage;
    int  start;
    int  end;
} s_task_del_member_t;          // delete a part of current group during a transfer

typedef struct {
    int        stage;
    node_rep_t bro;
    int        array_size;
    int        right;
} s_task_add_bro_array_t;       // add an array of brothers

typedef struct {
    int stage;
    s_node_rep_t new_node;
    int right;
} s_task_shift_bro_t;           // shift brothers to let a new one come in

typedef struct {
    int stage;
    int new_id;
} s_task_add_bro_t;             // add a brother at a given stage

typedef struct {
    int stage;
    int right;
    int cut_pos;
} s_task_cut_node_t;            // Cut node during a transfer

typedef struct {
    int        stage;
    node_rep_t bro;
    int        array_size;
    int        right;
} s_task_br_add_bro_array_t;    // broadcast an add_bro_array task

typedef struct {
    int stage;
    int pos2repl;
    int new_id;
} s_task_update_upper_stage_t;  // update upper stage after a transfer

typedef struct {
    s_node_rep_t new_node;
} s_task_flag_request_t;        // check if a set update broadcast task is possible

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
    s_task_flag_request_t       flag_request;
};

/**
 * Generic request data type
 */
typedef struct req_data {

    e_task_type_t type;                         // type of request task
    int sender_id;                              // sender id
    char answer_to[MAILBOX_NAME_SIZE];          // mailbox for the answer
    int recipient_id;                           // recipient id
    char sent_to[MAILBOX_NAME_SIZE];            // recipient mailbox
    u_req_args_t args;                          // request args

} s_req_data_t, *req_data_t;

/**
 * Return values
 */
typedef struct {

    s_node_rep_t new_rep;
} s_task_ans_get_rep_t;

typedef struct {

    s_node_rep_t **brothers, new_contact;
    int *bro_index;
    int height;
    e_val_ret_t val_ret;

    // for debug
    struct state contact_state;

    // for DST infos
    int add_stage;
    int nbr_split_stages;
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
    int rep_array_size;
    int stay_id;
} s_task_ans_transfer_t;

typedef struct {

    int id;
} s_task_ans_get_new_contact_t;

/**
 * Generic answer values
 */
typedef union answer {

    char                    fill[48];       // to allow direct assignment
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

    e_task_type_t type;                     // answer type (must match request type)
    int sender_id;                          // sender id
    char sent_to[MAILBOX_NAME_SIZE];        // recipient mailbox
    int recipient_id;                       // recipient id
    u_ans_data_t answer;                    // returned data
    e_task_type_t br_type;                  /* type of broadcasted task
                                               (must match request one) */
};


// utility functions
static void  display_var(node_t me);
static char* routing_table(node_t me);
static void  set_n_store_infos(node_t me);
static void  display_preds(node_t me, char log);
static void  display_rout_table(node_t me, char log);
static void  get_mailbox(int node_id, char* mailbox);
static void  task_free(m_task_t* task);
static int   index_bro(node_t me, int stage, int id);
static int   index_pred(node_t me, int stage, int id);
static e_val_ret_t wait_for_completion(node_t me, int ans_cpt);
static int   tot_msg_number(void);
static void  make_copy_brothers(node_t me,
        s_node_rep_t ***cpy_brothers,
        int **cpy_bro_index);
static void  make_copy_preds(node_t me,
        s_node_rep_t ***cpy_preds,
        int **cpy_pred_index);
static void  make_broadcast_task(node_t me, u_req_args_t args, m_task_t *task);
static s_state_t get_state(node_t me);
static void  set_active(node_t me, int new_id);
static e_val_ret_t set_update(node_t me, int new_id);
static void  set_state(node_t me, int new_id, char active);
static int   check(node_t me);
static void  run_delayed_tasks(node_t me, char c);
static void  node_free(node_t me);
static void  data_ans_free(node_t me, ans_data_t *answer_data);
static void  data_req_free(node_t me, req_data_t *req_data);
static void  elem_free(void* elem_ptr);
static void  display_expected_answers(node_t me, char log);
static void  display_states(node_t me, char mode);
static void  display_cnx_queue(node_t me, char mode);
static int   expected_answers_search(node_t me,
        xbt_dynar_t dynar,
        e_task_type_t type,
        e_task_type_t br_type,
        int recp_id);
static int   state_search(node_t me, char active, int new_id);
static u_ans_data_t is_brother(node_t me, int id);
static int dst_xbt_dynar_search_or_negative(xbt_dynar_t dynar, const void *elem);
static char dst_xbt_dynar_member(xbt_dynar_t dynar, void *elem);
static e_val_ret_t flag_request(node_t me, s_node_rep_t new_node);


// communication functions
static MSG_error_t send_msg_sync(node_t me,
        e_task_type_t type,
        int recipient_id,
        u_req_args_t args,
        ans_data_t *answer_data);
static MSG_error_t send_msg_async(node_t me,
        e_task_type_t type,
        int recipient_id,
        u_req_args_t args);
static MSG_error_t send_ans_sync(node_t me,
        e_task_type_t type,
        int recipient_id,
        u_ans_data_t u_ans_data);
static e_val_ret_t broadcast(node_t me, u_req_args_t args);
static void send_completed(node_t me, e_task_type_t type, int recipient_id);

// core functions
static void         init(node_t me);
static int          join(node_t me, int contact_id, int try);
static u_ans_data_t get_rep(node_t me, int stage, int new_node_id);
static u_ans_data_t connection_request(node_t me, s_node_rep_t new_node, int try);
static void         new_brother_received(node_t me, s_node_rep_t new_node);
static void         split_request(node_t me, int stage_nbr, int new_node_id);
static void         add_stage(node_t me);
static void         add_pred(node_t me, int stage, int id);
static void         del_pred(node_t me, int stage, int pred2del);
static void         del_member(node_t me, int stage, int start, int end);
static void         cut_node(node_t me, int stage, int right, int cut_pos);
static void         add_brother(node_t me, int stage, int id);
static void         insert_bro(node_t me, int stage, int id);
static void         add_bro_array(node_t me, int stage, node_rep_t bro, int array_size, int right);
static void         br_add_bro_array(node_t me, int stage, node_rep_t bro, int array_size, int right);
static void         update_upper_stage(node_t me, int stage, int pos2repl, int new_id);
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
        int right);
static void         broadcast_merge(node_t me,
        int stage,
        int pos_me,
        int pos_contact,
        int right,
        int lead_br);
static void         leave(node_t me);
static u_ans_data_t transfer(node_t me, int st, int right, int cut_pos, s_node_rep_t sender);
static void         replace_bro(node_t me,
        int stage,
        int init_idx,
        int new_id);
static void         shift_bro(node_t me, int stage, s_node_rep_t new_node, int right);
static void         del_root(node_t me, int init_height);
static void         clean_upper_stage(node_t me,
        int stage,
        int pos_me,
        int pos_contact);
static void         merge_request(node_t me);
static void         load_balance(node_t me, int contact_id);
static u_ans_data_t get_new_contact(node_t me);

// process functions
static int          node(int argc, char *argv[]);
static e_val_ret_t handle_task(node_t me, m_task_t* task);

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

    XBT_IN();

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

    XBT_OUT();
}

/**
 * \brief Display me's routing table
 * \param me the current node
 */
static void display_rout_table(node_t me, char log) {

    XBT_IN();

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

    XBT_OUT();
}

/**
 * \brief Gets the mailbox name of a node
 * \param node_id node id
 * \param mailbox pointer to where the mailbox name should be written
 * (there must be enough space)
 */
static void get_mailbox(int node_id, char* mailbox) {

    snprintf(mailbox, MAILBOX_NAME_SIZE, "%d", node_id);
}

/**
 * \brief Frees the memory used by a task. Any attached data have to be freed
 *        before calling this function.
 * \param task pointer to the MSG task to destroy
 */
static void task_free(m_task_t *task) {

    XBT_IN();
    xbt_ex_t ex;

    if (*task == NULL) {

        XBT_VERB("in task_free, task = %p", *task);
    } else {

        TRY {
            XBT_DEBUG("Destroy task");
            MSG_task_destroy(*task);
            *task = NULL;
            XBT_DEBUG("Task destroyed");
        }
        CATCH(ex) {
            *task = NULL;
            xbt_ex_free(ex);
            XBT_DEBUG("Error in task_free");
        }
    }
    XBT_OUT();
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

        XBT_VERB("Node %d not found as a brother of node %d for stage %d",
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
 * \brief Returns the position of '{type, br_type, id}' in given dynar
 *        (either sync or async expected answers)
 * \param me the current node
 * \param dynar the dynar to search into
 * \param type the task type to search
 * \param br_type the braodcasted task type to search
 * \param recp_id the recipient id to search
 * \return the position of found entry (-1 if not found)
 */
static int expected_answers_search(node_t me,
        xbt_dynar_t dynar,
        e_task_type_t type,
        e_task_type_t br_type,
        int recp_id) {

    XBT_IN();

    int pos = -1;
    unsigned int iter = 0;
    recp_rec_t elem;

    xbt_dynar_foreach(dynar, iter, elem) {

        XBT_DEBUG("Node %d dynar traversal: iter = %d - idx = %d --> {type:"
                " '%s | %s' - recipient: %d - data: %p}",
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

    XBT_DEBUG("Node %d: pos = %u", me->self.id, pos);
    XBT_OUT();

    return pos;
}

/**
 * \brief Answers 1 if id is a brother of current node
 * \param me the current node
 * \param id another node's id
 * \return 1 if id is a brother of current node, -1 otherwise
 */
static u_ans_data_t is_brother(node_t me, int id) {

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
 * \brief Search a given object in a dynar
 * \param dynar the dynar to search into
 * \param elem the object to search for
 * \return The index where the object is found. -1 otherwise.
 */
static int dst_xbt_dynar_search_or_negative(xbt_dynar_t dynar, const void *elem) {

    XBT_IN();

    unsigned int iter = 0;
    int idx = -1;
    void *item = NULL;

    xbt_dynar_foreach(dynar, iter, item) {

        if (!memcmp(item, elem, sizeof(*elem)) && idx == -1) {

            idx = iter;
        }
    }
    return idx;

    XBT_OUT();
}

/**
 * \brief Check if an object is found in a dynar.
 * \param dynar the dynar to look in
 * \param elem a pointer to the object to search for
 * \return A boolean value (1 for yes)
 */
static char dst_xbt_dynar_member(xbt_dynar_t dynar, void *elem) {

    XBT_IN();

    int idx = dst_xbt_dynar_search_or_negative(dynar, elem);
    return (idx > -1 ? 1 : 0);

    XBT_OUT();
}

/**
 * \brief Ask leaders if it's possible to broadcast a set_update task.
 * \param me the current node
 * \param new_node the new coming node
 * \return An answer OK or NOK */
static e_val_ret_t flag_request(node_t me, s_node_rep_t new_node) {

    XBT_IN();

    XBT_VERB("Node %d: in flag_request - new_node.id = %d",
            me->self.id,
            new_node.id);

    e_val_ret_t val_ret = OK;
    s_state_t state = get_state(me);

    // me is already flagged => refuse CNX_REQ
    if (state.active == 'u') return UPDATE_NOK;

    node_rep_t cnx_elem = xbt_new0(s_node_rep_t, 1);
    cnx_elem->id = new_node.id;
    get_mailbox(new_node.id, cnx_elem->mailbox);

    display_cnx_queue(me, 'V');

    if (xbt_dynar_is_empty(me->cnx_req_queue)) {

        XBT_VERB("Node %d: cnx_req_queue is empty", me->self.id);

        xbt_dynar_push(me->cnx_req_queue, &cnx_elem);
        val_ret = OK;

        XBT_VERB("Node %d: After push of node %d", me->self.id, new_node.id);
        display_cnx_queue(me, 'V');
    } else {

        int idx = dst_xbt_dynar_search_or_negative(me->cnx_req_queue, cnx_elem);
        if (idx > -1) {

            XBT_VERB("Node %d: new node found !!", me->self.id);
            display_cnx_queue(me, 'V');
            val_ret = (idx == 0 ? OK : UPDATE_NOK);
        } else {

            xbt_dynar_push(me->cnx_req_queue, &cnx_elem);
            XBT_VERB("Node %d: After push NOK of node %d", me->self.id, new_node.id);
            display_cnx_queue(me, 'V');

            val_ret = UPDATE_NOK;
        }
    }

    XBT_VERB("Node %d: val_ret = %s - new node id = %d",
            me->self.id,
            (val_ret == OK ? "OK" : "NOK"),
            new_node.id);

    XBT_OUT();
    return val_ret;
}

/**
 * \brief Wait for the completion of a bunch of async sent tasks
 * \param me the current node
 * \param ans_cpt the number of expected anwswers
 * \return A value to indicate if tasks succeed or not
 */
static e_val_ret_t wait_for_completion(node_t me, int ans_cpt) {

    XBT_IN("\tNode %d: waiting for %d answers",
            me->self.id,
            ans_cpt);

    xbt_assert(ans_cpt >= 0,
            "Node %d: in wait_for_completion() - ans_cpt should be >= 0 : %d",
            me->self.id,
            ans_cpt);

    // inits
    e_val_ret_t ret = OK;
    int dynar_size = (int) xbt_dynar_length(me->expected_answers);

    XBT_DEBUG("Node %d: dynar_size = %d", me->self.id, dynar_size);

    recp_rec_t *elem_ptr = NULL;

    float max_wait = MSG_get_clock() + MAX_WAIT_COMPL;
    //me->comm_received = NULL;
    m_task_t task_received = NULL;
    ans_data_t ans = NULL;
    MSG_error_t res = MSG_OK;
    int k, idx, nok_id;
    s_state_t state;

    // async answers already received ? (from other calls of this function)
    for (idx = dynar_size - 1;
            idx >= dynar_size - ans_cpt; idx--) {

        elem_ptr = xbt_dynar_get_ptr(me->expected_answers, idx);
        if ((*elem_ptr)->recp.id == -1) {

            // check if an UPDATE_NOK has been received
            if ((*elem_ptr)->answer_data != NULL &&
                    ret != UPDATE_NOK) {

                ret = (*elem_ptr)->answer_data->answer.handle.val_ret;
                nok_id = (ret == UPDATE_NOK ?
                        (*elem_ptr)->answer_data->answer.handle.val_ret_id :
                        -1);
            }

            if ((*elem_ptr)->answer_data != NULL) {

                xbt_free((*elem_ptr)->answer_data);
                (*elem_ptr)->answer_data = NULL;
            }

            // yes
            ans_cpt--;

            // delete this entry from expected answers
            xbt_dynar_remove_at(me->expected_answers, idx, NULL);
            dynar_size = (int) xbt_dynar_length(me->expected_answers);
        }
    }

    xbt_assert(ans_cpt >= 0,
            "Node %d: ack reception error",
            me->self.id);

    // waiting loop
    while (MSG_get_clock() <= max_wait && ans_cpt > 0) {

        XBT_VERB("Node %d: Waiting for completion ... - ans_cpt = %d",
                me->self.id,
                ans_cpt);

        display_expected_answers(me, 'V');

        res = MSG_task_receive(&task_received, me->self.mailbox);

        // a communication has occurred
        state = get_state(me);
        XBT_VERB("Node %d: '%c'/%d - End of wait - ans_cpt = %d",
                me->self.id,
                state.active,
                state.new_id,
                ans_cpt);

        //display_expected_answers(me, 'V');

        if (res != MSG_OK) {

            // reception failure
            XBT_ERROR("Node %d: Failed to receive ack answer",
                    me->self.id);

            task_free(&task_received);
        } else {

            // reception success
            // extract data from task
            ans = MSG_task_get_data(task_received);
            req_data_t req = (req_data_t)ans;

            if (!strcmp(MSG_task_get_name(task_received), "ans")) {

                // answer
                XBT_VERB("Node %d: Received task : '%s - %s %s'"
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
                XBT_VERB("Node %d: Received task  : '%s - %s %s'"
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

            if (!strcmp(MSG_task_get_name(task_received), "ans")) {

                /* the received task is an answer */

                // is it an async expected answer ?
                int dynar_idx = -1;
                dynar_idx = expected_answers_search(me,
                        me->expected_answers,
                        ans->type,
                        ans->br_type,
                        ans->sender_id);

                XBT_VERB("Node %d in wait_for_completion(): record%sfound"
                        " in async answers dynar: idx = %d",
                        me->self.id,
                        (dynar_idx == -1 ? " not " : " "),
                        dynar_idx);

                if (dynar_idx != -1) {

                    // yes. Is it one of the current expected ones ?

                    if (dynar_idx <= dynar_size - 1 &&
                            dynar_idx >= dynar_size - ans_cpt) {

                        // look if a set_update task has failed
                        if (ans->type == TASK_BROADCAST || ans->type == TASK_SET_UPDATE) {
                            if (ret != UPDATE_NOK) {

                                ret = ans->answer.handle.val_ret;

                                nok_id = (ret == UPDATE_NOK ?  ans->answer.handle.val_ret_id : -1);
                            }
                        }

                        /* yes, this answer is one of the current expected
                           acknowledgments */
                        ans_cpt--;

                        // remove this entry from dynar
                        elem_ptr = xbt_dynar_get_ptr(me->expected_answers, dynar_idx);
                        if ((*elem_ptr)->answer_data != NULL) {

                            xbt_free((*elem_ptr)->answer_data);
                            (*elem_ptr)->answer_data = NULL;
                        }

                        xbt_dynar_remove_at(me->expected_answers,
                                dynar_idx,
                                NULL);

                        dynar_size = (int) xbt_dynar_length(me->expected_answers);
                    } else {

                        /* this answer is expected by one of parent calls:
                           just mark the entry as received ... */
                        elem_ptr = xbt_dynar_get_ptr(me->expected_answers,
                                dynar_idx);
                        (*elem_ptr)->recp.id = -1;

                        xbt_assert((*elem_ptr)->answer_data == NULL,
                                "Node %d: in wait_for_completion(): async "
                                "answer_data %d not NULL !",
                                me->self.id,
                                dynar_idx);

                        // ... and record the answer
                        if ((*elem_ptr)->answer_data == NULL) {

                            (*elem_ptr)->answer_data =
                                xbt_new0(s_ans_data_t, 1);
                            (*elem_ptr)->answer_data->answer.handle.val_ret =
                                ans->answer.handle.val_ret;
                            (*elem_ptr)->answer_data->answer.handle.val_ret_id =
                                ans->answer.handle.val_ret_id;
                        }
                    }

                    XBT_VERB("Node %d: answer '%s - %s' received from %d -"
                            " ans_cpt = %d",
                            me->self.id,
                            debug_msg[ans->type],
                            debug_msg[ans->br_type],
                            ans->sender_id,
                            ans_cpt);
                } else {

                    /* this answer might be a sync expected one */

                    // look for corresponding record in dynar
                    int dynar_idx = -1;
                    dynar_idx = expected_answers_search(me,
                            me->sync_answers,
                            ans->type,
                            ans->br_type,
                            ans->sender_id);

                    XBT_VERB("Node %d in wait_for_completion(): record%sfound"
                            " in sync answers dynar: idx = %d",
                            me->self.id,
                            (dynar_idx == -1 ? " not " : " "),
                            dynar_idx);

                    if (dynar_idx > -1) {

                        // matching entry found
                        elem_ptr =
                            xbt_dynar_get_ptr(me->sync_answers, dynar_idx);

                        xbt_assert((*elem_ptr)->answer_data == NULL,
                                "Node %d: in wait_for_completion(): sync"
                                " answer_data %d not null",
                                me->self.id,
                                dynar_idx);

                        // record answer
                        (*elem_ptr)->answer_data = xbt_new0(s_ans_data_t, 1);
                        *((*elem_ptr)->answer_data) = *ans;

                        XBT_DEBUG("Node %d: other sync answer received."
                                " recorded in %d - length = %lu",
                                me->self.id,
                                dynar_idx,
                                xbt_dynar_length(me->sync_answers));
                    } else {

                        XBT_DEBUG("Node %d: not found in dynar. Ignore it",
                                me->self.id);
                    }
                }

                // answer has been processed. discard task and data
                data_ans_free(me, &ans);
                task_free(&task_received);
            } else {

                /* the received task is a request */
                XBT_VERB("Node %d: this is not an answer",
                        me->self.id);

                /*NOTE: il ne semble pas nécessaire de récupérer la valeur de
                  retour de handle_task() ici */

                if (xbt_dynar_is_empty(me->remain_tasks) == 0 &&
                    req->type == TASK_CNX_REQ) {

                    xbt_dynar_push(me->remain_tasks, &task_received);
                    task_received = NULL;
                } else {

                    // handle the received task
                    handle_task(me, &task_received);
                }
                ans = NULL;

                /* NOTE: ne pas détruire les data ici.
                   Si la tâche traitée envoie un message, c'est le
                   destinataire qui doit détruire ces données */

                XBT_DEBUG("Node %d: back to wait_for_completion()."
                        " Answers received meanwhile ?",
                        me->self.id);

                // async answers received meanwhile ?
                dynar_size = (int) xbt_dynar_length(me->expected_answers);
                for (idx = dynar_size - 1;
                        idx >= dynar_size - ans_cpt; idx--) {

                    elem_ptr = xbt_dynar_get_ptr(me->expected_answers, idx);
                    if ((*elem_ptr)->recp.id == -1) {

                        // check if an UPDATE_NOK has been received
                        if ((*elem_ptr)->answer_data != NULL &&
                                ret != UPDATE_NOK) {

                            ret = (*elem_ptr)->answer_data->answer.handle.val_ret;
                            nok_id = (ret == UPDATE_NOK ?
                                    (*elem_ptr)->answer_data->answer.handle.val_ret_id :
                                    -1);
                        }

                        if ((*elem_ptr)->answer_data != NULL) {

                            xbt_free((*elem_ptr)->answer_data);
                            (*elem_ptr)->answer_data = NULL;
                        }

                        // yes
                        ans_cpt--;

                        // delete this entry from expected answers
                        xbt_dynar_remove_at(me->expected_answers,
                                idx,
                                NULL);
                        dynar_size = (int) xbt_dynar_length(me->expected_answers);
                    }
                }

                xbt_assert(ans_cpt >= 0,
                        "Node %d: ack reception error",
                        me->self.id);
            } // task_received is a request
        } // task reception OK

        xbt_assert(ans == NULL && task_received == NULL,
                "Node %d in wait_for_completion(): ans and task_received"
                " should be NULL here ! ans = %p - task_received = %p",
                me->self.id,
                ans,
                task_received);
    } // reception loop

    // Error if max_wait reached
    if (ans_cpt > 0) {

        display_expected_answers(me, 'V');
    }
    xbt_assert(ans_cpt == 0, "Node %d: Wait error - cpt = %d",
            me->self.id,
            ans_cpt);

    XBT_VERB("Node %d: wait completed\n",
            me->self.id);

    display_expected_answers(me, 'D');

    XBT_DEBUG("Node %d - end of wait_for_completion(): val_ret = '%s' -"
            " nok_id = %d",
            me->self.id,
            (ret == UPDATE_NOK ? "UPDATE_NOK" : "OK"),
            (ret == UPDATE_NOK ? nok_id : -1));

    XBT_OUT();
    return ret;
}

/**
 * \brief Returns the total number of messages exchanged so far
 * \return The total number of messages
 */
static int tot_msg_number(void) {

    int i, tot = 0;

    for (i = 0; i < TYPE_NBR; i++) {
        tot += nb_messages[i];
    }
    return tot;
}

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
 * \brief Make a broadcast task to be handled by handle_task
 * \param me the current node
 * \param args arguments needed by the request to be broadcasted
 * \param task the created task
 */
static void make_broadcast_task(node_t me, u_req_args_t args, m_task_t *task) {

    XBT_IN();

    //args.broadcast.source_id = me->self.id;

    req_data_t req_data = xbt_new0(s_req_data_t, 1);
    req_data->type = TASK_BROADCAST;
    req_data->sender_id = me->self.id;
    req_data->recipient_id = me->self.id;

    get_mailbox(me->self.id, req_data->answer_to);
    get_mailbox(me->self.id, req_data->sent_to);

    req_data->args = args;

    (*task) = MSG_task_create(NULL, COMP_SIZE, COMM_SIZE, req_data);
    MSG_task_set_name((*task), "async");

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

    XBT_IN();
    unsigned int len = xbt_dynar_length(me->states);
    s_state_t state;

    /*
       XBT_DEBUG("Node %d: Dynar states length: %u",
       me->self.id,
       len);
       */

    if (len == 0) {

        state.active = ' ';
        state.new_id = -1;
    } else {

        state_t *state_ptr = NULL;
        state_ptr = xbt_dynar_get_ptr(me->states, len - 1);

        state.active = (*state_ptr)->active;
        state.new_id = (*state_ptr)->new_id;
    }

    XBT_OUT();
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

    int idx = -1;
    unsigned int iter = 0;
    recp_rec_t elem;

    // eventually remove new node from queue
    node_rep_t cnx_elem = xbt_new0(s_node_rep_t, 1);
    cnx_elem->id = new_id;
    get_mailbox(new_id, cnx_elem->mailbox);
    int idx_new = dst_xbt_dynar_search_or_negative(me->cnx_req_queue, cnx_elem);

    XBT_VERB("Node %d: idx_new = %d remove new node = %d",
            me->self.id,
            idx_new,
            cnx_elem->id);

    if (idx_new > -1) {

        xbt_dynar_remove_at(me->cnx_req_queue, idx_new, NULL);
    }
    xbt_free(cnx_elem);

    XBT_VERB("Node %d: After removal of node %d", me->self.id, new_id);
    display_cnx_queue(me, 'V');

    //TODO : on ne fait un Set_active que s'il n'y en a plus en attente
    xbt_dynar_foreach(me->expected_answers, iter, elem){

        if (elem->type == TASK_BROADCAST &&
            elem->br_type == TASK_SET_ACTIVE &&
            elem->answer_data == NULL) {

            idx = iter;
        }
    }

    // get current state
    if (idx == -1  &&
        ((state.active == 'u' && (state.new_id == new_id || state.new_id == -1)) ||
             (state.active == 'l' && state.new_id == new_id) ||
             (state.active != 'u' && state.active != 'l'))) {

        xbt_dynar_reset(me->states);
        set_state(me, new_id, 'a');

        state = get_state(me);
    } else {

        //TODO : faut-il plutôt stocker cette requête ?

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
 * \return A value that indicate if set_update succeded
 */

//TODO: lorsque l'état courant est p, il ne faudrait pas refuser le set_update
//      mais soit le stocker, soit le laisser passer, soit répondre OK si le
//      set_update se trouve déjà dans la pile d'états.

static e_val_ret_t set_update(node_t me, int new_id) {

    XBT_IN();

    s_state_t state = get_state(me);

    e_val_ret_t val_ret;

    if ((state.active == 'a') ||
            (state.active == 'g') ||
            (state.active == 'u' && state.new_id == new_id)) {

        // push new state 'u'
        set_state(me, new_id, 'u');
        val_ret = UPDATE_OK;
    } else {

        if (state.active == 'l') {

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
    }

    state = get_state(me);
    XBT_DEBUG("Node %d: '%c'/%d - end of set_update() ... - new_node = %d",
            me->self.id,
            state.active,
            state.new_id,
            new_id);

    /*
       XBT_VERB("Node %d end of set_update(): val_ret = '%s' - active = '%c'"
       " - new_id = %d",
       me->self.id,
       (val_ret == UPDATE_NOK ?  "UPDATE_NOK" : "UPDATE_OK"),
       state.active,
       state.new_id);
       */

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

    XBT_IN();

    /*
       XBT_DEBUG("Node %d: (before set) Dynar states length: %u",
       me->self.id,
       xbt_dynar_length(me->states));
       */

    s_state_t state = get_state(me);
    if (state.active != active || state.new_id != new_id) {

        state_t state_ptr = xbt_new0(s_state_t, 1);
        state_ptr->active = active;
        state_ptr->new_id = new_id;
        xbt_dynar_push(me->states, &state_ptr);
    } else {

        XBT_VERB("Node %d: Current state already set. '%c'/%d",
                me->self.id,
                active,
                new_id);
    }

    state = get_state(me);
    XBT_DEBUG("Node %d: '%c'/%d - end of set_state() ... - new_node = %d",
            me->self.id,
            state.active,
            state.new_id,
            new_id);

    /*
       XBT_DEBUG("Node %d: (after set) Dynar states length: %u",
       me->self.id,
       xbt_dynar_length(me->states));
       */

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
 * \brief Run tasks stored for a delayed execution
 * \param me the current node
 */
static void run_delayed_tasks(node_t me, char c) {

    XBT_IN();

    // get current values
    s_state_t state = get_state(me);
    unsigned int nb_elems = xbt_dynar_length(me->remain_tasks);

    // display delayed tasks
    XBT_VERB("Node %d: run %c - '%c'/%d -  remain_tasks length = %u",
            me->self.id,
            c,
            state.active,
            state.new_id,
            nb_elems);

    m_task_t *task_ptr = NULL;
    req_data_t req_data = NULL;
    m_task_t elem;
    int cpt = 0;
    int k = 0;

    for (k = 0; k < nb_elems; k++) {

        task_ptr = xbt_dynar_get_ptr(me->remain_tasks, k);
        req_data = MSG_task_get_data(*task_ptr);
        XBT_VERB("Node %d: task[%d] = {'%s - %s' from %d}",
                me->self.id,
                k,
                MSG_task_get_name(*task_ptr),
                debug_msg[req_data->type],
                req_data->sender_id);
    }

    /* run delayed CNX_GROUPS for new node new_id if current state is 'u'/new_id */
    if (state.active == 'u' &&
        xbt_dynar_is_empty(me->remain_tasks) == 0) {

        u_req_args_t req_args;

        for (cpt = 0; cpt < nb_elems && state.active == 'u'; cpt++) {

            task_ptr = xbt_dynar_get_ptr(me->remain_tasks, cpt);
            req_data = MSG_task_get_data(*task_ptr);

            if (req_data->type == TASK_CNX_GROUPS) {

                req_args = req_data->args;
                if (req_args.cnx_groups.new_node_id == state.new_id) {

                    XBT_INFO("Node %d: run %c - '%c'/%d - run CNX_GROUPS (task[%d])",
                            me->self.id,
                            c,
                            state.active,
                            state.new_id,
                            cpt);

                    // run CNX_GROUPS
                    xbt_dynar_remove_at(me->remain_tasks, cpt, &elem);
                    handle_task(me, &elem);
                    nb_elems = (int) xbt_dynar_length(me->remain_tasks);

                    // state may have been changed by handle_task()
                    state = get_state(me);
                }
            }
        }
    }

    // run delayed tasks
    if (state.active == 'a' &&
            xbt_dynar_is_empty(me->remain_tasks) == 0) {

        if (nb_elems > 0) {

            int nb_cnx_req = 0;
            //m_task_t *task_ptr = NULL;
            //m_task_t elem;
            req_data_t req = NULL;
            //int cpt = 0;
            int mem_nb_elems = 0;

            do {
                for (cpt = 0; cpt < nb_elems && state.active == 'a'; cpt++) {

                    task_ptr = xbt_dynar_get_ptr(me->remain_tasks, cpt);
                    req = MSG_task_get_data(*task_ptr);

                    // process CNX_REQ tasks after all the others
                    if (req->type == TASK_CNX_REQ && nb_cnx_req < nb_elems) {

                        nb_cnx_req++;

                        XBT_VERB("Node %d: inside run %c - '%c'/%d - task[%d] is {'%s - %s' from %d} - nb CNX_REQ = %d",
                                me->self.id,
                                c,
                                state.active,
                                state.new_id,
                                cpt,
                                MSG_task_get_name(*task_ptr),
                                debug_msg[req->type],
                                req->sender_id,
                                nb_cnx_req);

                        continue;
                    }

                    XBT_VERB("Node %d: run %c - '%c'/%d - task[%d] is {'%s - %s' from %d} - nb CNX_REQ = %d",
                            me->self.id,
                            c,
                            state.active,
                            state.new_id,
                            cpt,
                            MSG_task_get_name(*task_ptr),
                            debug_msg[req->type],
                            req->sender_id,
                            nb_cnx_req);

                    //xbt_dynar_remove_at(me->remain_tasks, cpt, &elem);
                    xbt_dynar_remove_at(me->remain_tasks, 0, &elem);

                    req = MSG_task_get_data(elem);

                    if (req->type == TASK_CNX_REQ) {

                        nb_cnx_req--;
                    }

                    // store current length
                    mem_nb_elems = nb_elems;

                    handle_task(me, &elem);
                    req = NULL;
                    state = get_state(me);

                    //dynar may have been modified by handle_task
                    nb_elems = (int) xbt_dynar_length(me->remain_tasks);
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
                }
            } while (nb_cnx_req > 0 && nb_elems > 0 && state.active == 'a');
        }

        state = get_state(me);
        XBT_VERB("Node %d: run %c - '%c'/%d - end of delayed tasks execution. dynar_length = %lu",
                me->self.id,
                c,
                state.active,
                state.new_id,
                xbt_dynar_length(me->remain_tasks));

        // one of the delayed tasks may have stored a task itself
        if (xbt_dynar_length(me->remain_tasks) > 0) {

            run_delayed_tasks(me, 'D');
        }
    }
    XBT_OUT();
}

/**
 * \brief Frees the memory used by a node
 * \param me the current node
 */
static void node_free(node_t me) {

    XBT_IN();

    xbt_dynar_free(&(me->expected_answers));
    xbt_dynar_free(&(me->remain_tasks));
    xbt_dynar_free(&(me->sync_answers));
    xbt_dynar_free(&(me->states));

    /*
    // will be freed with dst_infos dynar
    xbt_free(me->dst_infos.load);
    me->dst_infos.load = NULL;
    */

    xbt_free(me->bro_index);
    me->bro_index = NULL;
    xbt_free(me->pred_index);
    me->pred_index = NULL;

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

        XBT_DEBUG("Node %d: free data '%s' from %d to %d",
                me->self.id,
                debug_msg[(*req_data)->type],
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

/**
 * \brief Display the global array of recipients whose answers are expected
 *        (see wait_for_completion())
 * \param me the current node
 */
/*
   static void display_global_array(node_t me) {

   XBT_IN();

   XBT_DEBUG("Node %d: global array:", me->self.id);

   int i;
   for (i = 0; i < me->recp_size; i++) {
   XBT_DEBUG("elm[%d] = {'%s %s' - %d}",
   i,
   debug_msg[me->recp_array[i].type],
   debug_msg[me->recp_array[i].br_type],
   me->recp_array[i].recp.id);
   }

   XBT_OUT();
   }
   */

static void display_expected_answers(node_t me, char log) {

    XBT_IN();

    unsigned int cpt = 0;
    recp_rec_t elem = NULL;

    switch(log) {

        case 'D':
            XBT_DEBUG("Node %d: dynar of %lu expected answers",
                    me->self.id,
                    xbt_dynar_length(me->expected_answers));
            break;

        case 'V':
            XBT_VERB("Node %d: dynar of %lu expected answers",
                    me->self.id,
                    xbt_dynar_length(me->expected_answers));
            break;

        case 'I':
            XBT_INFO("Node %d: dynar of %lu expected answers",
                    me->self.id,
                    xbt_dynar_length(me->expected_answers));
            break;
    }

    xbt_dynar_foreach(me->expected_answers, cpt, elem) {

        switch(log) {

            case 'D':
                XBT_DEBUG("elem[%d] = {%s | %s - %d-'%s' - %p}",
                        cpt,
                        debug_msg[elem->type],
                        debug_msg[elem->br_type],
                        elem->recp.id,
                        elem->recp.mailbox,
                        elem->answer_data);
                break;

            case 'V':
                XBT_VERB("elem[%d] = {%s | %s - %d-'%s' - %p}",
                        cpt,
                        debug_msg[elem->type],
                        debug_msg[elem->br_type],
                        elem->recp.id,
                        elem->recp.mailbox,
                        elem->answer_data);
                break;

            case 'I':
                XBT_INFO("elem[%d] = {%s | %s - %d-'%s' - %p}",
                        cpt,
                        debug_msg[elem->type],
                        debug_msg[elem->br_type],
                        elem->recp.id,
                        elem->recp.mailbox,
                        elem->answer_data);
                break;
        }

        /*
        // No recp.id can be lower than 0
        xbt_assert(elem->recp.id > -1,
                "Node %d: in display_expected_answers() : elem[%d].recp.id = %d !!",
                me->self.id,
                cpt,
                elem->recp.id);
        */
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

static void  display_cnx_queue(node_t me, char mode) {

    XBT_IN();

    unsigned int iter = 0;
    node_rep_t elem = NULL;

    if (mode == 'I') {

        XBT_INFO("Node %d: cnx_req_queue", me->self.id);
    } else {

        if (mode == 'D') {

            XBT_DEBUG("Node %d: cnx_req_queue", me->self.id);
        } else {

            XBT_VERB("Node %d: cnx_req_queue", me->self.id);
        }
    }

    xbt_dynar_foreach(me->cnx_req_queue, iter, elem) {

        if (mode == 'I') {

            XBT_INFO(" {[%d] --> %d}",
                    iter,
                    elem->id);
        } else {

            if (mode == 'D') {

                XBT_DEBUG(" {[%d] --> %d}",
                        iter,
                        elem->id);
            } else {

                XBT_VERB(" {[%d] --> %d}",
                        iter,
                        elem->id);
            }
        }
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
static MSG_error_t send_msg_sync(node_t me,
        e_task_type_t type,
        int recipient_id,
        u_req_args_t args,
        ans_data_t *answer_data) {

    XBT_IN();

    req_data_t req_data = xbt_new0(s_req_data_t, 1);
    req_data_t cpy_req_data = xbt_new0(s_req_data_t, 1);

    //XBT_DEBUG("Start send_msg_sync - req_data = %p", req_data);
    //XBT_DEBUG("Start send_msg_sync - cpy_req_data = %p", cpy_req_data);

    // init request datas
    req_data->type = type;
    req_data->sender_id = me->self.id;
    req_data->recipient_id = recipient_id;
    req_data->args = args;

    // create mailboxes
    get_mailbox(req_data->recipient_id, req_data->sent_to);
    get_mailbox(req_data->sender_id, req_data->answer_to);

    // req_data may be altered during loops
    *cpy_req_data = *req_data;

    // create and send task with data
    m_task_t task_sent = MSG_task_create(NULL, COMP_SIZE, COMM_SIZE, req_data);
    MSG_task_set_name(task_sent, "sync");

    XBT_VERB("Node %d: Sending sync '%s' to %d",
            req_data->sender_id,
            debug_msg[req_data->type],
            req_data->recipient_id);

    // async best effort send
    //MSG_task_isend(task_sent, req_data->sent_to);
    XBT_DEBUG("Before dsend: sent_to = %s", req_data->sent_to);

    MSG_task_dsend(task_sent, req_data->sent_to, NULL);

    XBT_VERB("Node %d: '%s - %s' Request to node %d sent",
            req_data->sender_id,
            debug_msg[req_data->type],
            (req_data->type == TASK_BROADCAST ?
             debug_msg[req_data->args.broadcast.type] : ""),
            req_data->recipient_id);

    // push onto sync_answers dynar
    recp_rec_t req_elem = xbt_new0(s_recp_rec_t, 1);
    recp_rec_t ans_elem = NULL;
    recp_rec_t *elem_ptr = NULL;

    req_elem->type = type;
    req_elem->recp.id = recipient_id;
    get_mailbox(recipient_id, req_elem->recp.mailbox);
    req_elem->br_type =
        (type == TASK_BROADCAST ? args.broadcast.type : TASK_NULL);
    req_elem->answer_data = NULL;
    xbt_dynar_push(me->sync_answers, &req_elem);

    XBT_DEBUG("Node %d: {type = '%s - %s' - recipient = %d - answer_data = %p}"
            " pushed, dynar length = %lu",
            me->self.id,
            debug_msg[req_elem->type],
            debug_msg[req_elem->br_type],
            req_elem->recp.id,
            req_elem->answer_data,
            xbt_dynar_length(me->sync_answers));

    // count messages
    nb_messages[req_data->type]++;
    if (req_data->type == TASK_BROADCAST) {

        nb_br_messages[req_data->args.broadcast.type]++;
    }

    // reception loop to get the answer back
    m_task_t task_received = NULL;
    ans_data_t ans = NULL;
    MSG_error_t res = MSG_OK;
    int dynar_idx;
    int idx;
    //double timeout = MSG_get_clock();
    s_state_t state;

    //while (MSG_get_clock() - timeout < MAX_WAIT_GET_REP ||
    while (res != MSG_TIMEOUT ||
            cpy_req_data->type != TASK_GET_REP) {

        dynar_idx = -1;

        // sync reception
        state = get_state(me);
        XBT_VERB("Node %d: '%c'/%d - Waiting for sync answer ...",
                cpy_req_data->sender_id,
                state.active,
                state.new_id);

        if (cpy_req_data->type == TASK_GET_REP) {

            res = MSG_task_receive_with_timeout(&task_received, cpy_req_data->answer_to, MAX_WAIT_GET_REP);
            //res = MSG_task_receive(&task_received, cpy_req_data->answer_to);
        } else {

            res = MSG_task_receive(&task_received, cpy_req_data->answer_to);
        }

        if (res != MSG_OK) {

            // reception failure
            XBT_ERROR("Node %d: Failed to receive the answer to my '%s' request from %s"
                    " result : %d",
                    cpy_req_data->sender_id,
                    debug_msg[cpy_req_data->type],
                    req_data->sent_to,
                    res);

            task_free(&task_received);
        } else {

            // reception success
            // extract data from task
            ans = MSG_task_get_data(task_received);
            req_data_t req = (req_data_t)ans;

            // get answer data
            *answer_data = ans;

            if (!strcmp(MSG_task_get_name(task_received), "ans")) {

                XBT_VERB("Node %d: Received task (data %p) : '%s - %s %s'"
                        " from %d to %d -> %s",
                        me->self.id,
                        ans,
                        MSG_task_get_name(task_received),
                        debug_msg[ans->type],
                        debug_msg[ans->br_type],
                        ans->sender_id,
                        ans->recipient_id,
                        ans->sent_to);
            } else {

                XBT_VERB("Node %d: Received task (data %p) : '%s - %s %s'"
                        " from %d to %d -> %s",
                        me->self.id,
                        req,
                        MSG_task_get_name(task_received),
                        debug_msg[req->type],
                        debug_msg[(req->type == TASK_BROADCAST ?
                            req->args.broadcast.type : TASK_NULL)],
                        req->sender_id,
                        req->recipient_id,
                        req->sent_to);
            }

            if (strcmp(MSG_task_get_name(task_received), "ans") != 0) {

                /* the received task is a request */
                XBT_VERB("Node %d: This is not the expected answer"
                        " {'%s' from %d}, it's a '%s' request from %d",
                        me->self.id,
                        debug_msg[cpy_req_data->type],
                        cpy_req_data->recipient_id,
                        debug_msg[req->type],
                        req->sender_id);

                if (xbt_dynar_is_empty(me->remain_tasks) == 0 &&
                    req->type == TASK_CNX_REQ) {

                    xbt_dynar_push(me->remain_tasks, &task_received);
                    task_received = NULL;
                } else {

                    // handle this received request
                    handle_task(me, &task_received);
                }
                ans = NULL;
                req = NULL;

                /* NOTE: ne pas détruire les data ici.
                   Si la tâche traitée envoie une réponse, c'est le
                   destinataire qui doit détruire ces données */

                // look if the expected answer has been received meanwhile
                XBT_VERB("Node %d: back to send_sync(). Answer received"
                        " meanwhile? - dynar length = %lu",
                        me->self.id,
                        xbt_dynar_length(me->sync_answers));

                // if it exists, my answer is at the top of dynar
                elem_ptr = xbt_dynar_get_ptr(
                        me->sync_answers,
                        xbt_dynar_length(me->sync_answers) - 1);

                XBT_DEBUG("Node %d top dynar: {type: '%s - %s' - recipient: %d"
                        " - data: %p}",
                        me->self.id,
                        debug_msg[(*elem_ptr)->type],
                        debug_msg[(*elem_ptr)->br_type],
                        (*elem_ptr)->recp.id,
                        (*elem_ptr)->answer_data);

                if ((*elem_ptr)->answer_data != NULL) {

                    // the expected answer has been received
                    *answer_data = (*elem_ptr)->answer_data;

                    // pop top record from dynar since it's now useless
                    xbt_dynar_pop(me->sync_answers, &ans_elem);
                    xbt_free(ans_elem);     //TODO: s'assurer que ça ne libère pas answer_data

                    XBT_VERB("Node %d: Yes. dynar length = %lu",
                            me->self.id,
                            xbt_dynar_length(me->sync_answers));

                    // run delayed tasks
                    run_delayed_tasks(me, '1');
                    break;
                } else {

                    // the answer has not been received
                    XBT_VERB("Node %d: No. dynar length = %lu",
                            me->self.id,
                            xbt_dynar_length(me->sync_answers));
                }
            } else {

                // the received task is an answer
                XBT_DEBUG("Node %d: the received task is an answer",
                        me->self.id);

                // look for corresponding record in dynar
                dynar_idx = expected_answers_search(me,
                        me->sync_answers,
                        ans->type,
                        ans->br_type,
                        ans->sender_id);

                XBT_DEBUG("Node %d in send_msg_sync(): record found in dynar ?"
                        " %s: idx = %d",
                        me->self.id,
                        (dynar_idx == -1 ? "No" : "Yes"),
                        dynar_idx);

                if (dynar_idx == (int)xbt_dynar_length(me->sync_answers) - 1) {

                    // this is the expected answer
                    state = get_state(me);
                    XBT_DEBUG("Node %d: This is the expected answer -"
                            " active = '%c'",
                            me->self.id,
                            state.active);

                    // pop the answer from dynar sync_answers
                    xbt_dynar_pop(me->sync_answers, &ans_elem);

                    //data_ans_free(me, ans_elem->answer_data);
                    if (ans_elem->answer_data != NULL) {

                        xbt_free(ans_elem->answer_data);
                        ans_elem->answer_data = NULL;
                    }
                    xbt_free(ans_elem);
                    task_free(&task_received);      // NOTE: ne pas libérer les data ici

                    XBT_DEBUG("Node %d: answer received. dynar has been poped:"
                            " length = %lu",
                            me->self.id,
                            xbt_dynar_length(me->sync_answers));

                    // run delayed tasks
                    run_delayed_tasks(me, '2');
                    break;
                } else {

                    /* this is not the expected answer.
                       Record its data in dynar sync_answers ? */
                    if (dynar_idx > -1) {

                        XBT_VERB("Node %d: This is not the expected answer -"
                                " req_type = %s, ans_type = %s - sent to %d,"
                                " answer from %d",
                                me->self.id,
                                debug_msg[cpy_req_data->type],
                                debug_msg[ans->type],
                                cpy_req_data->recipient_id,
                                ans->sender_id);

                        elem_ptr = xbt_dynar_get_ptr(me->sync_answers, dynar_idx);

                        xbt_assert((*elem_ptr)->answer_data == NULL,
                                "Node %d in send_msg_sync(): dynar rec %d"
                                " data not null",
                                me->self.id,
                                dynar_idx);

                        // record answer
                        (*elem_ptr)->answer_data = xbt_new0(s_ans_data_t, 1);
                        *((*elem_ptr)->answer_data) = *ans;

                        XBT_DEBUG("Node %d: other sync answer received. recorded"
                                " in %d - length = %lu",
                                me->self.id,
                                dynar_idx,
                                xbt_dynar_length(me->sync_answers));

                        // get ready for a new loop
                        data_ans_free(me, &ans);
                        task_free(&task_received);
                    } else {

                        // is it an async expected answer ?
                        dynar_idx = expected_answers_search(me,
                                me->expected_answers,
                                ans->type,
                                ans->br_type,
                                ans->sender_id);
                        if (dynar_idx != -1) {

                            /* this task is one of the expected acknowledgments:
                               mark it as received ... */
                            elem_ptr = xbt_dynar_get_ptr(me->expected_answers,
                                    dynar_idx);
                            (*elem_ptr)->recp.id = -1;

                            xbt_assert((*elem_ptr)->answer_data == NULL,
                                    "Node %d: rec answer_data not NULL !",
                                    me->self.id);

                            // ... and record the answer
                            if ((*elem_ptr)->answer_data == NULL) {

                                (*elem_ptr)->answer_data =
                                    xbt_new0(s_ans_data_t, 1);
                                (*elem_ptr)->answer_data->answer.handle.val_ret =
                                    ans->answer.handle.val_ret;
                                (*elem_ptr)->answer_data->answer.handle.val_ret_id =
                                    ans->answer.handle.val_ret_id;
                            }

                            XBT_VERB("Node %d: async answer '%s - %s' received from %d"
                                    " - dynar_size = %lu - idx = %d",
                                    me->self.id,
                                    debug_msg[ans->type],
                                    debug_msg[ans->br_type],
                                    ans->sender_id,
                                    xbt_dynar_length(me->expected_answers),
                                    dynar_idx);

                        }
                        // get prepared for a new loop
                        data_ans_free(me, &ans);
                        task_free(&task_received);

                    } // task_received is not a sync expected answer
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

        if (xbt_dynar_is_empty(me->sync_answers) == 0) {

            // if waiting for GET_REP answer is canceled, then pop sync_answers
            elem_ptr = xbt_dynar_get_ptr(me->sync_answers,
                    xbt_dynar_length(me->sync_answers) - 1);
            if ((*elem_ptr)->type == TASK_GET_REP) {

                xbt_dynar_pop(me->sync_answers, &ans_elem);

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

    xbt_free(cpy_req_data);
    cpy_req_data = NULL;

    XBT_OUT();
    return res;
}

/**
 * \brief Sends a request task to another node
 * \param me the current node
 * \param type type of sent task
 * \param recipient_id id of the recipient
 * \param args args needed by this type of task
 * \return Communication error code
 */
static MSG_error_t send_msg_async(node_t me,
        e_task_type_t type,
        int recipient_id,
        u_req_args_t args) {

    XBT_IN();

    req_data_t req_data = xbt_new0(s_req_data_t, 1);

    // init request data
    req_data->type = type;
    req_data->sender_id = me->self.id;
    req_data->recipient_id = recipient_id;
    req_data->args = args;

    // create mailboxes
    get_mailbox(req_data->recipient_id, req_data->sent_to);
    get_mailbox(req_data->sender_id, req_data->answer_to);

    // create and send task with data
    m_task_t task_sent = MSG_task_create(NULL, COMP_SIZE, COMM_SIZE, req_data);
    MSG_task_set_name(task_sent, "async");

    XBT_VERB("Node %d: Sending async '%s' to %d-%s",
            req_data->sender_id,
            debug_msg[req_data->type],
            req_data->recipient_id,
            req_data->sent_to);

    //MSG_task_isend(task_sent, req_data->sent_to);
    // best effort send
    MSG_task_dsend(task_sent, req_data->sent_to, NULL);

    // count messages
    nb_messages[req_data->type]++;
    if (req_data->type == TASK_BROADCAST) {

        nb_br_messages[req_data->args.broadcast.type]++;
    }

    XBT_OUT();
    return MSG_OK;
}

/**
 * \brief Sends an answer back to a requesting node
 * \param me the current node
 * \param type type of sent task (must match the type of request that is answered)
 * \param recipient_id id of the recipient
 * \param answer_data data answered back
 */
static MSG_error_t send_ans_sync(node_t me,
        e_task_type_t type,
        int recipient_id,
        u_ans_data_t u_ans_data) {

    XBT_IN();

    ans_data_t ans_data = xbt_new0(s_ans_data_t, 1);

    // init answer data
    ans_data->type = type;
    ans_data->br_type = (type == TASK_BROADCAST ?
            u_ans_data.handle.br_type : TASK_NULL);
    ans_data->sender_id = me->self.id;
    ans_data->recipient_id = recipient_id;
    ans_data->answer = u_ans_data;

    // create mailbox
    get_mailbox(ans_data->recipient_id, ans_data->sent_to);

    // create and send task with answer data
    m_task_t task_sent = MSG_task_create(NULL, COMP_SIZE, COMM_SIZE, ans_data);
    MSG_task_set_name(task_sent, "ans");

    XBT_VERB("Node %d: Answering '%s - %s' to %d - '%s'",
            ans_data->sender_id,
            debug_msg[ans_data->type],
            debug_msg[ans_data->br_type],
            ans_data->recipient_id,
            ans_data->sent_to);

    //MSG_error_t res = MSG_task_send(task_sent, ans_data->sent_to);
    //MSG_task_isend(task_sent, ans_data->sent_to);
    // best effort send
    MSG_task_dsend(task_sent, ans_data->sent_to, NULL);

    /*
       if (res != MSG_OK) {

    // send failure
    task_free(&task_sent);

    XBT_ERROR("Node %d: Failed to send '%s' answer to %d",
    ans_data->sender_id,
    debug_msg[ans_data->type],
    ans_data->recipient_id);

    if (res == MSG_TIMEOUT) {XBT_VERB("Timeout");}
    } else {

    nb_messages[type]++;
    }
    */

    MSG_error_t res = MSG_OK;
    xbt_assert(res == MSG_OK, "Node %d: Failed to answer to '%s' from %d",
            me->self.id,
            debug_msg[type],
            recipient_id);

    nb_messages[type]++;
    if (type == TASK_BROADCAST) {

        nb_br_messages[ans_data->br_type]++;
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

    e_val_ret_t ret = OK;

    /*
       XBT_VERB("Node %d: In broadcast - Broadcast stage = %d, bro_idx = %d",
       me->self.id,
       args.broadcast.stage,
       me->bro_index[args.broadcast.stage]);
       */

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

    m_task_t task_sent = NULL;

    if (args.broadcast.stage > 0 ||
            (args.broadcast.stage == 0 && args.broadcast.lead_br == 0)) {

        for (brother = 0; brother < nb_bro; brother++) {

            XBT_VERB("Node %d: In broadcast: brother = %d, Broadcast stage = %d",
                    me->self.id,
                    cpy_brothers[brother].id,
                    args.broadcast.stage);

            if (cpy_brothers[brother].id == me->self.id) {

                /* local direct call (no expected answer)
                   to be handled at the end of function (see below) */
                ans_cpt--;
                make_broadcast_task(me, args, &task_sent);

            } else {

                // remote call
                MSG_error_t res = send_msg_async(me,
                        TASK_BROADCAST,
                        cpy_brothers[brother].id,
                        args);
                xbt_assert(res == MSG_OK, "Node %d: Broadcast error",
                        me->self.id);

                // record recipient in expected answers dynar
                recp_rec_t elem = xbt_new0(s_recp_rec_t, 1);

                elem->type = TASK_BROADCAST;
                elem->recp = cpy_brothers[brother];
                elem->br_type = args.broadcast.type;
                elem->answer_data = NULL;

                xbt_assert(elem->recp.id > - 1,
                        "Node %d: #1# recp.id is %d !!",
                        me->self.id,
                        elem->recp.id);

                xbt_dynar_push(me->expected_answers, &elem);
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
            // remote call
            MSG_error_t res = send_msg_async(me,
                    TASK_BROADCAST,
                    cpy_brothers[0].id,
                    args);

            xbt_assert(res == MSG_OK, "Node %d: Broadcast error 2",
                    me->self.id);

            // record recipient in expected answers dynar
            recp_rec_t elem = xbt_new0(s_recp_rec_t, 1);

            elem->type = TASK_BROADCAST;
            elem->recp = cpy_brothers[0];
            elem->br_type = args.broadcast.type;
            elem->answer_data = NULL;

            xbt_assert(elem->recp.id > - 1,
                    "Node %d: #1# recp.id is %d !!",
                    me->self.id,
                    elem->recp.id);

            xbt_dynar_push(me->expected_answers, &elem);
        }
    }

    // synchronize each stage
    if (ans_cpt > 0) {

        XBT_DEBUG("Node %d in broadcast(): before wait_for_completion",
                me->self.id);
        //display_expected_answers(me, 'V');

        ret = wait_for_completion(me, ans_cpt);
        //xbt_free(recp_array);

        //xbt_free(elem);
    }

    // check if a set_update task has failed
    if (ret == UPDATE_NOK) {

        XBT_DEBUG("Node %d: a set_update task has failed",
                me->self.id);
    }

    // Handle local task only if no set_update has failed.
    /*
       xbt_assert(args.broadcast.type != TASK_CLEAN_STAGE || me->self.id != 26,
       "Node %d: stage = %d - type = '%s' - source = %d",
       me->self.id,
       args.broadcast.stage,
       debug_msg[args.broadcast.type],
       args.broadcast.source_id);
       */

    if (!(args.broadcast.type == TASK_CLEAN_STAGE &&
                args.broadcast.stage == 0 &&
                me->self.id == args.broadcast.source_id)) {
        if (task_sent != NULL && ret != UPDATE_NOK) {

            ret = handle_task(me, &task_sent);
        }
    } else {

        /* If a clean_stage is broadcasted, the source branch mustn't locally
         * run this task since it has already been run before launching
         * broadcast */

        XBT_VERB("Node %d: DON'T run TASK_CLEAN_STAGE - source_id = %d",
                me->self.id,
                args.broadcast.source_id);
        /*
           xbt_assert(1 == 0,
           "Node %d: stage = %d - type = '%s' - source = %d",
           me->self.id,
           args.broadcast.stage,
           debug_msg[args.broadcast.type],
           args.broadcast.source_id);
        */
    }

    /* NOTE : ces libérations sont faites dans handle_task()
    req_data_t req = MSG_task_get_data(task_sent);
    data_req_free(me, &req);
    task_free(&task_sent);
    */

    xbt_free(cpy_brothers);
    cpy_brothers = NULL;
    XBT_OUT();
    return ret;
}

/**
 * \brief Send a 'completed task' message back (acknowledgement)
 * \param me the current node
 * \param type the type of message to send back
 *             (must match the one that's acknowledged)
 * \param recipient_id the recipient of this message
 */
static void send_completed(node_t me, e_task_type_t type, int recipient_id) {

    XBT_IN();

    u_ans_data_t answer;
    answer.handle.val_ret = OK;

    send_ans_sync(me, type, recipient_id, answer);

    XBT_VERB("Node %d: '%s Completed' message sent to %d",
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

    //xbt_assert(1 == 0, "sizeof(u_ans_data_t) = %ld", sizeof(u_ans_data_t));
    //xbt_assert(1 == 0, "sizeof(u_req_args_t) = %ld", sizeof(u_req_args_t));

    me->height = 1;
    me->comm_received = NULL;

    // set current state
    me->states = xbt_dynar_new_sync(sizeof(state_t), &xbt_free_ref);
    set_state(me, me->self.id, 'b');    // building in progress

    //me->recp_array = NULL;
    //me->recp_size = 0;
    me->expected_answers = xbt_dynar_new_sync(sizeof(recp_rec_t), &xbt_free_ref);
    me->remain_tasks = xbt_dynar_new_sync(sizeof(m_task_t), &xbt_free_ref);
    me->sync_answers = xbt_dynar_new_sync(sizeof(recp_rec_t), &xbt_free_ref);
    me->cnx_req_queue = xbt_dynar_new_sync(sizeof(node_rep_t), &xbt_free_ref);


    // DST infos initialization
    me->dst_infos.order = 0;
    me->dst_infos.node_id = me->self.id;
    me->dst_infos.routing_table = NULL;
    me->dst_infos.nb_messages = 0;
    me->dst_infos.add_stage = 0;
    me->dst_infos.nbr_split_stages = 0;
    me->dst_infos.load = xbt_new0(int, me->height);
    me->dst_infos.size = NULL;

    // routing table initialization
    me->bro_index = xbt_new0(int, me->height);
    me->brothers = xbt_new0(node_rep_t, me->height);

    int i;
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

    display_preds(me, 'D');
    XBT_OUT();
}

/**
 * \brief This function lets a new node join the DST
 * \param me the new node
 * \param contact_id the node 'me' can contact to get into the DST
 * \param try number of joining attempt
 * \return 1 if join succeeded, 0 otherwise
 */
static int join(node_t me, int contact_id, int try) {

    XBT_IN();
    s_state_t state = get_state(me);
    XBT_VERB("Node %d: '%c'/%d - join() ...",
            me->self.id,
            state.active,
            state.new_id);

    // prepare data to exchange
    u_req_args_t u_req_args;
    u_req_args.cnx_req.new_node = me->self;
    u_req_args.cnx_req.try = try;

    //ans_data_t answer_data = xbt_new0(s_ans_data_t, 1);
    ans_data_t answer_data = NULL;

    // send connection request
    MSG_error_t res = send_msg_sync(me,
            TASK_CNX_REQ,
            contact_id,
            u_req_args,
            &answer_data);

    XBT_DEBUG("Node %d: Back to join - val_ret = %d - contact = %d",
            me->self.id,
            answer_data->answer.cnx_req.val_ret,
            answer_data->answer.cnx_req.new_contact.id);

    if (res != MSG_OK || answer_data->answer.cnx_req.val_ret == UPDATE_NOK) {

        // join failure
        XBT_ERROR("Node %d failed to join the DST", me->self.id);
        data_ans_free(me, &answer_data);
        return 0;
    }

    /* get task answer data: new contact and its routing table */

    state = get_state(me);
    XBT_INFO("Node %d: '%c'/%d - **** GETTING INFOS FROM ITS NEW CONTACT %d ... ****",
            me->self.id,
            state.active,
            state.new_id,
            answer_data->answer.cnx_req.new_contact.id);

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
    XBT_INFO("Node %d: '%c'/%d - contact %d has %d stages -"
            " add_stage = %d - nbr_split_stages = %d - its state was '%c'/%d",
            me->self.id,
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
        }
    }

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

    // set DST infos
    me->dst_infos.load[0] = me->pred_index[0];

    // answer_data is not needed anymore
    data_ans_free(me, &answer_data);

    // display the received datas
    display_rout_table(me, 'V');

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

    display_rout_table(me, 'I');
    state = get_state(me);
    XBT_INFO("Node %d: '%c'/%d - **** DONE (after load_balance) ***",
            me->self.id,
            state.active,
            state.new_id);

    me->dst_infos.nb_messages = tot_msg_number() - me->dst_infos.nb_messages + 1;
    XBT_VERB("Node %d: nb_msg = %d", me->self.id, me->dst_infos.nb_messages);

    display_rout_table(me, 'V');

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
    if (state.active == 'u' || state.active == 'g') {

        XBT_OUT();
        return answer;
    }

    // push state 'g' if current node is active
    if (state.active == 'a') {

        set_state(me, new_node_id, 'g');
    }

    XBT_VERB("Node %d: '%c'/%d - get_rep() ... - new_node = ?",
            me->self.id,
            state.active,
            state.new_id);

    display_states(me, 'V');

    int load = me->pred_index[stage];
    int load_f = 0;

    ans_data_t rcv_ans_data = NULL;

    u_req_args_t u_req_args;
    u_req_args.nb_pred.stage = stage;

    int f;
    for (f = 0; f < me->bro_index[0]; f++) {

        if (me->brothers[0][f].id == me->self.id) {

            load_f = me->pred_index[stage];
        } else {
            MSG_error_t res = send_msg_sync(me,
                    TASK_NB_PRED,
                    me->brothers[0][f].id,
                    u_req_args,
                    &rcv_ans_data);

            // TODO : s'assurer qu'on est toujours à l'état 'g' à ce stade

            if (res != MSG_OK) {

                // nb pred failure
                XBT_WARN("Node %d: failed to get the number of predecessors"
                        " for stage %d from node %d",
                        me->self.id,
                        stage, me->brothers[0][f].id);

                // defaults to b in this case   //TODO: vérifier si c'est une bonne idée
                rcv_ans_data->answer.nb_pred.load = b;  //TODO : Attention, pointeur pas initialisé ?
            }
            load_f = rcv_ans_data->answer.nb_pred.load;
            /* load_f will be negative if me->brothers[0][f] is in 'b' state */

            XBT_DEBUG("Node %d: load of brothers[0][%d](%d) = %d",
                    me->self.id,
                    f,
                    me->brothers[0][f].id,
                    load_f);
        }

        if (load_f >= 0 && load_f < load) {
            load = load_f;
            answer.get_rep.new_rep = me->brothers[0][f];
        }

        data_ans_free(me, &rcv_ans_data);
    }

    int idx = state_search(me, 'g', new_node_id);
    if (idx > -1) {

        XBT_DEBUG("Node %d: idx = %d", me->self.id, idx);
        xbt_dynar_remove_at(me->states, idx, NULL);
    }

    XBT_VERB("Node %d: (end get_rep) states length = %lu",
            me->self.id,
            xbt_dynar_length(me->states));
    display_states(me, 'V');

    xbt_assert(xbt_dynar_length(me->states) > 0,
            "Node %d: (in get_rep) dynar states is empty !",
            me->self.id);

    XBT_OUT();
    return answer;
}

/**
 * \brief This function makes room for a new node joining the DST if necessary
 *        and insert it.
 * \param me the current node
 * \param new_node the joining node
 * \param try number of joining attempts
 * \return Answer data containing the contact's routing table
 */
static u_ans_data_t connection_request(node_t me, s_node_rep_t new_node, int try) {

    XBT_IN();

    // node being updated
    set_update(me, new_node.id);

    s_state_t state = get_state(me);
    XBT_VERB("Node %d: '%c'/%d - connection_request() ... - new_node = %d",
            me->self.id,
            state.active,
            state.new_id,
            new_node.id);

    e_val_ret_t val_ret = OK;
    u_ans_data_t answer;

    display_rout_table(me, 'V');

    // to set DST infos
    answer.cnx_req.add_stage = 0;
    node_rep_t cpy_brothers = NULL;

    if (me->self.id != me->brothers[0][0].id) {

        // me isn't the leader: transfer the request to the leader

        state = get_state(me);
        XBT_INFO("Node %d: '%c'/%d -Transfering 'Connection request' to the leader %d",
                me->self.id,
                state.active,
                state.new_id,
                me->brothers[0][0].id);

        u_req_args_t args;
        args.cnx_req.new_node = new_node;
        args.cnx_req.try = try;

        ans_data_t answer_data = NULL;
        MSG_error_t res;

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
        set_active(me, new_node.id);

    } else {

        // me is the leader
        state = get_state(me);
        XBT_INFO("Node %d: '%c'/%d - I am the leader",
                me->self.id,
                state.active,
                state.new_id);

        XBT_DEBUG("Node %d: start try = %d",
                me->self.id,
                try);

        int n = 0;

        while ((n < me->height) && (me->bro_index[n] == b)) {

            // n is the number of stages that have to be splitted
            n++;
        }

        // to set DST infos
        answer.cnx_req.nbr_split_stages = n;

        if (n > 0) {

            state = get_state(me);
            XBT_INFO("Node %d: '%c'/%d - **** MAKING ROOM FOR NODE %d ... ****",
                    me->self.id,
                    state.active,
                    state.new_id,
                    new_node.id);

            // to set DST infos
            if (n == me->height) {
                answer.cnx_req.add_stage = 1;
            }

            u_req_args_t args;
            m_task_t task_sent = NULL;

            // broadcast a flag_request to all concerned leaders
            XBT_VERB("Node %d: broadcast a flag_request to all concerned leaders",
                    me->self.id);

            args.broadcast.type = TASK_FLAG_REQ;

            /* broadcast starts one level higher than highest splitted stage
               because of connect_splitted_groups */
            if (n == me->height) {

                args.broadcast.stage = n - 1;
            } else {

                args.broadcast.stage = n;
            }
            args.broadcast.first_call = 1;
            args.broadcast.source_id = me->self.id;
            args.broadcast.lead_br = 1;

            args.broadcast.args = xbt_new0(u_req_args_t, 1);
            args.broadcast.args->flag_request.new_node = new_node;
            make_broadcast_task(me, args, &task_sent);

            val_ret = handle_task(me, &task_sent);

            xbt_free(args.broadcast.args);
            args.broadcast.args = NULL;

            // continue only if flag_request returned OK
            if (val_ret != UPDATE_NOK) {

                // set all concerned nodes as 'u'
                XBT_VERB("Node %d: set all concerned nodes as 'u'",
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
                args.broadcast.lead_br = 1;

                args.broadcast.args = xbt_new0(u_req_args_t, 1);
                args.broadcast.args->set_update.new_id = new_node.id;
                make_broadcast_task(me, args, &task_sent);

                val_ret = handle_task(me, &task_sent);

                xbt_free(args.broadcast.args);
                args.broadcast.args = NULL;

                /* Set_Update broadcast isn't supposed to fail if flag_request
                   returned OK */
                xbt_assert(val_ret != UPDATE_NOK,
                        "Node %d: Set UPDATE error while making room for node %d",
                        me->self.id,
                        new_node.id);

                    // splits stages
                    int k;
                    for (k = n; k > 0; k--) {

                        split_request(me, k, new_node.id);
                    }

                    XBT_VERB("Node %d: Back to connection request.", me->self.id);

                    // TODO : TROP TOT ?

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
                    args.broadcast.lead_br = 0;             //TODO !! test passer à 1

                    args.broadcast.args = xbt_new0(u_req_args_t, 1);
                    args.broadcast.args->set_active.new_id = new_node.id;
                    make_broadcast_task(me, args, &task_sent);

                    handle_task(me, &task_sent);

                    xbt_free(args.broadcast.args);
                    args.broadcast.args = NULL;

                    state = get_state(me);
                    XBT_INFO("Node %d: '%c'/%d -  **** ROOM MADE FOR NODE %d ****",
                            me->self.id,
                            state.active,
                            state.new_id,
                            new_node.id);
            }
        }

        // continue only if set_update succeeded
        if (val_ret != UPDATE_NOK) {

            set_update(me, new_node.id);    // stay at 'u' until the end

            state = get_state(me);
            XBT_INFO("Node %d: '%c'/%d - **** INSERTING NODE %d ... ****",
                    me->self.id,
                    state.active,
                    state.new_id,
                    new_node.id);

            /* tells every first stage concerned node to let the new node in */

            // works on a copy of first stage brothers
            int cpy_bro_index = me->bro_index[0];
            cpy_brothers = xbt_new0(s_node_rep_t, b);
            int i,j;
            for (i = 0; i < b; i++) {

                cpy_brothers[i] = me->brothers[0][i];
            }

            int cpt = 0;
            recp_rec_t elem = NULL;
            u_req_args_t args;
            MSG_error_t res = MSG_OK;

            for (i = 0; i < cpy_bro_index; i++) {

                if (cpy_brothers[i].id != me->self.id) {

                    /* sends a 'New Brother Receive' task to every brother other
                       than 'me' */
                    args.new_brother_rcv.new_node = new_node;

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
                    elem->answer_data = NULL;
                    xbt_dynar_push(me->expected_answers, &elem);

                    xbt_assert(elem->recp.id > - 1,
                            "Node %d: #2# recp.id is %d !!",
                            me->self.id,
                            elem->recp.id);

                    cpt++;
                } else {

                    // local call for 'me'
                    new_brother_received(me, new_node);
                }
            }

            // synchro (5)
            if (cpt > 0) {

                wait_for_completion(me, cpt);
            }
            //xbt_free(recp_array);

            // answer construction
            answer.cnx_req.new_contact = me->self;
            answer.cnx_req.brothers = me->brothers;
            answer.cnx_req.bro_index = me->bro_index;
            answer.cnx_req.height = me->height;
            s_state_t state = get_state(me);
            answer.cnx_req.contact_state = state;

            // now, me can be active
            set_active(me, new_node.id);

            state = get_state(me);
            XBT_INFO("Node %d: '%c'/%d - **** NODE %d INSERTED ****",
                    me->self.id,
                    state.active,
                    state.new_id,
                    new_node.id);

            /* set every member to 'p' to show that their preds table is about
               to change (when adding the new coming node in it) */

            u_req_args_t u_req_args;
            u_req_args.set_state.new_id = new_node.id;
            u_req_args.set_state.state = 'p';

            for (i = 1; i < me->height; i++) {    // not needed for stage 0
                for (j = 0; j < me->bro_index[i]; j++) {

                    if (me->brothers[i][j].id != me->self.id) {

                        res = send_msg_async(me,
                                TASK_SET_STATE,
                                me->brothers[i][j].id,
                                u_req_args);

                    } else {

                        /* current node doesn't have to change its state. It'll
                           be replaced by the new node in the transmitted table */
                    }
                }
            }

        } else {

            // flag_request returned NOK

            /*
            int idx = state_search(me, 'u', new_node.id);
            if (idx > -1) {

                XBT_DEBUG("Node %d: idx = %d", me->self.id, idx);
                xbt_dynar_remove_at(me->states, idx, NULL);
            }
            */

            state = get_state(me);
            XBT_INFO("Node %d: '%c'/%d - **** FAILED TO MAKE ROOM FOR NODE %d (try = %d) ****",
                    me->self.id,
                    state.active,
                    state.new_id,
                    new_node.id,
                    try);

            /*
               state_t *state_ptr = NULL;
               state_ptr = xbt_dynar_get_ptr(me->states,
               xbt_dynar_length(me->states) - 1);
               (*state_ptr)->active = mem_state.active;
               (*state_ptr)->new_id = mem_state.new_id;
               */

            answer.cnx_req.new_contact.id = -1;

            /*
            if (try >= 2000) {  // broadcast 'a' after 2 attemps to broadcast 'u'

                u_req_args_t args;
                m_task_t task_sent = NULL;

                // reset all concerned nodes as 'a'
                XBT_VERB("Node %d: join abort: reset all concerned nodes as 'a'",
                        me->self.id);

                args.broadcast.type = TASK_SET_ACTIVE;

                   broadcast starts one level higher than highest splitted stage
                   because of connect_splitted_groups
                if (n == me->height) {

                    args.broadcast.stage = n - 1;
                } else {

                    args.broadcast.stage = n;
                }
                args.broadcast.first_call = 1;
                args.broadcast.source_id = me->self.id;
                args.broadcast.lead_br = 0;             //TODO !! test passer à 1

                args.broadcast.args = xbt_new0(u_req_args_t, 1);
                args.broadcast.args->set_active.new_id = new_node.id;
                make_broadcast_task(me, args, &task_sent);

                handle_task(me, &task_sent);

                xbt_free(args.broadcast.args);
                args.broadcast.args = NULL;
            }
            */
        }
        answer.cnx_req.val_ret = val_ret;
    }

    state = get_state(me);
    XBT_INFO("Node %d: '%c'/%d -  end of connection_request() - val_ret = '%s'",
            me->self.id,
            state.active,
            state.new_id,
            (val_ret == UPDATE_NOK ? "UPDATE_NOK" : "OK"));

    XBT_DEBUG("Node %d: answer.cnx_req.add_stage = %d",
            me->self.id,
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
static void new_brother_received(node_t me, s_node_rep_t new_node) {

    XBT_IN();

    add_brother(me, 0, new_node.id);
    add_pred(me, 0, new_node.id);

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
    set_update(me, new_node_id);

    s_state_t state = get_state(me);
    XBT_VERB("Node %d: '%c'/%d - split_request() ... - new_node = %d",
            me->self.id,
            state.active,
            state.new_id,
            new_node_id);

    XBT_VERB("Node %d: In split_request - number of stages to be splitted: %d",
            me->self.id, stage_nbr);

    display_rout_table(me, 'V');

    u_req_args_t broadcast_args;
    ans_data_t answer_data = NULL;
    int stage = stage_nbr - 1;
    int chk = 0;
    int nb_loop = 0;

    //if (1 == 0) {
    //if (me->brothers[stage][0].id != me->self.id) {
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

        MSG_error_t res = send_msg_sync(me,
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

        m_task_t task_sent = NULL;
        if (stage_nbr == me->height) {

            // add a stage to every node since the root has to be splitted
            XBT_VERB("Node %d: add a stage", me->self.id);

            broadcast_args.broadcast.type = TASK_ADD_STAGE;
            broadcast_args.broadcast.stage = me->height - 1;
            broadcast_args.broadcast.first_call = 1;
            broadcast_args.broadcast.source_id = me->self.id;
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
    XBT_DEBUG("Node %d: '%c'/%d - add_pred() ...",
            me->self.id,
            state.active,
            state.new_id);

    xbt_assert(stage < me->height,
            "Node %d: height error - stage = %d - height = %d",
            me->self.id,
            stage,
            me->height);

    // nothing to do (pred already exists)
    if (index_pred(me, stage, id) != -1) return;

    me->preds[stage][me->pred_index[stage]].id = id;
    get_mailbox(id, me->preds[stage][me->pred_index[stage]].mailbox);

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
            get_mailbox(0, me->preds[stage][i].mailbox);
        }

        XBT_VERB("Node %d: Predecessors array has been set bigger", me->self.id);
    }

    /* if current state is 'p', then it's an add_pred coming from load_balance() :
       just pops out this state */

    //TODO: afficher les états ici avant le pop
    state = get_state(me);
    if (state.active == 'p' && me->self.id != state.new_id) {

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
        get_mailbox(-1, me->preds[stage][idx].mailbox);
        me->pred_index[stage]--;

        // set DST infos
        me->dst_infos.load[stage] = me->pred_index[stage];
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

    MSG_error_t res;
    u_req_args_t args;
    args.del_pred.stage = stage;
    args.del_pred.pred2del_id = me->self.id;

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
            get_mailbox(0, me->brothers[stage][j].mailbox);
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
 */
static void cut_node(node_t me, int stage, int right, int cut_pos) {

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
            // TODO : voir s'il ne faut pas plutôt transmettre le new_node depuis le
            // noeud appelant (47 dans l'exemple)
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
                right);
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
// TODO : est-il possible d'y inclure les ADD_PRED ?
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
    get_mailbox(id, me->brothers[stage][me->bro_index[stage]].mailbox);
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
    get_mailbox(id, me->brothers[stage][0].mailbox);
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
 */
static void add_bro_array(node_t me, int stage, node_rep_t bro, int array_size, int right) {
    XBT_IN();

    int i;
    MSG_error_t res;
    u_req_args_t args;

    XBT_VERB("Node %d: in add_bro_array() - stage = %d - size = %d - right = %d",
            me->self.id,
            stage,
            array_size,
            right);

    args.add_pred.stage = stage;
    args.add_pred.new_pred_id = me->self.id;

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
 */
static void br_add_bro_array(node_t me, int stage, node_rep_t bro, int array_size, int right) {

    XBT_IN();

    MSG_error_t res;
    u_req_args_t args;
    m_task_t task_sent = NULL;

    args.broadcast.type = TASK_ADD_BRO_ARRAY;
    args.broadcast.stage = stage;
    args.broadcast.first_call = 1;
    args.broadcast.source_id = me->self.id;
    args.broadcast.lead_br = 0;

    args.broadcast.args = xbt_new0(u_req_args_t, 1);

    args.broadcast.args->add_bro_array.stage = stage;
    args.broadcast.args->add_bro_array.bro = bro;
    args.broadcast.args->add_bro_array.array_size = array_size;
    args.broadcast.args->add_bro_array.right = right;

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
 */
static void update_upper_stage(node_t me, int stage, int pos2repl, int new_id) {

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
                    new_id);

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
            "Node %d : can't delete 'me' !! - stage = %d",
            me->self.id,
            stage);

    int size = me->bro_index[stage];
    int idx = index_bro(me, stage, bro2del);

    if (idx < size - 1) {

        int i = 0;
        for (i = idx; i < size - 1; i++) {

            me->brothers[stage][i] = me->brothers[stage][i + 1];
        }
    }

    me->brothers[stage][size - 1].id = -1;
    get_mailbox(0, me->brothers[stage][size - 1].mailbox);
    me->bro_index[stage]--;

    XBT_OUT();
}

/**
 * \brief This function tells a node that one of his sons of a given stage has
 *        split. As their father, it must connect to those two new sons.
 * \param me the current node
 * \param stage the stage that owns a new son
 * \param pos_init the position of init_rep in the calling node's stage
 *                 above the splitted one
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
    set_update(me, new_node_id);   //TODO!! : à vérifier

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

    MSG_error_t res;
    u_req_args_t args;
    args.add_pred.stage = stage;
    args.add_pred.new_pred_id = me->self.id;

    // insert init_rep and new_rep
    XBT_DEBUG("Node %d: insert %d in brothers[%d][%d]",
            me->self.id,
            init_rep_id,
            stage,
            pos_init);

    me->brothers[stage][pos_init].id = init_rep_id;
    get_mailbox(init_rep_id, me->brothers[stage][pos_init].mailbox);

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
            get_mailbox(rep_id, elem->recp.mailbox);
            elem->answer_data = NULL;
            xbt_dynar_push(me->expected_answers, &elem);

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
            get_mailbox(init_rep_id, elem->recp.mailbox);
            elem->answer_data = NULL;
            xbt_dynar_push(me->expected_answers, &elem);

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
        get_mailbox(new_rep_id, elem->recp.mailbox);
        elem->answer_data = NULL;
        xbt_dynar_push(me->expected_answers, &elem);

        xbt_assert(elem->recp.id > - 1,
                "Node %d: #5# recp.id is %d !!",
                me->self.id,
                elem->recp.id);

        cpt++;
    }

    //synchro (4)
    if (cpt > 0) {

        wait_for_completion(me, cpt);
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
    set_update(me, new_node_id);
    s_state_t state = get_state(me);

    XBT_VERB("Node %d: '%c'/%d - split() ... - new_node = %d",
            me->self.id,
            state.active,
            state.new_id,
            new_node_id);

    XBT_VERB("Node %d: In split - splitting stage %d", me->self.id, stage);

    XBT_DEBUG("Node %d routing table before split", me->self.id);
    display_rout_table(me, 'V');
    display_preds(me, 'D');

    int pos = index_bro(me, stage, me->self.id);
    int stay = (pos + 1) % 2;

    // finds a representative of the other node (after splitting)
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
    u_req_args_t args;
    MSG_error_t res = MSG_OK;

    // prepare the dynar of all async messages recipients
    int cpt = 0;
    //recp_rec_t recp_array = xbt_new0(s_recp_rec_t, me->bro_index[stage]);
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
                elem->answer_data = NULL;
                xbt_dynar_push(me->expected_answers, &elem);

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
                elem->answer_data = NULL;
                xbt_dynar_push(me->expected_answers, &elem);

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
            "Node %d: cpt too high (%d) - bro_index[%d] = %d",
            me->self.id,
            cpt,
            stage,
            me->bro_index[stage]);

    XBT_DEBUG("Node %d: init_rep_id = %d - new_rep_id = %d",
            me->self.id,
            init_rep_id,
            new_rep_id);

    xbt_free(me->brothers[stage]);
    me->brothers[stage] = new_grp;
    me->bro_index[stage] = idx;

    // synchro (2)
    if (cpt > 0) {

        wait_for_completion(me, cpt);
    }

    xbt_assert(idx <= b, "DST out of bounds. idx = %d", idx);

    XBT_VERB("Node %d routing table after split", me->self.id);
    display_rout_table(me, 'V');

    // set and store DST infos
    set_n_store_infos(me);

    display_preds(me, 'D');

    // tells every upper stage pred it's got a new member
    int ans_cpt = me->pred_index[stage + 1];
    args.cnx_groups.stage = stage + 1;
    args.cnx_groups.pos_init = index_bro(me, stage + 1, me->self.id);
    args.cnx_groups.pos_new = me->bro_index[stage + 1];
    args.cnx_groups.init_rep_id = init_rep_id;
    args.cnx_groups.new_rep_id = new_rep_id;
    args.cnx_groups.new_node_id = new_node_id;

    // works on a copy of upper stage preds
    int cpy_pred_index = me->pred_index[stage + 1];
    node_rep_t cpy_preds = xbt_new0(s_node_rep_t, me->pred_index[stage + 1]);
    for (i = 0; i < cpy_pred_index ; i++) {

        cpy_preds[i] = me->preds[stage + 1][i];
    }

    for (i = 0; i < cpy_pred_index; i++) {
        if (cpy_preds[i].id == me->self.id) {

            // local call (no answer is expected)
            ans_cpt--;

            connect_splitted_groups(me,
                    args.cnx_groups.stage,
                    args.cnx_groups.pos_init,
                    args.cnx_groups.pos_new,
                    args.cnx_groups.init_rep_id,
                    args.cnx_groups.new_rep_id,
                    args.cnx_groups.new_node_id);
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
            elem->answer_data = NULL;
            xbt_dynar_push(me->expected_answers, &elem);

            xbt_assert(elem->recp.id > - 1,
                    "Node %d: #8# recp.id is %d !!",
                    me->self.id,
                    elem->recp.id);

        }
    }

    // synchro (3)
    if (ans_cpt > 0) {

        //wait_for_completion(me, ans_cpt, recp_array, cpy_pred_index);
        wait_for_completion(me, ans_cpt);
    }

    // restore state    //TODO: non !! Trop tôt. Laisser faire la diffusion de set active
    /*
    idx = state_search(me, 'u', new_node_id);
    if (idx > -1) {

        xbt_dynar_remove_at(me->states, idx, NULL);
    }
    */
    xbt_free(cpy_preds);
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
    MSG_error_t res;

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
 */
static void merge(node_t me,
        int *nodes_array,
        int nodes_array_size,
        int stage,
        int pos_me,
        int pos_contact,
        int right) {

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
    MSG_error_t res;
    args.add_pred.stage = stage;
    args.add_pred.new_pred_id = me->self.id;

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
 */
static void broadcast_merge(node_t me, int stage, int pos_me, int pos_contact, int right, int lead_br) {

    XBT_IN();

    int i;
    u_req_args_t args;
    m_task_t task_sent = NULL;

    // broadcast a 'merge' task
    args.broadcast.type = TASK_MERGE;
    args.broadcast.stage = stage;           // same stage as the merging one
    args.broadcast.first_call = 1;
    args.broadcast.source_id = me->self.id;
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
    MSG_error_t res;
    u_req_args_t args;
    int new_rep_id = 0;
    int cpt = 0;

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
                    elem->answer_data = NULL;
                    xbt_dynar_push(me->expected_answers, &elem);

                    xbt_assert(elem->recp.id > - 1,
                            "Node %d: #9# recp.id is %d !!",
                            me->self.id,
                            elem->recp.id);

                    cpt++;
                } else {

                    // randomly choose a new rep to replace 'me'
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
                        elem->answer_data = NULL;
                        xbt_dynar_push(me->expected_answers, &elem);

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

            wait_for_completion(me, cpt);
        }

        cpt = 0;

        // tell all my brothers I'm leaving
        for (brother = 0; brother < cpy_bro_index[stage]; brother++) {

            if (cpy_brothers[stage][brother].id != me->self.id) {

                args.del_pred.stage = stage;
                args.del_pred.pred2del_id = me->self.id;

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
                elem->answer_data = NULL;
                xbt_dynar_push(me->expected_answers, &elem);

                xbt_assert(elem->recp.id > - 1,
                        "Node %d: #11# recp.id is %d !!",
                        me->self.id,
                        elem->recp.id);

                cpt++;
            }
        }

        // synchro
        if (cpt > 0) {

            wait_for_completion(me, cpt);
        }
        cpt = 0;
    }
    //xbt_free(recp_array);

    // manage merges if necessary
    if (me->bro_index[0] <= a) {

        int idx = 0;
        while (me->brothers[0][idx].id == me->self.id && idx < me->bro_index[0]) {

            idx++;
        }

        xbt_assert(idx < me->bro_index[0],
                "Node %d: Merge index error - idx = %d - me->bro_index[0] = %d",
                me->self.id,
                idx,
                me->bro_index[0]);

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
 * \return the array of extracted nodes (the leaving part)
 */
static u_ans_data_t transfer(node_t me, int st, int right, int cut_pos, s_node_rep_t sender) {
    //TODO ôter l'argument sender qui ne sert plus à rien

    XBT_IN();

    int i, k, start, end, rep_idx;
    int cpt = 0;
    MSG_error_t res;
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
    args.broadcast.lead_br = 0;

    args.broadcast.args = xbt_new0(u_req_args_t, 1);

    args.broadcast.args->cut_node.stage = st;
    args.broadcast.args->cut_node.right = right;
    args.broadcast.args->cut_node.cut_pos = cut_pos;

    m_task_t task_sent = NULL;
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
 */
static void replace_bro(node_t me, int stage, int init_idx, int new_id) {

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
        get_mailbox(new_id, me->brothers[stage][init_idx].mailbox);
    } else {

        // add brother
        add_brother(me, stage, new_id);
    }

    u_req_args_t args;
    MSG_error_t res;

    // delete predecessor
    if ((bro_id != me->self.id) && (init_idx < me->bro_index[stage])){

        args.del_pred.stage = stage;
        args.del_pred.pred2del_id = me->self.id;

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
 */
static void shift_bro(node_t me, int stage, s_node_rep_t new_node, int right) {

    XBT_IN();

    u_req_args_t args;
    MSG_error_t res;
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
 */
static void clean_upper_stage(node_t me, int stage, int pos_me, int pos_contact) {

    XBT_IN();

    u_req_args_t args;
    MSG_error_t res;

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
 */
static void merge_request(node_t me) {

    XBT_IN();

    // manage merges
    int size_last_stage = 0;
    int pos_me, pos_contact;
    int stage = 0;
    int i = 0;
    MSG_error_t res;
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
            clean_upper_stage(me, stage, pos_me, pos_contact);

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
            args.broadcast.lead_br = 0;

            args.broadcast.args = xbt_new0(u_req_args_t, 1);

            args.broadcast.args->clean_stage.stage = stage;
            args.broadcast.args->clean_stage.pos_me = pos_me;
            args.broadcast.args->clean_stage.pos_contact = pos_contact;

            m_task_t task_sent = NULL;
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
                    (right + 1) % 2);

            // every member of neighbour branch append current nodes
            XBT_VERB("Node %d : Ask %d to broadcast an ADD_BRO_ARRAY task",
                    me->self.id,
                    answer_data->answer.transfer.rep_array[0].id);

            args.br_add_bro_array.stage = stage;
            args.br_add_bro_array.bro = current_bro;
            args.br_add_bro_array.array_size = current_size;
            args.br_add_bro_array.right = right;

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

            update_upper_stage(me, stage, pos_contact, answer_data->answer.transfer.stay_id);

            // update upper stage for all concerned members
            m_task_t task_sent = NULL;

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
            args.broadcast.lead_br = 0;

            args.broadcast.args = xbt_new0(u_req_args_t, 1);

            args.broadcast.args->update_upper_stage.stage = stage;
            args.broadcast.args->update_upper_stage.pos2repl = pos_contact;
            args.broadcast.args->update_upper_stage.new_id = answer_data->answer.transfer.stay_id;

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

        m_task_t task_sent = NULL;
        make_broadcast_task(me, args, &task_sent);
        handle_task(me, &task_sent);
        task_free(&task_sent);

        xbt_free(args.broadcast.args);

        //data_ans_free(me, &answer_data);
    }
    */

    // delete root stage
    if (size_last_stage == 1) {

        m_task_t task_sent = NULL;

        args.broadcast.type = TASK_DEL_ROOT;
        args.broadcast.stage = me->height - 1;
        args.broadcast.first_call = 1;
        args.broadcast.source_id = me->self.id;
        args.broadcast.lead_br = 0;

        args.broadcast.args = xbt_new0(u_req_args_t, 1);
        args.broadcast.args->del_root.init_height = me->height;
        make_broadcast_task(me, args, &task_sent);

        handle_task(me, &task_sent);

        xbt_free(args.broadcast.args);
    }

    args.broadcast.args = NULL;

    XBT_OUT();
}

/**
 * \brief To balance load among nodes for current routing table
 * \param me current node
 * \param contact_id the node that provided current me's routing table
 */
static void load_balance(node_t me, int contact_id) {

    XBT_IN();

    int idx = 0;
    int i, j, brother;
    node_rep_t new_nodes = NULL;

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
    MSG_error_t res;

    /* starts by adding me as a pred for the whole table
       (so that each member's preds will be up to date as soon as possible) */
    u_req_args.add_pred.new_pred_id = me->self.id;

    for (i = 1; i < me->height; i++) {
        for (j = 0; j < me->bro_index[i]; j++) {

            if (me->brothers[i][j].id != contact_id) {

                u_req_args.add_pred.stage = i;
                res = send_msg_async(me,
                        TASK_ADD_PRED,
                        me->brothers[i][j].id,
                        u_req_args);

            }
        }
    }

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

                    /* answer_data may be NULL if GET_REP didn't get any answer
                       before a certain time */
                    if (res != MSG_OK || answer_data == NULL) {

                        // get_rep failure
                        XBT_WARN("Node %d: failed to get a new representative for"
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

                        u_req_args.del_pred.stage = i;
                        u_req_args.del_pred.pred2del_id = me->self.id;

                        res = send_msg_async(me,
                                TASK_DEL_PRED,
                                me->brothers[i][j].id,
                                u_req_args);

                        u_req_args.add_pred.stage = i;
                        u_req_args.add_pred.new_pred_id = me->self.id;

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
 * \brief Provide a new contact for a coming node that failed to join
 * \param me the current node
 */
static u_ans_data_t get_new_contact(node_t me) {

    XBT_IN();
    u_ans_data_t answer;

    int idx, stage;

    if (me->height > 1) {

        stage = 1;
    } else {

        stage = 0;
    }

    idx = rand() % (me->bro_index[stage]);
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

    get_mailbox(node.self.id, node.self.mailbox);

    int join_success = 1;       // success by default
    int tries = 0;

    srand(time(NULL));
    init(&node);

    if (argc == 5) {            // all nodes but first one need to join

        double sleep_time = atof(argv[3]);
        node.deadline = atof(argv[4]);
        int contact_id = atoi(argv[2]);
        u_req_args_t u_req_args;
        ans_data_t answer_data = NULL;
        MSG_error_t res;

        do {

            tries++;

            // sleep before starting to join
            if (tries > 1) {

                // TODO: faire des essais avec différentes bornes
                // 16 <= sleep_time < 37
                sleep_time = (rand() / (double)RAND_MAX) * 16 + 37;

                // get a new contact if too many failures
                if (tries > 200) {

                    answer_data = NULL;
                    res = send_msg_sync(&node,
                            TASK_GET_NEW_CONTACT,
                            contact_id,
                            u_req_args,
                            &answer_data);

                    if (res == MSG_OK) {

                        contact_id = answer_data->answer.get_new_contact.id;

                        XBT_VERB("Node %d: got new contact %d instead of %d",
                                node.self.id,
                                contact_id,
                                atoi(argv[2]));

                    } else {

                        XBT_VERB("Node %d: failed to get a new contact from node %d",
                                node.self.id,
                                contact_id);
                    }
                }
            }

            XBT_INFO("Node %d: Let's sleep during %f",
                    node.self.id,
                    sleep_time);

            MSG_process_sleep(sleep_time);

            XBT_INFO("Node %d: **** START JOINING DST *** (try number %d)",
                    node.self.id,
                    tries);
            XBT_INFO("Node %d: Let's join the system ! (via contact %d) -"
                    " deadline = %f",
                    node.self.id,
                    //atoi(argv[2]),
                    contact_id,
                    node.deadline);

            // set the dst infos for the current node
            if (tries == 1) {

                // only one order number per node
                node.dst_infos.order = order++;
            }
            node.dst_infos.nb_messages = tot_msg_number();

            XBT_VERB("Node %d: order = %d, nb_msg = %d",
                    node.self.id,
                    node.dst_infos.order,
                    node.dst_infos.nb_messages);

            join_success = join(&node, contact_id, tries);

            if (!join_success) {

                XBT_INFO("Node %d: Join failure ! - tries = %d",
                        node.self.id,
                        tries);
            } else {

                //xbt_assert(node.self.id != 1521, "Node %d", node.self.id);
            }
        } while (!join_success && tries < MAX_JOIN);
    } else {

        // first node
        order = 0;
        node.dst_infos.order = order++;
        node.deadline = atoi(argv[2]);
    }

    if (join_success) {

        XBT_INFO("Node %d: **** JOIN COMPLETED ****", node.self.id);

        // active state
        set_active(&node, node.self.id);
        set_n_store_infos(&node);

        XBT_DEBUG("Node %d: add stage = %d",
                node.self.id,
                node.dst_infos.add_stage);

        // run remaining tasks, if any
        run_delayed_tasks(&node, '3');

        // task receive loop
        m_task_t task_received = NULL;
        MSG_error_t res = MSG_TRANSFER_FAILURE;
        s_state_t state = get_state(&node);

        while (MSG_get_clock() < max_simulation_time && state.active != 'n') {

            // prepare a new 'listening' phase
            if (node.comm_received == NULL) {

                task_received = NULL;
                node.comm_received =
                    MSG_task_irecv(&task_received, node.self.mailbox);

                XBT_VERB("Node %d: Waiting for a task...", node.self.id);
            }

            if (!MSG_comm_test(node.comm_received)) {

                // no task was received: make some periodic calls
                state = get_state(&node);

                if (MSG_get_clock() >= 680) {

                    xbt_log_control_set("msg_dst.thres:verbose");
                }

                if (MSG_get_clock() >= node.deadline && state.active == 'a') {

                    XBT_INFO("Node %d: deadline reached: time to leave !",
                            node.self.id);
                    state_t *state_ptr = NULL;
                    state_ptr = xbt_dynar_get_ptr(node.states,
                            xbt_dynar_length(node.states) - 1);
                    (*state_ptr)->active = 'n';      // non active state
                    leave(&node);
                    set_n_store_infos(&node);
                    XBT_INFO("Node %d left ...", node.self.id);
                } else {

                    /*
                       XBT_DEBUG("Node %d: nothing to do: sleep for a while",
                       node.self.id);
                       */

                    MSG_process_sleep(0.2);     //TODO: pb synchros si plus long
                }
            } else {

                // some task has been received
                res = MSG_comm_get_status(node.comm_received);
                MSG_comm_destroy(node.comm_received);
                node.comm_received = NULL;
                req_data_t req = MSG_task_get_data(task_received);

                XBT_VERB("Node %d: Task received", node.self.id);

                if (res == MSG_OK) {

                    if (xbt_dynar_is_empty(node.remain_tasks) == 0 &&
                        req->type == TASK_CNX_REQ) {

                        xbt_dynar_push(node.remain_tasks, &task_received);
                        task_received = NULL;
                    } else {

                        handle_task(&node, &task_received);
                    }

                    // run remaining tasks, if any
                    run_delayed_tasks(&node, '4');
                } else {

                    // reception failure
                    XBT_WARN("Node %d: Failed to receive a task",
                            node.self.id);
                }
            }
            state = get_state(&node);
        }
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

    node_free(&node);
    XBT_OUT();
    return (!join_success);
}

/**
 * \brief This function is called by the current node when it receives a task
 * \param me the current node
 * \param task pointer to the task to handle
 * \return A value to indicate if things went right or not
 */
static e_val_ret_t handle_task(node_t me, m_task_t* task) {

    XBT_IN();

    xbt_assert(task != NULL, "Node %d: task must not be NULL !",
            me->self.id);

    e_val_ret_t val_ret = OK;
    req_data_t rcv_req = (req_data_t) MSG_task_get_data(*task);
    ans_data_t rcv = (ans_data_t) MSG_task_get_data(*task);
    e_task_type_t type = rcv_req->type;

    s_state_t state = get_state(me);

    XBT_VERB("Node %d - '%c'/%d: Handling task '%s - %s' received from node %d",
            me->self.id,
            state.active,
            state.new_id,
            MSG_task_get_name(*task),
            debug_msg[type],
            rcv_req->sender_id);

    // don't handle answers
    if (strcmp(MSG_task_get_name(*task), "ans") == 0) {

        XBT_DEBUG("Node %d: only requests accepted in handle_task !",
                me->self.id);

        data_ans_free(me, &rcv); 
        task_free(task);

        return val_ret;
    }

    u_req_args_t rcv_args;
    if (rcv_req != NULL) {

        rcv_args = rcv_req->args;
    }
    u_ans_data_t answer;
    MSG_error_t res = MSG_OK;
    int init_idx;

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

                xbt_dynar_push(me->remain_tasks, task);
                *task = NULL;
                val_ret = STORED;
            } else {

                // run task now
                answer = get_rep(me, rcv_args.get_rep.stage, rcv_args.get_rep.new_node_id);
                res = send_ans_sync(me, type, rcv_req->sender_id, answer);

                data_req_free(me, &rcv_req);
                task_free(task);

                XBT_VERB("Node %d: TASK_GET_REP done", me->self.id);
            }
            break;

        case TASK_CNX_REQ:
            //TODO : voir s'il ne faudrait pas aussi tester l'état du leader ici

            if ((state.active == 'u' && state.new_id == rcv_args.cnx_req.new_node.id) ||
                (state.active == 'a')) {

                // run task now
                answer = connection_request(me,
                        rcv_args.cnx_req.new_node,
                        rcv_args.cnx_req.try);

                XBT_DEBUG("Node %d: CNX_REQ val_ret = %d - contact = %d",
                        me->self.id,
                        answer.cnx_req.val_ret,
                        answer.cnx_req.new_contact.id);

                res = send_ans_sync(me, type, rcv_req->sender_id, answer);

                data_req_free(me, &rcv_req);
                task_free(task);

                XBT_VERB("Node %d: TASK_CNX_REQ done", me->self.id);

            } else {

                //if (state.active == 'b') {
                if (1 == 1) {

                    val_ret = UPDATE_NOK;

                    // send an answer back ...
                    answer.cnx_req.val_ret = val_ret;
                    res = send_ans_sync(me, type, rcv_req->sender_id, answer);
                    *task = NULL;
                } else {

                    // ... or store task (CNX_REQ isn't broadcasted)
                    XBT_VERB("Node %d: '%c'/%d -  store CNX_REQ/%d for later execution",
                            me->self.id,
                            state.active,
                            state.new_id,
                            rcv_args.cnx_req.new_node.id);

                    xbt_dynar_push(me->remain_tasks, task);
                    *task = NULL;
                    val_ret = STORED;
                }
            }
            break;

        case TASK_NEW_BROTHER_RCV:
            new_brother_received(me, rcv_args.new_brother_rcv.new_node);
            res = send_ans_sync(me, type, rcv_req->sender_id, answer);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_NEW_BROTHER_RCV done", me->self.id);
            break;

        case TASK_SPLIT_REQ:
            if (state.active == 'b') {

                // store task (SPLIT_REQ isn't broadcasted)
                XBT_VERB("Node %d - active = '%c': store task for later"
                        " execution",
                        me->self.id,
                        state.active);

                xbt_dynar_push(me->remain_tasks, task);
                *task = NULL;
                val_ret = STORED;
            } else {

                // run task
                split_request(me,
                        rcv_args.split_req.stage_nbr,
                        rcv_args.split_req.new_node_id);
                res = send_ans_sync(me, type, rcv_req->sender_id, answer);

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

            if (state.active == 'b' ||
                    state.active == 'l' ||
                    state.active == 'p') {

                // store task (CNX_GROUPS isn't broadcasted)
                XBT_VERB("Node %d - active = '%c': store task for later execution",
                        me->self.id,
                        state.active);

                xbt_dynar_push(me->remain_tasks, task);
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

                    send_completed(me, type, rcv_req->sender_id);
                }

                data_req_free(me, &rcv_req);
                task_free(task);

                XBT_VERB("Node %d: TASK_CNX_GROUPS done", me->self.id);
            }
            break;

        case TASK_SPLIT:
            if (state.active == 'b' || state.active == 'l') {

                // store broadcasted task
                val_ret = STORED;
            } else {

                split(me, rcv_args.split.stage_nbr, rcv_args.split.new_node_id);

                data_req_free(me, &rcv_req);
                task_free(task);

                XBT_VERB("Node %d: TASK_SPLIT done",
                        me->self.id);
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
            res = send_ans_sync(me, type, rcv_req->sender_id, answer);

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

                xbt_dynar_push(me->remain_tasks, task);
                *task = NULL;
                val_ret = STORED;
            } else {

                // run task
                add_pred(me, rcv_args.add_pred.stage, rcv_args.add_pred.new_pred_id);

                // send a 'completed task' message back
                if (rcv_req->sender_id != me->self.id) {

                    send_completed(me, type, rcv_req->sender_id);
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

                send_completed(me, type, rcv_req->sender_id);
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
                       - SET_ACTIVE
                       (if they are triggered by the same new node as
                       current one)

                       - ADD_STAGE
                       - SPLIT
                       */
                    (state.active == 'u' &&
                     (
                      !(rcv_args.broadcast.type == TASK_SET_UPDATE &&
                          rcv_args.broadcast.args->set_update.new_id ==
                          state.new_id) &&
                      /*
                      !(rcv_args.broadcast.type == TASK_SET_ACTIVE &&
                          ((rcv_args.broadcast.args->set_active.new_id ==
                          state.new_id) ||
                          (state.new_id == -1))) &&
                      */

                      rcv_args.broadcast.type != TASK_SET_ACTIVE &&

                      rcv_args.broadcast.type != TASK_ADD_STAGE &&
                      rcv_args.broadcast.type != TASK_SPLIT &&
                      rcv_args.broadcast.type != TASK_FLAG_REQ
                     )
                    )
                        ) {

                            /* for refused broadcasted tasks, either send an answer back ... */
                            if (rcv_args.broadcast.type == TASK_SET_ACTIVE ||
                                    rcv_args.broadcast.type == TASK_SET_UPDATE) {

                                XBT_VERB("Don't accept '%s' here - current new_id = %d"
                                        " - rcv_new_id = %d",
                                        debug_msg[rcv_args.broadcast.type],
                                        state.new_id,
                                        (rcv_args.broadcast.type == TASK_SET_UPDATE ?
                                         rcv_args.broadcast.args->set_update.new_id :
                                         rcv_args.broadcast.args->set_active.new_id));

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
                                    res = send_ans_sync(me, type, rcv_req->sender_id, answer);

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

                                xbt_dynar_push(me->remain_tasks, task);
                                *task = NULL;
                                val_ret = STORED;
                            }
                        } else {

                            /* start call: forward broadcast to the leader ? */
                            if (rcv_args.broadcast.first_call == 1) {

                                if (rcv_args.broadcast.type == TASK_SET_UPDATE) {

                                    XBT_VERB("Node %d: Run broadcast of Set Update for new_id = %d - state.new_id = %d",
                                            me->self.id,
                                            rcv_args.broadcast.args->set_update.new_id,
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

                                    // self set 'u' right now
                                    if (rcv_args.broadcast.type == TASK_SET_UPDATE) {

                                        set_update(me,
                                                rcv_args.broadcast.args->set_update.new_id);
                                    }

                                    /* If a Set Active task is broacasted, it
                                     * mustn't be interrupted by another
                                     * broadcast (of Set Update, for instance)
                                     */
                                    if (rcv_args.broadcast.type == TASK_SET_ACTIVE) {

                                        set_update(me, -1);
                                    }

                                    task_free(task);

                                    rcv_args.broadcast.first_call = 0;
                                    val_ret = broadcast(me, rcv_args);
                                } else {

                                    // forward broadcast request to the leader
                                    XBT_VERB("Node %d: forward broadcast request to leader %d",
                                            me->self.id,
                                            //me->brothers[rcv_args.broadcast.stage][0].id);
                                            me->brothers[0][0].id);

                                    /* If a Set Active task is broacasted, it
                                     * mustn't be interrupted by another
                                     * broadcast (of Set Update, for instance)
                                     */
                                    if (rcv_args.broadcast.type == TASK_SET_ACTIVE) {

                                        set_update(me, -1);
                                    }

                                    ans_data_t answer_data = NULL;
                                    res = send_msg_sync(me,
                                            TASK_BROADCAST,
                                            //me->brothers[rcv_args.broadcast.stage][0].id,
                                            me->brothers[0][0].id,
                                            rcv_args,
                                            &answer_data);

                                    xbt_assert(res == MSG_OK, "Node %d: Failed to send '%s' to %d",
                                            me->self.id,
                                            debug_msg[TASK_BROADCAST],
                                            me->brothers[rcv_args.broadcast.stage][0].id);

                                    // TODO : peut-être pas utile ?
                                    if (rcv_args.broadcast.type == TASK_SET_ACTIVE &&
                                        state.active == 'u' && state.new_id == -1) {

                                        xbt_assert(1 == 0, "SET_ACTIVE %d", rcv_args.broadcast.args->set_active.new_id);
                                        set_active(me, rcv_args.broadcast.args->set_active.new_id);
                                    }

                                    // get the return value
                                    val_ret = answer_data->answer.handle.val_ret;
                                    data_ans_free(me, &answer_data);
                                }

                                /* send a message back (in case of sync call of TASK_BROADCAST) */
                                if (rcv_req->sender_id != me->self.id) {

                                    answer.handle.val_ret = val_ret;
                                    answer.handle.val_ret_id = me->self.id;
                                    answer.handle.br_type = rcv_args.broadcast.type;
                                    res = send_ans_sync(me, type, rcv_req->sender_id, answer);

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

                                    // self set 'u' right now
                                    if (rcv_args.broadcast.type == TASK_SET_UPDATE) {

                                        set_update(me, rcv_args.broadcast.args->set_update.new_id);
                                    }

                                    /* If a Set Active task is broacasted, it
                                     * mustn't be interrupted by another
                                     * broadcast (of Set Update, for instance)
                                     */
                                    if (rcv_args.broadcast.type == TASK_SET_ACTIVE) {

                                        set_update(me, -1);
                                    }

                                    task_free(task);

                                    //TODO: Ne pas continuer si UPDATE_NOK ici

                                    // Transmit the message to lower stage.
                                    rcv_args.broadcast.stage--;
                                    val_ret = broadcast(me, rcv_args);
                                } else {

                                    /* stage 0 reached: handle the broadcasted task */
                                    XBT_VERB("Node %d: run broadcasted task - '%s'",
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
                                        get_mailbox(me->self.id, req_data->answer_to);
                                        get_mailbox(me->self.id, req_data->sent_to);

                                        if (rcv_args.broadcast.args != NULL) {

                                            req_data->args = *(rcv_args.broadcast.args);
                                        } else {

                                            /* XBT_DEBUG("Node %d: broadcast.args = NULL",
                                               me->self.id); */
                                        }

                                        m_task_t br_task = MSG_task_create(NULL,
                                                COMP_SIZE,
                                                COMM_SIZE,
                                                req_data);
                                        MSG_task_set_name(br_task, "async");

                                        val_ret = handle_task(me, &br_task);

                                        /* if br_task has to be delayed, store the whole
                                           broadcasted task */
                                        if (val_ret == STORED) {

                                            // store task
                                            XBT_VERB("Node %d - active = '%c': store "
                                                    "broadcasted task for later execution",
                                                    me->self.id,
                                                    state.active);

                                            xbt_dynar_push(me->remain_tasks, task);
                                            *task = NULL;     //TODO : utile ?
                                        } else {

                                            task_free(task);
                                        }
                                    }

                                    XBT_VERB("Node %d: End of run broadcasted task - '%s'",
                                            me->self.id,
                                            debug_msg[rcv_args.broadcast.type]);
                                }

                                // send an answer back if task hasn't been stored
                                if (rcv_req->sender_id != me->self.id && val_ret != STORED) {

                                    answer.handle.val_ret = val_ret;
                                    answer.handle.val_ret_id = me->self.id;
                                    answer.handle.br_type = rcv_args.broadcast.type;
                                    res = send_ans_sync(me, type, rcv_req->sender_id, answer);

                                    XBT_DEBUG("Node %d: answer '%s' sent to %d",
                                            me->self.id,
                                            (val_ret == UPDATE_NOK ? "UPDATE_NOK" : "OK"),
                                            rcv_req->sender_id);
                                }
                            }

                            if (val_ret != STORED) {

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
            res = send_ans_sync(me, type, rcv_req->sender_id, answer);

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
                send_completed(me, type, rcv_req->sender_id);
                send_completed(me, type, rcv_req->sender_id);
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
                    rcv_args.repl_bro.new_id);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id);
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
                    rcv_args.merge.right);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id);
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
                    rcv_args.broad_merge.lead_br);

            res = send_ans_sync(me, type, rcv_req->sender_id, answer);

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
                    rcv_args.clean_stage.pos_contact);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_CLEAN_STAGE done", me->self.id);
            break;

        case TASK_MERGE_REQ:
            merge_request(me);

            res = send_ans_sync(me, type, rcv_req->sender_id, answer);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_MERGE_REQ done", me->self.id);
            break;

        case TASK_SET_ACTIVE:
            set_active(me, rcv_args.set_active.new_id);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_SET_ACTIVE done", me->self.id);
            break;

        case TASK_SET_UPDATE:
            val_ret = set_update(me, rcv_args.set_update.new_id);

            // send the answer back
            if (rcv_req->sender_id != me->self.id) {

                XBT_DEBUG("Node %d: send TASK_SET_UPDATE answer - active = '%c'",
                        me->self.id,
                        state.active);

                answer.handle.val_ret = val_ret;
                answer.handle.val_ret_id = me->self.id;
                res = send_ans_sync(me, type, rcv_req->sender_id, answer);

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
                    rcv_args.set_state.new_id,
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
            answer = is_brother(me, rcv_args.is_brother.id);
            res = send_ans_sync(me, type, rcv_req->sender_id, answer);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_IS_BROTHER done", me->self.id);
            break;

        case TASK_TRANSFER:
            answer = transfer(me,
                    rcv_args.transfer.st,
                    rcv_args.transfer.right,
                    rcv_args.transfer.cut_pos,
                    rcv_args.transfer.sender);

            res = send_ans_sync(me, type, rcv_req->sender_id, answer);

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
                    rcv_args.add_bro_array.right);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_ADD_BRO_ARRAY done", me->self.id);
            break;

        case TASK_SHIFT_BRO:
            shift_bro(me,
            rcv_args.shift_bro.stage,
            rcv_args.shift_bro.new_node,
            rcv_args.shift_bro.right);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id);
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

                send_completed(me, type, rcv_req->sender_id);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_ADD_BRO done", me->self.id);
            break;

        case TASK_CUT_NODE:
            cut_node(me,
                    rcv_args.cut_node.stage,
                    rcv_args.cut_node.right,
                    rcv_args.cut_node.cut_pos);

            // send a 'completed task' message back
            if (rcv_req->sender_id != me->self.id) {

                send_completed(me, type, rcv_req->sender_id);
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
                    rcv_args.br_add_bro_array.right);

            res = send_ans_sync(me, type, rcv_req->sender_id, answer);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_BR_ADD_BRO_ARRAY done", me->self.id);
            break;

        case TASK_UPDATE_UPPER_STAGE:
            update_upper_stage(me,
                    rcv_args.update_upper_stage.stage,
                    rcv_args.update_upper_stage.pos2repl,
                    rcv_args.update_upper_stage.new_id);

            res = send_ans_sync(me, type, rcv_req->sender_id, answer);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_UPDATE_UPPER_STAGE done", me->self.id);
            break;

        case TASK_GET_NEW_CONTACT:
            answer = get_new_contact(me);

            res = send_ans_sync(me, type, rcv_req->sender_id, answer);

            data_req_free(me, &rcv_req);
            task_free(task);

            XBT_VERB("Node %d: TASK_GET_NEW_CONTACT done", me->self.id);
            break;

        case TASK_FLAG_REQ:
            val_ret = flag_request(me, rcv_args.flag_request.new_node);

            // send the answer back
            if (rcv_req->sender_id != me->self.id) {

                XBT_DEBUG("Node %d: send TASK_FLAG_REQ answer - active = '%c'",
                        me->self.id,
                        state.active);

                answer.handle.val_ret = val_ret;
                answer.handle.val_ret_id = me->self.id;
                res = send_ans_sync(me, type, rcv_req->sender_id, answer);
            }

            data_req_free(me, &rcv_req);
            task_free(task);

            state = get_state(me);
            XBT_VERB("Node %d: '%c'/%u - TASK_FLAG_REQ done - val_ret = %s",
                    me->self.id,
                    state.active,
                    state.new_id,
                    (val_ret == UPDATE_NOK ? "NOK" : "OK"));
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
            (val_ret == UPDATE_NOK ? "UPDATE_NOK" : "OK"));

    XBT_OUT();
    return val_ret;
}

/**
 * \brief Main function
 */
int main(int argc, char *argv[]) {

    XBT_IN();

    if (argc < 3) {

        printf("Usage: %s platform_file deployment_file [simulation_time]"
                " --log=msg_dst.thres:info 2>&1 | tools/MSG_visualization/colorize.pl\n",
                argv[0]);
        printf("       simulation_time defaults to %d ",
                (int) max_simulation_time);
        exit(1);
    }

    xbt_log_control_set("msg_dst.thres:TRACE");
    MSG_global_init(&argc, argv);
    srand(time(NULL));
    //infos_dst = xbt_dynar_new_sync_sync(sizeof(dst_infos_t), &xbt_free_ref);
    infos_dst = xbt_dynar_new_sync(sizeof(dst_infos_t), &elem_free);

    // init array of failed nodes
    failed_nodes = xbt_new0(s_f_node_t, 1);

    // create timer
    xbt_os_timer_t timer = xbt_os_timer_new();

    const char* platform_file = argv[1];
    const char* deployment_file = argv[2];
    if (argc == 4) max_simulation_time = atoi(argv[3]);

    //MSG_set_channel_number(0);         //TODO: je n'ai pas compris l'utilité de set_channel_number
    MSG_create_environment(platform_file);

    MSG_function_register("node", node);
    MSG_launch_application(deployment_file);

    xbt_os_timer_start(timer);
    MSG_error_t res = MSG_main();
    xbt_os_timer_stop(timer);


    // print all routing tables
    XBT_INFO("************************************     PRINT ALL     "
            "************************************\n");
    unsigned int cpt = 0, nb_nodes = 0, nb_nodes_tot = 0;

    // to store non active nodes id
    int size = 100;
    int non_active[size];
    int i;
    for (i = 0; i < size; i++) {

        non_active[i] = -1;
    }
    size = 0;

    dst_infos_t elem;
    int tot_msg_nodes = 0;
    int max_msg_nodes = -1;
    int max_node;

    xbt_dynar_foreach(infos_dst, cpt, elem) {
        if (elem != NULL) {

            nb_nodes_tot++;
            if (elem->active == 'a') {

                nb_nodes++;
                tot_msg_nodes += elem->nb_messages;
                if (elem->nb_messages > max_msg_nodes) {
                    max_msg_nodes = elem->nb_messages;
                    max_node = elem->node_id;
                }

                XBT_INFO("[Node %d]: \n%s\nNeeded to split %d stages\nNeeded"
                        " %d messages to join\n%s\n",
                        elem->node_id,
                        (elem->add_stage == 0 ? "Didn't add any stage" :
                         "Added a stage"),
                        elem->nbr_split_stages,
                        elem->nb_messages,
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
    
    XBT_INFO("Number of elements in infos_dst = %d", cpt);
    XBT_INFO("Messages needed for %d active nodes / %d total nodes ( sent - broadcasted )",
            nb_nodes,
            nb_nodes_tot);
    for (i = 0; i < TYPE_NBR; i++) {
        XBT_INFO("\t['%22s': %6d - %d]",
                debug_msg[i],
                nb_messages[i],
                nb_br_messages[i]);
    }

    if (nb_nodes != nb_nodes_tot) {

        XBT_INFO("");
        i = 0;
        while (non_active[i] != -1) {

            XBT_INFO("Node %d non active", non_active[i]);
            i++;
        }
        XBT_INFO("");
    }

    XBT_INFO("");
    XBT_INFO("Total number of messages: %d\n", tot_msg_number());
    XBT_INFO("Max messages needed by node %d: %d\n",
            max_node,
            max_msg_nodes);
    XBT_INFO("Total number of join abortions: %d - Details:", nb_abort);
    for (i = 0; i < nb_abort; i++) {

        XBT_INFO("\tnode %d failed at [%f]",
                failed_nodes[i].id,
                failed_nodes[i].f_time);
    }

    // all elements have already been freed during foreach
    xbt_dynar_free_container(&infos_dst);

    XBT_INFO("\nSimulation time %lf", xbt_os_timer_elapsed(timer));
    XBT_INFO("Simulated time: %g", MSG_get_clock());

    xbt_os_timer_free(timer);

    //MSG_clean();
    XBT_OUT();

    if (res == MSG_OK)
        return 0;
    else
        return 1;
}
