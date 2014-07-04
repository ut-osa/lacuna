#!/usr/bin/env python

import re
import sys
import os
import json
import socket

# Is this the best way to add our library directory?
libdir = os.path.realpath(os.path.dirname(os.path.realpath(__file__)) + '/../pylib')
sys.path.append(libdir)

from stats import stddev,avg
from util import get_git_rev

start_exp = re.compile('priv_connect_all start: (\d+)')
end_exp = re.compile('priv_connect_all end: (\d+)')
register_exp = re.compile('priv_input_bench_trigger: (\d+)')

def parse_file(filename):
    state = 0
    result = {}
    for l in open(filename, 'r').readlines():
        if state == 0:
            m = start_exp.match(l)
            if m:
                result['start'] = int(m.group(1))
                state = 1
        elif state == 1:
            m = end_exp.match(l)
            if m:
                result['end'] = int(m.group(1))
                state = 2
        elif state == 2:
            m = register_exp.match(l)
            if m:
                result['register'] = int(m.group(1))
                return result
    raise Exception('Parse error in %s' % filename)

def parse_args(args):
    opts = {}
    if len(args) != 3:
        print('Usage: %s <data dir> <comp mhz>' % args[0])
        exit(1)
    opts['data_dir'] = args[1]
    opts['comp_mhz'] = int(args[2])
    return opts

def main():
    opts = parse_args(sys.argv)

    for (root, dirnames, filenames) in os.walk(opts['data_dir']):
        filenames = filter(lambda name: name.endswith('.dat'), filenames)
        break

    data = [parse_file(opts['data_dir'] + '/' + filename) for filename in filenames]
    delta_t1 = [float(datum['end'] - datum['start'])/(opts['comp_mhz']*1000000)
                for datum in data]
    delta_t2 = [float(datum['register'] - datum['start'])/(opts['comp_mhz']*1000000)
                for datum in data]

    result = {}
    result['qemu_delta_t'] = avg(delta_t1)
    result['qemu_delta_t_stddev'] = stddev(delta_t1)
    result['total_delta_t'] = avg(delta_t2)
    result['total_delta_t_sttdev'] = stddev(delta_t2)
    result['hostname'] = socket.gethostname()
    result['comp_mhz'] = opts['comp_mhz']
    result['override_clean_check'] = True
    result['git_rev'] = get_git_rev(result['override_clean_check'])

    result_filename = opts['data_dir'] + '/' + 'summary'
    f = open(result_filename,'w+')
    json.dump(result, f, indent=4)
    f.write("\n")
    f.close()

    print('Wrote summary to %s' % result_filename)

if __name__ == '__main__':
    main()
