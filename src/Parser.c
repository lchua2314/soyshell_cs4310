/*
  Parser for the shell designed to parse the following grammar
  ('+' = whitespace)
  expr: s / s + op + expr
  s: {expr} / invoke
  invoke: cmd [ + | + cmd ]...
  op: && / || / ; / =
  redir: < / << / > / >>
  cmd: EXECUTABLE [+ arg]... [ + &] [+ redir + FILE_NAME/DELIM]
  arg: $NAMED_CONSTANT / LITERAL

  IMPORTANT: Don't forget to call init() to intialize the array of user
  defined constants and finish() to cleanup the array
*/
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#define BUFF_MAX 1024 /* Maximum number of characters in the character buffer */
#define INVALID_POS -1
#define INIT_CONSTS 8 /* Initial number of constants to allocate memory for */
#define MAX_ARGS 1024 /* Maximum number of arguments in argv */

char ***consts; /* Array of string pairs to store user defined constants. If we have more time, this should be replaced with a BST */
unsigned int numConsts; /* Current number of constants ie. next free index */
unsigned int maxConsts; /* Current maximum number of user defined constants */

void init();
void finish();
bool addConst(char*, char*);
char* getConst(char*);
bool isOp(char*);
bool isPipe(char*);
bool matchBrace(char*, int*, const unsigned int);
bool matchQuote(char*, int*, const unsigned int);
bool parseExpr(char*, char*, char*, char*);
bool parseCmd(char*, const unsigned int, char*, char**, unsigned int*, bool*);
bool parseS(char*, char*, char*);
char* evalArg(char*);
int evalCmd(char*, unsigned int, char**, bool);
int evalS(char*);
int evalExpr(char*);

/* Initialize the global variables */
void init()
{
    maxConsts = INIT_CONSTS;
    numConsts = 0;
    consts = (char***) malloc(maxConsts * sizeof(char**));
    /* Reserve the 0th index for the PATH variable */
    consts[numConsts] = (char**) malloc(2 * sizeof(char*));
    consts[numConsts][0] = (char*) malloc(BUFF_MAX * sizeof(char));
    consts[numConsts][1] = (char*) malloc(BUFF_MAX * sizeof(char));
    strcpy(consts[numConsts][0], "PATH");
    /* For the purpose of the assignment, we will make the assumption that the executable is called in the root of the
       repo and the default path will be the repo's bin folder */
    getcwd(consts[numConsts][1], BUFF_MAX);
    strcat(consts[numConsts][1], "/bin");
    ++numConsts;
}

/* Clean up global variables */
void finish()
{
    for (unsigned int i = 0; i < numConsts; ++i)
    {
        free(consts[i][0]);
        free(consts[i][1]);
        free(consts[i]);
    }
    free(consts);
}

/* Define a constant with the specified key and value */
bool addConst(char *key, char *val)
{
    if (strlen(key) > BUFF_MAX - 1)
    {
        fprintf(stderr, "addConst: length of key exceeds BUFF_MAX\n");
        return false;
    }
    if (strlen(val) > BUFF_MAX - 1)
    {
        fprintf(stderr, "addConst: length of val exceeds BUFF_MAX\n");
        return false;
    }
    if (!isalpha(key[0]))
    {
        fprintf(stderr, "addConst: key must start with an alphabetical character\n");
        return false;
    }
    for (unsigned int i = 1; i < strlen(key); ++i)
    {
        if (!isalnum(key[i]))
        {
            fprintf(stderr, "addConst: key must be alpha-numeric\n");
            return false;
        }
    }
    consts[numConsts] = (char**) malloc(2 * sizeof(char*));
    consts[numConsts][0] = (char*) malloc(BUFF_MAX * sizeof(char));
    consts[numConsts][1] = (char*) malloc(BUFF_MAX * sizeof(char));
    strcpy(consts[numConsts][0], key);
    strcpy(consts[numConsts][1], val);
    ++numConsts;
    if (numConsts == maxConsts) /* Need to expand the array */
    {
        maxConsts *= 2;
        consts = (char***) realloc(consts, maxConsts * sizeof(char***));
    }
    return true;
}

/*
  Get the string associated with the key
  Returns blank string on failure
*/
char* getConst(char *key)
{
    /* Due to time constraints, this will just be a linear search for now */
    for (unsigned int i = 0; i < numConsts; ++i)
    {
        if (strcmp(key, consts[i][0]) == 0) /* Found it */
            return consts[i][1];
    }
    return "";
}

