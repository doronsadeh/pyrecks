#include "../redismodule.h"
#include "../rmutil/util.h"
#include "../rmutil/strings.h"
#include "../rmutil/test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <Python.h>

// TODO handle free for all mallocs

// Prototypes, cause its C, and C is shit
char* PyExecCode(char* code, char* func, char* arg);
void runPyFromRedis(RedisModuleCtx *ctx, RedisModuleString **argv, int argc, char* next_py);

//Create a new module object
PyObject *pNewMod = NULL;

char* next_py_to_run = NULL;

int BCL_Reply(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
        REDISMODULE_NOT_USED(argv);
        REDISMODULE_NOT_USED(argc);
        char *msg = RedisModule_GetBlockedClientPrivateData(ctx);
        return RedisModule_ReplyWithSimpleString(ctx, msg);
}

int BCL_Timeout(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
        REDISMODULE_NOT_USED(argv);
        REDISMODULE_NOT_USED(argc);
        return RedisModule_ReplyWithSimpleString(ctx, "Request timed-out");
}

void BCL_FreeData(void *privdata) {
        RedisModule_Free(privdata);
}

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

        printf("Thread runs function: %s(%s)\n", func, farg);

        // Action starts here, note the next_py_to_run triggers a new thread
        // invoked by runPyFromRedis
        next_py_to_run = PyExecCode(code, func, farg);

        RedisModule_UnblockClient(bc, next_py_to_run);
        return NULL;
}

int startBlockingClient(RedisModuleCtx *ctx,
                        long long timeout,
                        char* code,
                        char* func,
                        char* arg) {
        pthread_t tid;
        RedisModuleBlockedClient *bc = RedisModule_BlockClient(ctx,
                                                               BCL_Reply,
                                                               BCL_Timeout,
                                                               BCL_FreeData,
                                                               timeout);
        /* Now that we setup a blocking client, we need to pass the control
         * to the thread. However we need to pass arguments to the thread:
         * the delay and a reference to the blocked client handle. */
        void **targ = RedisModule_Alloc(sizeof(void*)*5);
        targ[0] = bc;
        targ[1] = func;
        targ[2] = arg;
        targ[3] = code;
        targ[4] = ctx;

        if (pthread_create(&tid, NULL, BCL_ThreadMain, targ) != 0) {
                RedisModule_AbortBlock(bc);
                return RedisModule_ReplyWithError(ctx,"-ERR Can't start thread");
        }

        return 0;
}

char* PyExecCode(char* code, char* func, char* arg)
{
        printf("Starting PyExecCode: running %s(%s) with code:\n%s\n", func, arg, code);

        PyObject *pName, *pModule, *pArgs, *pValue, *pFunc;
        PyObject *pGlobal = PyDict_New();
        PyObject *pLocal;

        Py_Initialize();
        PyModule_AddStringConstant(pNewMod, "__file__", "");

        //Get the dictionary object from my module so I can pass this to PyRun_String
        pLocal = PyModule_GetDict(pNewMod);

        //Define my function in the newly created module
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
                rv = (char*)malloc(strlen(ret_str) + 1);
                rv = strcpy(rv, ret_str);

                printf("Returned val: %s\n", rv);
        }
        else {
                printf("NULL pValue returned\n");
        }

        Py_DECREF(pValue);

        Py_XDECREF(pFunc);
        // Noooooooooooo u don't! we have one global temp module ... Py_DECREF(pNewMod);
        Py_Finalize();

        return rv;

}

