#include "../redismodule.h"
#include "../rmutil/util.h"
#include "../rmutil/strings.h"
#include "../rmutil/test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <Python.h>

// Prototypes, cause its C, and C is shit
char* PyExecCode(char* code, char* func, char* arg);
void runPyFromRedis(RedisModuleCtx *ctx, RedisModuleString **argv, int argc, char* next_py);

//Create a new module object
PyObject *pNewMod = NULL;

// [DISABLED BLOCKING CLIENT]
// int BCL_Reply(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
//         REDISMODULE_NOT_USED(argv);
//         REDISMODULE_NOT_USED(argc);
//         char *msg = RedisModule_GetBlockedClientPrivateData(ctx);
//         return RedisModule_ReplyWithSimpleString(ctx, msg);
// }
//
// int BCL_Timeout(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
//         REDISMODULE_NOT_USED(argv);
//         REDISMODULE_NOT_USED(argc);
//         return RedisModule_ReplyWithSimpleString(ctx, "Request timed-out");
// }
//
// void BCL_FreeData(void *privdata) {
//         RedisModule_Free(privdata);
// }

/* The thread entry point that actually executes the blocking part
 * of the command HELLO.BLOCK. */
void *BCL_ThreadMain(void *arg) {
        void **targ = arg;
        RedisModuleBlockedClient *bc = targ[0];
        char* func = (char*)targ[1];
        char* farg = (char*)targ[2];
        char* code = (char*)targ[3];
        RedisModuleCtx *ctx = (RedisModuleCtx*)targ[4];

        RedisModule_Free(targ);

        printf("[Thread] Thread runs function: %s(%s)\n", func, farg);

        char* next_py = PyExecCode(code, func, farg);

        // Do we need to chain to another function
        if (NULL != next_py && strlen(next_py) > 0) {
                printf("[Thread] Code ran and returned: '%s'\n", next_py);
                runPyFromRedis(ctx, NULL, 0, next_py);
        }
        else {
                printf("\n--------------------------\nPython kernel chain done\n--------------------------\n");
        }

        // [DISABLED BLOCKING CLIENT]
        // int mockint = 0;
        // RedisModule_UnblockClient(bc, &mockint);
        return NULL;
}

int startBlockingClient(RedisModuleCtx *ctx,
                        long long timeout,
                        char* code,
                        char* func,
                        char* arg) {
        pthread_t tid;
        // [DISABLED BLOCKING CLIENT]
        // RedisModuleBlockedClient *bc = RedisModule_BlockClient(ctx,
        //                                                        BCL_Reply,
        //                                                        BCL_Timeout,
        //                                                        BCL_FreeData,
        //                                                        timeout);
        /* Now that we setup a blocking client, we need to pass the control
         * to the thread. However we need to pass arguments to the thread:
         * the delay and a reference to the blocked client handle. */
        void **targ = RedisModule_Alloc(sizeof(void*)*5);
        // [DISABLED BLOCKING CLIENT] targ[0] = bc;
        targ[0] = NULL;
        targ[1] = func;
        targ[2] = arg;
        targ[3] = code;
        targ[4] = ctx;

        if (pthread_create(&tid, NULL, BCL_ThreadMain, targ) != 0) {
                // [DISABLED BLOCKING CLIENT] RedisModule_AbortBlock(bc);
                return RedisModule_ReplyWithError(ctx,"-ERR Can't start thread");
        }

        return 0;
}

char *strstrip(char *s)
{
        size_t size;
        char *end;
        size = strlen(s);

        if (!size)
                return s;

        end = s + size - 1;
        while (end >= s && isspace(*end))
                end--;
        *(end + 1) = '\0';

        while (*s && isspace(*s))
                s++;

        return s;
}

char* extractImportedPackageName(char* token) {
        char* m = strstr(token, "import ");
        m += strlen("import ");
        m = strstrip(m);
        return m;
}

int importSingleModule(char* token, PyObject *pGlobal) {
        if (strstr(token, "import ") - token == 0) {
                // Starts with import ...
                char* package = extractImportedPackageName(token);

                if (NULL != package) {
                        printf("[IMPORT] Importing package %s\n", package);
                        PyObject *impmod = PyImport_ImportModule(package);
                        if (NULL == impmod)
                                printf("[IMPORT] Package %s not found\n", package);
                        else {
                                printf("[IMPORT] Package %s imported\n", package);
                                PyMapping_SetItemString(pGlobal, package, impmod);
                                return 1;
                        }
                }
        }

        return 0;
}

char* importModules(char* code, PyObject *pGlobal) {
        char* trimmed_code = (char*)RedisModule_Alloc(sizeof(char)*(strlen(code) + 1));
        trimmed_code[0] = '\0';

        char* token = strtok(code, "\n");

        while( token != NULL ) {
                // [DEBUG] printf( "token: %s\n", token);
                int imported = importSingleModule(token, pGlobal);
                if (!imported) {
                        sprintf(trimmed_code, "%s%s\n", trimmed_code, token);
                }
                token = strtok(NULL, "\n");
        }

        return trimmed_code;
}

