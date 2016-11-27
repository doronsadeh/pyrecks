import redis
import os
import sys

def read_file(filename):
    f = open(filename, 'rb')
    text = f.read()
    return text

r = redis.Redis(host='localhost', port=6379)

base = sys.argv[1]
print 'Scanning directory %s' % base

files = [f for f in os.listdir(base) if f.endswith('.py')]

print 'Compacting and setting files into Redis: %s' % files

for f in files:
    name = f[:f.index('.py')]
    text = read_file(os.path.join(base, f))
    print 'SET %s\n%s' % (name, text)
    r.set(name, text)