/* Check if the string is a valid operator */
bool isOp(char *s)
{
    if (strlen(s) > 2) /* No operators are more than 2 characters long */
        return false;
    if (strlen(s) == 1) /* Single character operator lets us use a switch statement */
    {
        switch (s[0])
        {
        case ';':
            return true;
        default:
            return false;
        }
    }
    else /* Compare the string to supported 2 character operators */
    {
        const unsigned int NUM_OPS = 2;
        char *ops[NUM_OPS] = { "&&", "||" }; /* All supported 2 character operators */
        for (unsigned int i = 0; i < NUM_OPS; ++i)
        {
            if (strcmp(s, ops[i]) == 0)
                return true;
        }
        return false;
    }
}

/* Check if the string is a pipe */
bool isPipe(char *s)
{ return (s[0] == '|'); }

bool containsOp(char *s)
{
    char substr[3];
    for (int i = 0; i < strlen(s); i++) {
        memcpy(substr,&s[0+i],1);
        substr[1] = '\0';
        if (isOp(substr)) {
            return true;
        }
        if (strlen(s) - i >= 2) {
            memcpy(substr,&s[0+i],2);
            substr[2] = '\0';
            if (isOp(substr)) {
                return true;
            }
        }
    }
    return false;
}

/* Move pos to matching closing brace */
bool matchBrace(char *s, int *pos, const unsigned int maxPos)
{
    if (s[*pos] != '{')
    {
        fprintf(stderr, "matchBrace: position passed is not a brace\n");
        return false;
    }
    unsigned int count = 1; /* Number of opening braces encountered */
    int i = *pos;
    while (count > 0 && i < maxPos)
    {
        ++i;
        if (s[i] == '{')
            ++count;
        if (s[i]=='}')
            --count;
    }
    if (count != 0)
    {
        fprintf(stderr, "matchBrace: matching brace not found\n");
        return false;
    }
    *pos = i;
    return true;
}

/* Move pos to matching closing closing */
bool matchQuote(char *s, int *pos, const unsigned int maxPos)
{
    if (s[*pos] != '\"')
    {
        fprintf(stderr, "matchBrace: position passed is not a quote\n");
        return false;
    }
    int i = *pos;
    while (i < maxPos)
    {
        ++i;
        if (s[i] == '\"')
            break;
    }
    if (s[i] != '\"')
        return false;
    *pos = i;
    return true;
}

/*
 Parse the given expression into a left statement, operator, and right expression
 expr: Expression to be parsed
 s: String to store the resulting statement
 op: String to store the resulting operator
 e: String to store the resulting right expression
*/
bool parseExpr(char *expr, char *s, char *op, char *e)
{
    int pos1 = 0;
    int pos2 = strlen(expr) - 1;
    int i = INVALID_POS;
    int sEnd = INVALID_POS; /* End position of statement */
    int opPos1 = INVALID_POS; /* Start and end positions for operator */
    int opPos2 = INVALID_POS;
    int expStart = INVALID_POS;
    char token[BUFF_MAX] = ""; /* The space delineated token we are considering */
    int tokPos1 = INVALID_POS;
    int tokPos2 = INVALID_POS;
    /* Initialize return values */
    s[0] = '\0';
    op[0] = '\0';
    e[0] = '\0';
    if (strlen(expr) == 0) /* Empty expression passed */
        return true;
    while (pos2 > pos1 && isspace(expr[pos2])) /* Remove trailing whitespace */
        --pos2;
    while (pos1 <= pos2 && isspace(expr[pos1])) /* Ignore leading whitespace */
        ++pos1;
    if (pos1 > pos2) /* Expression consisted of all white space */
        return true; /* Do nothing */
    i = pos1;
    if (expr[i] == '{') /* Statement is a braced expression. Move i to the matching brace */
    {
        bool ok = matchBrace(expr, &i, pos2);
        if (!ok) /* Failed to match brace */
        {
            fprintf(stderr, "parseExpr: failed to match brace\n");
            return false;
        }
        ++i; /* Move past the matched brace */
    }
    /* Move to the operator */
    while (i <= pos2)
    {
        while (i <= pos2 && isspace(expr[i])) /* Move until not whitespace */
            ++i;
        tokPos1 = i;
        while (i <= pos2 && !isspace(expr[i])) /* Move until whitespace */
            ++i;
        tokPos2 = i - 1;
        strncpy(token, expr + tokPos1, tokPos2 - tokPos1 + 1);
        token[tokPos2 - tokPos1 + 1] = '\0';
        if (isOp(token)) /* Found the operator */
        {
            opPos1 = tokPos1;
            opPos2 = tokPos2;
            break;
        }
    }
    if (opPos1 == INVALID_POS) /* No operator, just a statement */
    {
        strncpy(s, expr + pos1, pos2 - pos1 + 1);
        s[pos2 - pos1 + 1] = '\0';
        return true;
    }
    strncpy(op, expr + opPos1, opPos2 - opPos1 + 1); /* Store the operator */
    op[opPos2 - opPos1 + 1] = '\0';
    sEnd = opPos1 - 1;
    strncpy(s, expr + pos1, sEnd - pos1 + 1); /* Store the statement */
    s[sEnd - pos1 + 1] = '\0';
    while (i <= pos2 && isspace(expr[i])) /* Move past white space seperation */
        ++i;
    expStart = i; /* Mark the start of the right hand expression */
    strncpy(e, expr + expStart, pos2 - expStart + 1);
    e[pos2 - expStart + 1] = '\0';
    return true;
}

