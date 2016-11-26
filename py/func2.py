import time
def func2(x):
    print 'func2 got %s' % x
    time.sleep(10)
    x = x + '-and-we-are-done'
    print 'func2 says %s' % x
    return ''

# Reduced version
# SET func2 "import time \ndef func2(x):\n\tprint 'func2 got %s' % x\n\ttime.sleep(10)\n\tx = x + '-and-we-are-done'\n\tprint 'func2 says %s' % x\n\treturn ''"
