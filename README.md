# Pyrecks
## Computational Kernels Runtime in Python Over Redis

### Overview
Pyrecks (Python Redis Computational Kernels System) was modeled after data flow machines running chains of small computational kernels.

Pyrecks is implemented as a Redis module using its low level APIs.

The basic Pyrecks runtime model allows snippets of python code (kernels) to run within an interpreter sandbox, as encapsulated work units, within Redis itself.

Each kernel receives an input, processes it, and returns a call to the next function-to-run, alongside the later's input data.

All kernels (apart from a _start_ script) are read from Redis. Input data may be computed by the previously running kernel, or read from Redis using some KEY. Note that kerenls are not allowed (even though not enforced in this version) to access Redis data while executing, apart from optionally getting input values by KEY. In general a kernel has no side effects.

Each python kernel is running within a thread, allowing Redis to be responsive to other client requests. The above no side-effects approach makes sure that those independent threads would carry no risk to Redis stability and consistency.

Putting the code (kernels) and data (Redis DB data store) in one place, allows both for rapid prototyping in Python alongside optimized execution of data intensive computational kernels chains that rely, and may requires access to, massive amounts of data.

### Installation
The system was developed and tested using Ubuntu 14.04, and Python 2.7.
Please use the same setup in order to avoid any misconfiguration.

If you have any problems make sure the Redis module _Makefile_ contains the correct paths to the python runtime as installed on your OS.

1. Clone and install the latests Redis from github
2. Clone this project (we'll assume you'll be cloning it into ~/dev/pyrecks)
3. Run make in ~/dev/pyrecks/src to get a module.so file
4. Set PYTHONPATH to ~/dev/pyrecks/py

### Running a Simple Example
Run Redis loading the compiled module

    redis-server --loadmodule <path to module.so>

Invoke redis-cli and SET the following sample functions (functions can also be found under ~/dev/pyrecks/py):

    SET func0 "def func0(x):\n\tprint 'func0 got %s' % x\n\tx = x + '3'\n\tprint 'func0 says %s' % x\n\treturn 'func1(\"say hi to func1 with %s\")' % x"

    SET func1 "def func1(x):\n\tprint 'func1 got %s' % x\n\tx = x * 3\n\tprint 'func1 says %s' % x\n\treturn 'func2(\"say hi to func2 with %s\")' % x\n"

    SET func2 "import time \ndef func2(x):\n\tprint 'func2 got %s' % x\n\ttime.sleep(10)\n\tx = x + '-and-we-are-done'\n\tprint 'func2 says %s' % x\n\treturn ''"

From redis-cli run the module command

    PYLD.pyrun start start

See functions' output as it is printed to the terminal window running the redis-server. Note the redis-cli should return immediately with "Py chain done", allowing further commands to be invoked while the functions are still running.

### Details
#### Writing a Pyrecks Python Kernel
A Pyrecks kernel is a function optionally preceded by some import statements. The function has some restrictions (see below), and _must_ return a string carrying a function call, or an empty one (to stop the kernel chain).

A kernel may use any other utility functions as long as the one invoked by the Pyrecks chain returns the next function call as state above.

Each kernel function _must_ be pre-SET into Redis, using its name as KEY.

Any Redis-stored data items (i.e. keys, with values) required by the kernel functions _must_ be in Redis when the kernel function runs, else it would fail, stopping the kernel chain.

##### Example
    import time
    def func0(x):
        print 'func0 got %s' % x
        x = x + '3'
        time.sleep(10)
        print 'func0 says %s' % x
        return 'func1(\"say hi to func1 with %s\")' % x

The above function imports the _time_ package, gets a single string (or KEY whose value would be translated to it value string) as a formal parameter.

It then runs a some string calculations, returning another string comprising the Python call to the next function in the chain. Note that it uses a computed value to build the _func1_ call. Such computed value may also be a KEY name.

Once it returns the next function would be looked up in Redis using its name as KEY (e.g. _func1_), executing the call as depicted in the returned string, and so forth.

##### Kernel Function Format
    import package
    ...
    import package

    def function_name("string" or "@key"):
    	... code to run ...
    	return 'next_function_name(%s)' % (some value or variable, or "@key")

    def _utility_function():
      ...

__Note!__ We currently support _only_ one string formal parameter, or Redis KEY whose value would be provided as the input string.

__Note!__ all imports would be treated as global imports.

__Note!__ 'from X import Y' form is not currently supported.

__Note!__ Access to Redis via network APIs is forbidden.

### Future Development
- Support var args in kernel functions
- Consider a _yield_ based model

---
Contact me at doron.shamia@gmail.com