/*
 Parse a string into a command and list of args
 s: Statement to parse
 maxArgs: The maximum number of arguments argv can store. This includes the NULL terminator
 cmd: String to store the resulting command
 argv: Array of unallocated character pointers to store the resulting arguments
 numArgs: Returns the number of arguments extracted
 isBg: Returns if a & was passed to indicate a background process
*/
bool parseCmd(char *s, const unsigned int maxArgs, char *cmd, char **argv, unsigned int *numArgs, bool *isBg)
{ /* TODO: Update this in accordance with new grammar */
    int pos1 = 0;
    int pos2 = strlen(s) - 1;
    int tokPos1 = INVALID_POS;
    int tokPos2 = INVALID_POS;
    unsigned int i = 0;
    /* Initialize return values */
    cmd[0] = '\0';
    *numArgs = 0;
    *isBg = false;
    if (strlen(s) == 0) /* Empty expression passed */
        return true;
    while (pos2 > pos1 && isspace(s[pos2])) /* Remove trailing whitespace */
        --pos2;
    while (pos1 <= pos2 && isspace(s[pos1])) /* Ignore leading whitespace */
        ++pos1;
    if (pos1 > pos2) /* Expression consisted of all white space */
        return true; /* Do nothing */
    if (pos2 - pos1 > 0 && s[pos2] == '&' && s[pos2 - 1] == ' ') /* & was passed to run process in background */
    {
        *isBg = true;
        /* Remove & from the argument list */
        --pos2;
        while (pos2 > pos1 && isspace(s[pos2]))
            --pos2;
    }
    /* Extract the first space delineated token and store in cmd */
    tokPos1 = tokPos2 = pos1;
    while (tokPos2 <= pos2 && !isspace(s[tokPos2]))
        ++tokPos2;
    strncpy(cmd, s + tokPos1, tokPos2 - tokPos1);
    cmd[tokPos2 - tokPos1] = '\0';
    /* First element of argv is always the name of the command */
    argv[i] = (char*) malloc(BUFF_MAX * sizeof(char));
    strcpy(argv[i], cmd);
    ++i;
    while (i < maxArgs - 1 && tokPos2 < pos2)
    {
        while (tokPos2 < pos2 && isspace(s[tokPos2]))
            ++tokPos2;
        tokPos1 = tokPos2;
        if (s[tokPos1] == '\"') /* Quoted argument */
        {
            bool ok = matchQuote(s, &tokPos2, pos2);
            if (!ok) /* Could not match quote */
            {
                fprintf(stderr, "parseCmd: could not parse all arguments\n");
                /* Terminate argv early */
                argv[i] = NULL;
                *numArgs = i;
                return false;
            }
            argv[i] = (char*) malloc(BUFF_MAX * sizeof(char));
            /* Take away the quotes and copy into argv */
            strncpy(argv[i], s + tokPos1 + 1, tokPos2 - tokPos1 - 1);
            argv[i][tokPos2 - tokPos1 - 1] = '\0';
            ++tokPos2; /* Move past end quote */
        }
        else
        {
            while (tokPos2 <= pos2 && !isspace(s[tokPos2]))
                ++tokPos2;
            argv[i] = (char*) malloc(BUFF_MAX * sizeof(char));
            strncpy(argv[i], s + tokPos1, tokPos2 - tokPos1);
            argv[i][tokPos2 - tokPos1] = '\0';
            evalArg(argv[i]); /* Expand any user defined constants in the arg */
        }
        ++i;
    }
    argv[i] = NULL; /* Terminate list of args with NULL */
    *numArgs = i;
    return true;
}

