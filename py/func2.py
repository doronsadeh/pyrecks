import time
def func2(x):
    print 'func2 got %s' % x
    time.sleep(10)
    x = x + '-and-we-are-done'
    print 'func2 says %s' % x
    return ''