char* PyExecCode(char* code, char* func, char* arg)
{
        printf("[PyExecCode] running %s(%s) with code:\n%s\n", func, arg, code);

        PyObject *pName, *pModule, *pArgs, *pValue, *pFunc;
        PyObject *pGlobal = PyDict_New();
        PyObject *pLocal;

        Py_Initialize();
        PyModule_AddStringConstant(pNewMod, "__file__", "");

        // Extract and import all modules used by the code
        char* code_copy = (char*)RedisModule_Alloc(sizeof(char)*(strlen(code) + 1));
        memcpy(code_copy, code, strlen(code));
        code_copy[strlen(code)] = '\0';
        code = importModules(code_copy, pGlobal);

        //Get the dictionary object from my module so I can pass this to PyRun_String
        pLocal = PyModule_GetDict(pNewMod);

        //Define my function in the newly created module
        printf("\n------------------------ Kernel Running ------------------------\n");
        pValue = PyRun_String(code, Py_file_input, pGlobal, pLocal);

        //pValue would be null if the Python syntax is wrong, for example
        if (pValue == NULL) {
                if (PyErr_Occurred()) {
                        PyErr_Print();
                }
                return NULL;
        }

        //pValue is the result of the executing code,
        //chuck it away because we've only declared a function
        Py_DECREF(pValue);

        //Get a pointer to the function I just defined
        pFunc = PyObject_GetAttrString(pNewMod, func);

        //Build a tuple to hold my arguments
        pArgs = PyTuple_New(1);
        pValue = PyString_FromString(arg);
        int success = PyTuple_SetItem(pArgs, 0, pValue);

        //Call my function, passing it the single string arg
        pValue = PyObject_CallObject(pFunc, pArgs);
        Py_DECREF(pArgs);

        char* rv = NULL;
        if (NULL != pValue) {
                char* ret_str = PyString_AsString(pValue);
                rv = (char*)RedisModule_Alloc(sizeof(char)*(strlen(ret_str) + 1));
                rv = strcpy(rv, ret_str);

                printf("[PyExecCode] Python kernel returned: %s\n", rv);
                Py_DECREF(pValue);
        }
        else {
                printf("[PyExecCode] Python kernel returned no value, please check kernel correctness\n");
        }

        printf("\n----------------------------------------------------------------\n");

        Py_XDECREF(pFunc);
        // Noooooooooooo u don't! we have one global temp module ... Py_DECREF(pNewMod);
        Py_Finalize();

        return rv;

}

char* getValueByKey(RedisModuleCtx *ctx, char* key) {
        char* value = NULL;

        printf("[getValueByKey] Key is: %s\n", key);

        RedisModuleString* rms = RedisModule_CreateString(ctx, key, strlen(key));
        RedisModuleCallReply *reply = RedisModule_Call(ctx, "GET", "s", rms);

        if (NULL != reply) {
                size_t len;
                const char* replyStr = RedisModule_CallReplyStringPtr(reply, &len);
                if (NULL != replyStr) {
                        printf("[getValueByKey] Reply string: %s\n", replyStr);
                        value = (char*)RedisModule_Alloc(sizeof(char)*(strlen(replyStr) + 1));
                        strcpy(value, replyStr);
                        value[strlen(replyStr)] = '\0';
                        if (NULL != value) {
                                value = strstrip(value);
                                printf("[REDIS] Got value for key [%s]:\n%s\n\n", key, value);
                        }
                }
                else {
                  printf("Cannot find key %s, aborting\n", key);
                }
        }

        return value;
}

void runPyFromRedis(RedisModuleCtx *ctx, RedisModuleString **argv, int argc, char* next_py) {

        printf("[runPyFromRedis] Next python kernel is: '%s'\n", next_py);

        int lpar = strstr(next_py, "(") - next_py;
        int rpar = strstr(next_py, ")") - next_py;

        char* next_func = (char*)RedisModule_Alloc(sizeof(char)*(lpar + 1));
        strncpy(next_func, next_py, lpar);
        next_func[lpar] = '\0';
        printf("[runPyFromRedis] \tNext function is: '%s'\n", next_func);

        // TODO this is a very shaky code, we need to strip leading/trailing dquotes w/o this +-2
        char* next_func_arg = (char*)RedisModule_Alloc(sizeof(char)*(rpar - lpar + 1));
        strncpy(next_func_arg, strstr(next_py, "(") + 2, rpar - lpar);
        next_func_arg[rpar - lpar - 3] = '\0';
        printf("[runPyFromRedis] \tNext function arg is: '%s'\n", next_func_arg);

        if (next_func_arg[0] == '@') {
                // It's a Redis KEY, we need to get the value
                char* value = getValueByKey(ctx, strstrip(next_func_arg + 1));
                if (NULL == value) {
                        printf("[runPyFromRedis] Cannot get %s required value from Redis, aborting chain\n", next_func_arg);
                        return;
                }
                else {
                        next_func_arg = value;
                }
        }

        char* code = getValueByKey(ctx, next_func);
        if (NULL != code) {
                printf("[runPyFromRedis] Next code is at: %s, code is:\n%s\n", next_py, code);

                char* xcode = (char*)RedisModule_Alloc(sizeof(char)*(strlen(code) + strlen(next_func) + 1024));
                // sprintf(xcode, "%s\n%s", code, next_py);
                sprintf(xcode, "%s", code);

                printf("[runPyFromRedis] Dispatching thread: %s\n", xcode);

                startBlockingClient(ctx,
                                    (long long)(1*1000),
                                    xcode,
                                    next_func,
                                    next_func_arg);
        }
}