/*
  Parse the statement into either an expression or a command
  If s is an expression enclosed in braces, it will be stored in e and cmd will be the empty string
  If s is simply a command, it will be stored in cmd and e will be the empty string
  s: The statement to be parsed
  e: String to store the parsed expression
  cmd: String to store the parsed command
*/
bool parseS(char *s, char *e, char *cmd)
{
    int pos1 = 0;
    int pos2 = strlen(s) - 1;
    e[0] = '\0';
    cmd[0] = '\0';
    if (strlen(s) == 0) /* Empty expression passed */
        return false;
    while (pos2 > pos1 && isspace(s[pos2])) /* Remove trailing whitespace */
        --pos2;
    while (pos1 <= pos2 && isspace(s[pos1])) /* Ignore leading whitespace */
        ++pos1;
    if (containsOp(s)) {
        strncpy(e, s + pos1, pos2 - pos1 + 1);
        s[pos2 - pos1 + 1] = '\0';
        return true;
    }
    if (s[pos1] == '{') /* Statement is an expression enclosed in braces */
    {
        if (s[pos2] != '}')
        {
            fprintf(stderr, "parseS: expression not properly enclosed in braces\n");
            return false;
        }
        ++pos1;
        --pos2;
        while (pos2 > pos1 && isspace(s[pos2]))
            --pos2;
        /* Return the expression */
        strncpy(e, s + pos1, pos2 - pos1 + 1);
        s[pos2 - pos1 + 1] = '\0';
        return true;
    }
    /* Statement is a command */
    strncpy(cmd, s + pos1, pos2 - pos1 + 1);
    s[pos2 - pos1 + 1] = '\0';
    return true;
}

/*
  Parse an invocation consisting of a series of commands and redirections
  s: String to be parsed
  cmds: Array of unallocated char pointers to store the parsed commands
  redirs: Array of unallocated char pointers to store the parsed redirection operators
  numCmds: Int to store the number of parsed commands
  numRedirs: Int to store the number of parsed redirection operators
*/
bool parseInvoke(char *s, char **cmds, unsigned int *numCmds, unsigned int *numPipes)
{
    int pos1 = 0;
    int pos2 = strlen(s) - 1;
    int tokPos1 = INVALID_POS;
    int tokPos2 = INVALID_POS;
    char token[BUFF_MAX] = {};
    int i = 0;
    /* Initialize return values */
    *numCmds = 0;
    if (strlen(s) == 0) /* Empty expression passed */
        return true;
    while (pos2 > pos1 && isspace(s[pos2])) /* Remove trailing whitespace */
        --pos2;
    while (pos1 <= pos2 && isspace(s[pos1])) /* Ignore leading whitespace */
        ++pos1;
    if (pos1 > pos2) /* Expression consisted of all white space */
        return true; /* Do nothing */
    i = pos1;
    while (i <= pos2)
    {
        while (i <= pos2 && isspace(s[i]))
            ++i;
        tokPos1 = i;
        while (i <= pos2 && !isspace(s[i]))
            ++i;
        tokPos2 = i;
        strncpy(token, s + tokPos1, tokPos2 - tokPos1);
        token[tokPos2 - tokPos1] = '\0';
        if (isPipe(token)) /* Found a redirection operator */
        {
            i = tokPos1 - 1;
            if (i == -1)
            {
                fprintf(stderr, "parseInvoke: expected left command for redirection operator\n");
                return false;
            }
            while (i > 0 && isspace(s[i]))
                --i;
            /* Save the command */
            cmds[*numCmds] = (char*) malloc(BUFF_MAX * sizeof(char));
            strncpy(cmds[*numCmds], s + pos1, i - pos1 + 1);
            cmds[*numCmds][i - pos1 + 1] = '\0';
            ++(*numCmds);
            ++(*numPipes);
            pos1 = tokPos2;
            while (pos1 <= pos2 && isspace(s[pos1]))
                ++pos1;
        }
        i = tokPos2;
        while (i <= pos2 && isspace(s[i]))
            ++i;
    }
    /* Add the remaining cmd */
    cmds[*numCmds] = (char*) malloc(BUFF_MAX * sizeof(char));
    strcpy(cmds[*numCmds], s + pos1);
    ++(*numCmds);
    /* Terminate with NULL */
    cmds[*numCmds] = NULL;
    return true;
}

