#ifndef PARSE_INCLUDE
#define PARSE_INCLUDE

/* macros for the   parse_args   routine */

#define P_STRING 1		/* Macros for the result_type attribute */
#define P_CHAR 2
#define P_SHORT 3
#define P_INT 4
#define P_LONG 5
#define P_FILE 6
#define P_OLD_FILE 7
#define P_NEW_FILE 8
#define P_FLOAT 9
#define P_DOUBLE 10

#define P_CASE_INSENSITIVE 01	/* Macros for the   flags   attribute */
#define P_REQUIRED_PREFIX 02

#define P_NO_ARGS 0		/* Macros for the   arg_count   attribute */
#define P_ONE_ARG 1
#define P_INFINITE_ARGS 2

#define p_entry(pref,swit,flag,count,type,store,size) \
    { (pref), (swit), (flag), (count), (type), (int *) (store), (size) }

typedef struct {
    char *prefix;
    char *string;
    int flags;
    int count;
    int result_type;
    int *result_ptr;
    int table_size;
} arg_info;

typedef int boolean;
boolean parse_args(int argc, char *argv[], arg_info table[], int entries, char *others[], int other_count);

#endif
