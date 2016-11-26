def func2(x):
    print 'func2 got %s' % x
    x = x + '-and-we-are-done'
    print 'func2 says %s' % x
    return ''

# Reduced version
# SET func2 "def func2(x):\n\tprint 'func2 got %s' % x\n\tx = x + '-and-we-are-done'\n\tprint 'func2 says %s' % x\n\treturn ''\n"