/* Evaluate the invocation */
int evalInvoke(char *s)
{
    char **cmds = (char**) malloc(MAX_ARGS * sizeof(char*));
    char **redirs = (char**) malloc(MAX_ARGS * sizeof(char*));
    unsigned int numCmds = 0;
    unsigned int numRedirs = 0;
    parseInvoke(s, cmds, redirs, &numCmds, &numRedirs);

    char cmd[BUFF_MAX] = "";
    char **argv = (char**) malloc(MAX_ARGS * sizeof(char*));
    char **cmdRedirs = (char**) malloc(MAX_ARGS * sizeof(char*));
    char **filenames = (char**) malloc(MAX_ARGS * sizeof(char*));
    unsigned int numArgs = 0;
    unsigned int numRedirsCmd = 0;
    unsigned int numFilenames = 0;
    bool isBg = false;

    if (numCmds == 0)
        return 0;
    

    for (i = 0; i < numCmds; i++) {
        parseCmd(cmds[i],MAX_ARGS,cmd,argv,cmdRedirs,filenames,&numArgs,&numRedirs,&numFilenames,&isBg);

    }
    


    //call ParseInvoke
    //make a loop on the operators
    return 0; /* Placeholder */
}

/*
  Evaluate the argument
  This just expands any user defined constants preceeded by a $
*/
char* evalArg(char *arg)
{
    int pos1 = 0;
    int pos2 = strlen(arg);
    int keyPos1 = INVALID_POS;
    int keyPos2 = INVALID_POS;
    int i = 0;
    char key[BUFF_MAX] = "";
    char *val = NULL;
    char temp[BUFF_MAX] = "";
    temp[0] = '\0';
    while (pos1 < pos2)
    {
        while (i < pos2 && arg[i] != '$')
            ++i;
        if (i == pos2) /* No $ in arg */
            break;
        /* Else found a $ */
        keyPos1 = keyPos2 = i + 1;
        while (keyPos2 <= pos2 && isalnum(arg[keyPos2])) /* Read key until we hit a non-alnum character or end of string */
            ++keyPos2;
        if (keyPos2 - keyPos1 > BUFF_MAX - 1)
        {
            fprintf(stderr, "evalArg: key length exceeds BUFF_MAX\n");
            return arg;
        }
        strncpy(key, arg + keyPos1, keyPos2 - keyPos1);
        key[keyPos2 - keyPos1] = '\0';
        val = getConst(key);
        strncpy(temp + pos1, arg + pos1, i - pos1);
        temp[i] = '\0';
        strcat(temp, val);
        pos1 = i = keyPos2;
    }
    if (pos1 < pos2)
        strcat(temp, arg + pos1); /* Add the remainder of the arg */
    strcpy(arg, temp);
    return arg;
}

/* Evaluate the command and run the executable */
int evalCmd(char *cmd, unsigned int argc, char **argv, bool isBg)
{ /* TODO: Update this in accordance with new grammar */
    char path[BUFF_MAX]; /* String to store the current value of PATH */
    char execPath[BUFF_MAX]; /* Resulting path to the executable we want to run */
    strcpy(path, consts[0][1]); /* Get the current PATH value */
    char *tok = NULL;
    bool isPath = false; /* Is the given command already a path to an executable */
    char *ptr = argv[0];
    int retVal = 0;
    if (*ptr == 'c' && *(++ptr) == 'd') {
        if (argc != 2) {
            printf("Only one argument allowed\n");
            return 1;
        }
        int r = chdir(argv[1]);
        if (r == 0) {
            return 0;
        }
        else {
            printf("cd %s: No such file or directory\n", argv[1]);
            return 1;
        }
    }

    for (unsigned int i = 0; i < strlen(cmd); ++i) /* Look for a '/' to signal that cmd is already a path */
    {
        if (cmd[i] == '/')
        {
            isPath = true;
            break;
        }
    }
    if (isPath)
        strcpy(execPath, cmd);
    else
    {
        tok = strtok(path, ":");
        while (tok != NULL)
        {
            /* Generate possible executable path using value in PATH and cmd */
            strcpy(execPath, tok);
            strcat(execPath, "/");
            strcat(execPath, cmd);
            if (access(execPath, X_OK) != -1) /* Found an appropriate executable */
                break;
            tok = strtok(NULL, ":");
        }
    }
    if (access(execPath, X_OK) != -1)
    {
        pid_t pid = fork();
        if (pid == 0) /* Child process */
        {
            if (isBg)
                setpgid(0,0); /* Put this child into a new process group */
            execv(execPath, argv);
            if (!isBg) {
                int waitstatus;
                wait(&waitstatus);
                retVal = WEXITSTATUS(waitstatus);
            }
            exit(127); /* If process fails */
        }
        else /* Parent process */
        {
            if (isBg)
                return 0; /* Don't wait for child process and just return */
            /* Else wait for process before returning */
            pid_t r = waitpid(pid, 0, 0);
            if (r == -1) /* An error occured */
                return 1;
            return retVal;
        }
    }
    fprintf(stderr, "\'%s\' is not a valid command\n", cmd);
    return 1;
}

