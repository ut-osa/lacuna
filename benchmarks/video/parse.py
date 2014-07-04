#!/usr/bin/env python

import os
import re
import sys
import json

# Is this the best way to add our library directory?
libdir = os.path.realpath(os.path.dirname(os.path.realpath(__file__)) + '/../pylib')
sys.path.append(libdir)

from stats import avg,stddev
from util import get_git_rev

sample_cutoff = 0
sample_cutoff_last = 0 # sk: I think _tail sounds better, but this name is kept to match with existing summary file entries
gpu_sample_cutoff = 1

def parse_mpstat(filename):
    global sample_cutoff, sample_cutoff_last

    sample_pattern = re.compile('^[0-9:]+ [AP]M\s+(all|\d+).*?([0-9.]+)$')
    def is_blank_line(l):
        return l == '' or l == '\n'
    def is_sample_start(l):
        return (l.find('CPU') != -1)
    def parse_sample_line(l):
        m = sample_pattern.match(l)
        if m:
            return {'cpu': m.group(1),
                    'usage': 100 - float(m.group(2))}
        else:
            raise Exception('Parse error')

    samples = []
    f = open(filename, 'r')
    l = f.readline()
    while l != '':
        if is_sample_start(l):
            l = f.readline()
            while not is_blank_line(l):
                sample_line = parse_sample_line(l)
                if sample_line['cpu'] == 'all':
                    samples.append(sample_line['usage'])
                l = f.readline()
        else:
            l = f.readline()
    f.close()

    samples = samples[sample_cutoff:(sample_cutoff_last>0) and -sample_cutoff_last or None]
    return samples

def parse_gpu_samples(filename):
    global gpu_sample_cutoff

    sample_pattern = re.compile('Avg_fps=([0-9.]+)')

    samples = []
    for l in open(filename, 'r').readlines():
        m = sample_pattern.match(l)
        if m:
            samples.append(float(m.group(1)))

    if len(samples) == 0:
        return None
    else:
        return samples[gpu_sample_cutoff:]

def parse_args(args):
    opts = {}
    if len(args) < 2 or len(args) > 5:
        raise Exception('num args')
    if len(args) >= 3:
        opts['sample_cutoff'] = int(args[2])
    if len(args) >= 4:
        opts['gpu_sample_cutoff'] = int(args[3])
    if len(args) >= 5:
        opts['sample_cutoff_last'] = int(args[4])
        
    opts['data_dir'] = args[1]
    return opts

def sample_filename(base_dir, num):
    return base_dir + '/' + ('%d.cpu.dat' % num)

def gpu_sample_filename(base_dir, num):
    return base_dir + '/' + ('%d.qemu.log' % num)

def main():
    global sample_cutoff, gpu_sample_cutoff, sample_cutoff_last

    opts = parse_args(sys.argv)
    sample_cutoff = opts.get('sample_cutoff', sample_cutoff)
    sample_cutoff_last = opts.get('sample_cutoff_last', sample_cutoff_last)

    num_runs = 1
    while os.path.exists(sample_filename(opts['data_dir'], num_runs)):
        num_runs += 1
    num_runs -= 1

    runs = []
    for i in range(1,num_runs+1):
        samples_file = sample_filename(opts['data_dir'], i)
        samples = parse_mpstat(samples_file)
        gpu_samples_file = gpu_sample_filename(opts['data_dir'], i)
        gpu_samples = parse_gpu_samples(gpu_samples_file)

        print(samples)

        run = {}
        run['avg'] = avg(samples)
        run['stddev'] = stddev(samples)
        if gpu_samples:
            run['avg_frames'] = avg(gpu_samples)
            run['stddev_frames'] = stddev(gpu_samples)
        runs.append(run)

    result = {}
    result['sample_cutoff'] = sample_cutoff
    result['sample_cutoff_last'] = sample_cutoff_last
    result['gpu_sample_cutoff'] = gpu_sample_cutoff
    result['git_rev'] = get_git_rev(override_clean_check=True)
    result['override_clean_check'] = True
    result['runs'] = runs
    result['enc'] = runs[0].has_key('avg_frames')

    run_avgs = [run['avg'] for run in runs]
    run_stddevs = [run['stddev'] for run in runs]
    result['run_avg'] = avg(run_avgs)
    result['run_stddev'] = stddev(run_avgs)
    result['run_var'] = max(run_stddevs)

    if result['enc']:
        run_avg_frames = [run['avg_frames'] for run in runs]
        run_stddev_frames = [run['stddev_frames'] for run in runs]
        result['run_avg_frames'] = avg(run_avg_frames)
        result['run_stddev_frames'] = stddev(run_avg_frames)
        result['run_var_frames'] = max(run_stddev_frames)

    result_filename = opts['data_dir'] + '/' + 'summary'
    f = open(result_filename, 'w+')
    json.dump(result, f, indent=4)
    f.write('\n')
    f.close()

    print('Wrote summary to %s' % result_filename)

if __name__ == '__main__':
    main()
