#!/usr/bin/env python3

def num( x ):
    try:
        return int(x)
    except:
        return 0

def max99( x ):
    i = num(x)
    if i > 99:
        i = 99
    return i

def version( descr ):
    # descr expected as $(git describe --dirty=+) output: major[.minor[.patch]][-commit[-ghash]][+]
    dirty = False
    if descr[-1] == '+':
        dirty = True
        descr = descr[:-1]
    dash = descr.split('-')
    dot  = dash[0].split('.')
    v = 0
    i = 0
    while (i < 3) and (i < len(dot)):
        v += max99(dot[i])
        v *= 100
        i += 1
    while i < 3:
        v *= 100
        i += 1

    if len(dash) > 1:
        v += max99(dash[1])
    v *= 100

    if not dirty:
        with open('buildnum', "w") as f:
            f.write( "0\n" )
        return str(v) + "-" + descr

    buildnum = 0
    try:
        with open('buildnum', "r") as f:
            buildnum = f.readline().rstrip('\n')
    except:
        pass
    buildnum = num(buildnum) + 1

    with open('buildnum', "w") as f:
        f.write( str(buildnum) + "\n" )
    v += max99(buildnum)
    return str(v) + "-" + descr

if __name__ == "__main__":
    import os
    stream = os.popen('git describe --dirty=+')
    gitvers = stream.read().rstrip('\n')
    print( version(gitvers) )