/* Evaluate the statement */
int evalS(char *s)
{
    char e[BUFF_MAX] = "";
    char cmd[BUFF_MAX] = "";
    bool ok = parseS(s, e, cmd);
    if (!ok)
        return 1;
    if (strlen(cmd) == 0) /* Statement is a braced expression */
        return evalExpr(e);
    /* Else statement is an invocation */
    return evalInvoke(s);
}

/* Evaluate the expression. Assume that there are no trailing whitespaces */
int evalExpr(char *expr)
{
    char left[BUFF_MAX];
    char op[BUFF_MAX];
    char right[BUFF_MAX];
    int l_code, r_code;
    if (strlen(expr) == 0) /* Empty expression */
        return 0;
    parseExpr(expr,left,op,right);
    if (strlen(op) == 0) /* No operator, just a statement */
        return evalS(expr);
    if (strlen(right) == 0) /* No right expression found */
    {
        fprintf(stderr, "evalExpr: expected right hand expression for operator\n");
        return 1;
    }
    if (strcmp(op, "&&") == 0) /* AND operator */
    {
        l_code = evalS(left);
        if (l_code == 0)
            r_code = evalExpr(right);
    }
    else if (strcmp(op, "||") == 0) /* OR operator */
    {
        l_code = evalS(left);
        if (l_code == 0)
            return 0;
        return evalExpr(right);
    }
    else if (strcmp(op, ";") == 0) /* Evaluate sequentially */
    {
        evalS(left);
        return evalExpr(right);
    }
    else if (strcmp(op, "=") == 0) /* Assignment operator */
    {
        /* Left is the key, right is the val */
        bool ok = addConst(left, right);
        if (!ok)
            return 1;
        return 0;
    }
    return 0;
}

char *read_command(void)
{
    char *command = NULL;
    long unsigned n = 0;
    
    if (getline(&command, &n, stdin) == -1)
        return NULL;
    return command;
}

/* Test driver */
int main()
{
    init();
    char invoke[BUFF_MAX] = "test < testing test test | tester much test >> so test | please << work";
    char *cmds[BUFF_MAX] = {};
    unsigned int numCmds = 0;
    unsigned int numPipes = 0;
    parseInvoke(invoke, cmds, &numCmds, &numPipes);
    printf("Invocation parsed: %s\n", invoke);
    puts("Commands...");
    for (unsigned int i = 0; i < numCmds; ++i)
    {
        puts(cmds[i]);
        free(cmds[i]);
    }
    printf("Number of pipes: %d\n", numPipes);
    finish();
    return 0;
}



/* int main() */
/* { */
/*     init(); */
/*     char d[PATH_MAX]; */
/*     char user[BUFF_MAX]; */
/*     unsigned int i = 0; */
/*     int lastResult; */
/*     if (getcwd(d, sizeof(d)) != NULL) */
/*     { */
/*         //TODO: Account for some kind of invalid read of path */
/*     } */
/*     char *command; */
/*     getlogin_r(user, BUFF_MAX); /\* Get name of current user *\/ */
/*     while (1) */
/*     { */
/*         getcwd(d, sizeof(d)); */
/*         /\* Only print the last directory in the current path *\/ */
/*         i = strlen(d); */
/*         while (i > 0 && d[i - 1] != '/') */
/*             --i; */
/*         printf("%s@soyshell %s> ", user, d + i); */
/*         command = read_command(); */
/*         if (command == NULL) /\* Failed to read input *\/ */
/*         { */
/*             fprintf(stderr, "Failed to read stdin\n"); */
/*             continue; */
/*         } */
/*         int len = strlen(command); */
/*         if (command[len-1] == '\n') */
/*             command[len-1] = 0; */
/*         if (strcmp(command, "exit") == 0) */
/*         { */
/*             if (command != NULL) */
/*                 free(command); */
/*             return 0; */
/*         } */
/*         lastResult = evalS(command); */
/*         free(command); */
/*     } */
/*     finish(); */
/*     return 0; */
/* } */