void runPyFromRedis(RedisModuleCtx *ctx, RedisModuleString **argv, int argc, char* next_py) {

        // Kill it so we won't get multiple calls while running
        next_py_to_run = NULL;

        printf("Next py is: '%s'\n", next_py);

        int lpar = strstr(next_py, "(") - next_py;
        int rpar = strstr(next_py, ")") - next_py;

        char* next_func = (char*)malloc(lpar + 1);
        strncpy(next_func, next_py, lpar);
        next_func[lpar] = '\0';
        printf("Next function is: '%s'\n", next_func);

        // TODO this is a very shaky code, we need to strip leading/trailing dquotes w/o this +-2
        char* next_func_arg = (char*)malloc(rpar - lpar + 1);
        strncpy(next_func_arg, strstr(next_py, "(") + 2, rpar - lpar);
        next_func_arg[rpar - lpar - 3] = '\0';
        printf("Next function arg is: '%s'\n", next_func_arg);

        RedisModuleString* rms = RedisModule_CreateString(ctx, next_func, strlen(next_func));

        // Get the next chunk of py code from Redis
        RedisModuleCallReply *reply = RedisModule_Call(ctx, "GET", "s", rms);

        // printf("Reply type: %d\n", RedisModule_CallReplyType(reply));

        if (NULL != reply) {
                size_t len;
                char* code = RedisModule_CallReplyStringPtr(reply, &len);
                if (NULL != code) {
                        printf("Next code is at: %s, code is:\n%s\n", next_py, code);

                        char* xcode = (char*)malloc(strlen(code) + strlen(next_func) + 1024);
                        // sprintf(xcode, "%s\n%s", code, next_py);
                        sprintf(xcode, "%s", code);

                        printf("Dispatching thread: %s\n", xcode);

                        startBlockingClient(ctx,
                                            (long long)(1*1000),
                                            xcode,
                                            next_func,
                                            next_func_arg);

                        // This should be legit, as runPyFromRedis dispatches a thread
                        while (NULL == next_py_to_run) {
                            sleep(1);
                        }

                        printf("Code ran and returned: '%s'\n", next_py_to_run);

                        // Do we need to chain to another function
                        if (strlen(next_py_to_run) > 0) {
                                char* _c_next_py_to_run = (char*)malloc(strlen(next_py_to_run) + 1);
                                strcpy(_c_next_py_to_run, next_py_to_run);
                                runPyFromRedis(ctx, NULL, 0, _c_next_py_to_run);
                                free(_c_next_py_to_run);
                        }
                }
        }
        else {
                printf("NULL reply when GET %s\n", (char*)RedisModule_StringPtrLen(rms, NULL));
        }
}


char* runPyMainModule(char** argv, int argc) {
        PyObject *pName, *pModule, *pDict, *pFunc;
        PyObject *pArgs, *pValue;
        int i;

        if (argc < 3) {
                fprintf(stderr,"Usage: call pythonfile funcname [args]\n");
                return NULL;
        }

        char* py_module = (char*)RedisModule_StringPtrLen(argv[1], NULL);
        char* py_func = (char*)RedisModule_StringPtrLen(argv[2], NULL);

        fprintf(stdout, "py env: %s, calling %s.%s\n", getenv("PYTHONPATH"), py_module, py_func);

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
                                        fprintf(stderr, "Cannot convert argument\n");
                                        return NULL;
                                }
                                /* pValue reference stolen here: */
                                PyTuple_SetItem(pArgs, i, pValue);
                        }
                        pValue = PyObject_CallObject(pFunc, pArgs);
                        Py_DECREF(pArgs);
                        if (pValue != NULL) {
                                char* rv = PyString_AsString(pValue);
                                printf("Result of call:\n%s\n", rv);
                                Py_DECREF(pValue);
                                return rv;
                        }
                        else {
                                Py_DECREF(pFunc);
                                Py_DECREF(pModule);
                                PyErr_Print();
                                fprintf(stderr,"Call failed\n");
                                return NULL;
                        }
                }
                else {
                        if (PyErr_Occurred())
                                PyErr_Print();
                        fprintf(stderr, "Cannot find function \"%s\"\n", argv[2]);
                }
                Py_XDECREF(pFunc);
                Py_DECREF(pModule);
        }
        else {
                PyErr_Print();
                fprintf(stderr, "Failed to load \"%s\"\n", argv[1]);
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
        printf("next_py: %s, %d\n", next_py, strlen(next_py));

        // Get the next function to run, and start the chain
        runPyFromRedis(ctx, argv, argc, next_py);

        printf("\nDone running python function chain, threads may still be running\n\n");
        next_py = "Done";

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
