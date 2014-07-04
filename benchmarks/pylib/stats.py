import numpy

def stddev(data):
    return numpy.std(data, ddof=0)

def avg(data):
    return numpy.average(data)
