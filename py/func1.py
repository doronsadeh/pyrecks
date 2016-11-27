def func1(x):
    print 'func1 got %s' % x
    x = x * 3
    print 'func1 says %s' % x
    return 'func2(\"say hi to func2 with %s\")' % x
