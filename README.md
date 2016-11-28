# Pyrecks
## Computational Kernels Runtime in Python Over Redis

### Overview
Pyrecks (Python Redis Computational Kernels System) was modeled after data flow machines running chains of small computational kernels.

Pyrecks is implemented as a Redis module using its low level APIs.

The basic Pyrecks runtime model allows snippets of python code (kernels) to run within an interpreter sandbox, as encapsulated work units, within Redis itself.

Each kernel receives an input, processes it, and returns a call to the next function-to-run, alongside the latter input data.

All kernels (apart from a _start_ script) are read from Redis. Input data may be computed by the previously running kernel, or read from Redis using some KEY. Note that kerenls are not allowed (even though not enforced in this version) to access Redis data while executing, apart from optionally getting input values by KEY. In general a kernel _must not_ have any side effects.

Each python kernel is running within a pthread, allowing Redis to be responsive to other client requests. The above _no side-effects_ approach makes sure that those independent threads would carry no risk to Redis stability and consistency.

Putting the code (kernels) and data (Redis DB data store) in one place, allows for both rapid prototyping in Python, alongside optimized execution of data intensive computational kernels chains that rely, and may requires access to, massive amounts of data.

### Installation
The system was developed and tested using Ubuntu 14.04, and Python 2.7.
Please use the same setup in order to avoid any misconfiguration.

If you have any problems make sure the Redis module _Makefile_ contains the correct paths to the python runtime as installed on your OS.

1. Clone and install the latest Redis from github
2. Clone this project (we'll assume you'll be cloning it into ~/dev/pyrecks)
3. Run make in ~/dev/pyrecks/src to get a module.so file
4. Set PYTHONPATH to ~/dev/pyrecks/py

### Running a Simple Example
Run Redis loading the compiled module

    redis-server --loadmodule ~/dev/pyrecks/src/module.so

Invoke redis-cli and SET the following sample functions (functions can also be found under ~/dev/pyrecks/py):

__Note you can now run _~/dev/pyrecks/tools/install.py ~/dev/pyrecks/py_ to SET all the sample kernels provided into Redis. Or manually enter them as specified below.__

    SET func0 "def func0(x):\n\tprint 'func0 got %s' % x\n\tx = x + '3'\n\tprint 'func0 says %s' % x\n\treturn 'func1(\"say hi to func1 with %s\")' % x"

    SET func1 "def func1(x):\n\tprint 'func1 got %s' % x\n\tx = x * 3\n\tprint 'func1 says %s' % x\n\treturn 'func2(\"say hi to func2 with %s\")' % x\n"

    SET func2 "import time \ndef func2(x):\n\tprint 'func2 got %s' % x\n\ttime.sleep(10)\n\tx = x + '-and-we-are-done'\n\tprint 'func2 says %s' % x\n\treturn ''"

Make sure the script at ~/dev/pyrecks/start.py calls your first function of choice, e.g. calls _func0_ by returning a call string such as : _return 'func0("start ...")'_. Note the string parameter for _func0_ can be anything.

From redis-cli run the module command

    PYLD.pyrun start start

See functions' output as it is printed to the terminal window running the redis-server. Note the redis-cli should return immediately with "py chain done", allowing further commands to be invoked while the functions may still be running.

### Details
#### Writing a Pyrecks Python Kernel
A Pyrecks kernel is a function optionally preceded by some import statements. The function has some restrictions (see below), and _must_ return a string carrying a function call, or an empty one stopping the kernel chain.

A kernel may use any other utility functions as long as the entry-point function invoked by the Pyrecks chain returns the next function call as state above.

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

The above function imports the _time_ module, gets a single string (or KEY whose value would be translated into its value string) as a formal parameter.

It then runs a some string calculations, returning another string comprising the Python call to the next function in the chain. Note that it uses a computed value to build the _func1_ call. Such computed value may also be a KEY name, using the _"@key"_ notation (the double quotes are mandatory).

Once the function returns the next function would be looked up in Redis using its name as KEY (i.e. _func1_), executing the call as stated in the returned string, and so forth.

##### Kernel Function Format
    import module
    ...
    import module

    def function_name("string" or "@key"):
    	... code to run ...
    	return 'next_function_name(%s)' % (some value or variable, or "@key")

    def _utility_function():
      ...


- We currently support _only one_ string formal parameter, or Redis KEY whose value would be provided as the input string.
- Kernels have no module, they are automatically run within one internal module created by the Pyrecks runtime on startup.
- All imports would be treated as global imports.
- The 'from X import Y' form is not currently supported.
- Access to Redis via network APIs is forbidden form within a kernel (no side effects approach).

### Future Development
- Remove the _start.py_ script, and run the first in line kernel from a Redis KEY directly.

- Support var args in kernel functions, of any type. Due to the nature of the PY/C bridge we may need to enforce a _type_ declaration scheme (in Python!) to tell the module, ahead of time, how to interpret and prepare each argument.

- Add a syntactical element to the kernels' return statement, where they provide a set of key-value pairs to be SET in Redis. This way we can allow the kernels to be stateless threads, no-side-effects capsules, that can save any requested state in the Redis itself. The saved state can be used by the same kernel, or other kernels.
This may look something like:

  <pre>return "next_func_to_call(%s, %s, %s) SET [(%s,%s), (%s, %s)]" % (x, y, z, key1, value1, key2, value2)</pre>

- Same as above using other Redis commands, or even a full Redis statement to be carried out on returning from the function, and before running the next in chain.

- Compile each kernel on-demand (i.e. when it is first accessed). Put the compiled version under a KEY comprised of the function name concatenated to the string _pyc_. When a kernel needs to be run (called by a previous one), first check if there is a compiled version, and use it. Else compile, save the compiled version and run.<br><br>
Keep a Redis struct under the function KEY, where we store the original Python script, its compiled version, and a flag telling us if it is a new script that was not yet accessed/compiled.<br><br>
New, or modified, scripts will have this flag set to off, causing them to be compiled on-demand.<br><br>
This way we can keep the ability to hot-patch any function in mid-execution, and still be able to use the compiled pyc instead of recompiling each time.

- [done] ~~Provide a simple Python script to take any number of Python kernel scripts, transform them to one-liners, and SET them in Redis as a batch. This should simplify the preparation process.~~


---
For any questions please contact me at __*doron.shamia@gmail.com*__
