def func1(x):
    print 'func1 got %s' % x
    x = x * 3
    print 'func1 says %s' % x
    return 'func2(\"say hi to func2 with %s\")' % x

# Reduced version
def func1(x):\n\tprint 'func1 got %s' % x\n\tx = x * 3\n\tprint 'func1 says %s' % x\n\treturn 'func2(\"say hi to func2 with %s\")' % x\n
