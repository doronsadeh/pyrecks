def func0(x):
    print 'func0 got %s' % x
    x = x + '3'
    print 'func0 says %s' % x
    return 'func1(\"say hi to func1 with %s\")' % x
