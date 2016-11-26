def func0(x):
    print 'func0 got %s' % x
    x = x + '3'
    print 'func0 says %s' % x
    return 'func1(\"say hi to func1 with %s\")' % x

# Reduced version
# SET func0 "def func0(x):\n\tprint 'func0 got %s' % x\n\tx = x + '3'\n\tprint 'func0 says %s' % x\n\treturn 'func1(\"say hi to func1 with %s\")' % x"