char* runPyMainModule(RedisModuleString** argv, int argc) {
        PyObject *pName, *pModule, *pDict, *pFunc;
        PyObject *pArgs, *pValue;
        int i;

        if (argc < 3) {
                fprintf(stderr, "[runPyMainModule] Usage: call pythonfile funcname [args]\n");
                return NULL;
        }

        char* py_module = (char*)RedisModule_StringPtrLen(argv[1], NULL);
        char* py_func = (char*)RedisModule_StringPtrLen(argv[2], NULL);

        printf("[INFO] py env: %s, calling %s.%s\n", getenv("PYTHONPATH"), py_module, py_func);

        Py_Initialize();
        pName = PyString_FromString(py_module);
        /* Error checking of pName left out */

        pModule = PyImport_Import(pName);
        Py_DECREF(pName);

        if (pModule != NULL) {
                pFunc = PyObject_GetAttrString(pModule, py_func);
                /* pFunc is a new reference */

                if (pFunc && PyCallable_Check(pFunc)) {
                        pArgs = PyTuple_New(argc - 3);
                        for (i = 0; i < argc - 3; ++i) {
                                pValue = PyInt_FromLong(atoi((char*)RedisModule_StringPtrLen(argv[i + 3], NULL)));
                                if (!pValue) {
                                        Py_DECREF(pArgs);
                                        Py_DECREF(pModule);
                                        fprintf(stderr, "[runPyMainModule] Cannot convert argument\n");
                                        return NULL;
                                }
                                /* pValue reference stolen here: */
                                PyTuple_SetItem(pArgs, i, pValue);
                        }
                        pValue = PyObject_CallObject(pFunc, pArgs);
                        Py_DECREF(pArgs);
                        if (pValue != NULL) {
                                char* rv = PyString_AsString(pValue);
                                printf("[runPyMainModule] Result of call:\n%s\n", rv);
                                Py_DECREF(pValue);
                                return rv;
                        }
                        else {
                                Py_DECREF(pFunc);
                                Py_DECREF(pModule);
                                PyErr_Print();
                                fprintf(stderr,"[runPyMainModule] Call failed\n");
                                return NULL;
                        }
                }
                else {
                        if (PyErr_Occurred())
                                PyErr_Print();
                        fprintf(stderr, "[runPyMainModule] Cannot find function \"%s\"\n", (char*)RedisModule_StringPtrLen(argv[2], NULL));
                }
                Py_XDECREF(pFunc);
                Py_DECREF(pModule);
        }
        else {
                PyErr_Print();
                fprintf(stderr, "[runPyMainModule] Failed to load \"%s\"\n", (char*)RedisModule_StringPtrLen(argv[1], NULL));
                return NULL;
        }

        Py_Finalize();
        return NULL;
}

int PyRunCommand_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
        // we must have at least 2 args
        if (argc < 2) {
                return RedisModule_WrongArity(ctx);
        }

        // init auto memory for created strings
        RedisModule_AutoMemory(ctx);

        // Start with a main python read from file
        char* next_py = runPyMainModule(argv, argc);
        printf("[PYLD.pyrun] Next Python kernel to run: %s, %d\n", next_py, (int)strlen(next_py));

        // Get the next function to run, and start the chain
        runPyFromRedis(ctx, argv, argc, next_py);

        printf("\n-----------------------------------------------------------------------------\n[PYLD.pyrun] Done running python function chain, threads may still be running\n-----------------------------------------------------------------------------\n\n");
        next_py = "py chain done";

        // Return final value (not really something to write home about)
        RedisModule_ReplyWithStringBuffer(ctx, next_py, strlen(next_py));
        return REDISMODULE_OK;
}

// ============================================================================

/* This function must be present on each Redis module. It is used in order to
 * register the commands into the Redis server. */
int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
        REDISMODULE_NOT_USED(argv);
        REDISMODULE_NOT_USED(argc);

        if (RedisModule_Init(ctx,"PYLD",1,REDISMODULE_APIVER_1)
            == REDISMODULE_ERR) return REDISMODULE_ERR;

        // A dummy py module, created once, and never DECREF-ed, to encapsulate
        // all in-redis py codes
        pNewMod = PyModule_New("redis_py_internal_module");

        /* an internal hook to get directly to the py runner, mainly for debug purposes */
        if (RedisModule_CreateCommand(ctx,"PYLD.pyrun",
                                      PyRunCommand_RedisCommand,"",0,0,0) == REDISMODULE_ERR)
                return REDISMODULE_ERR;

        return REDISMODULE_OK;
}
