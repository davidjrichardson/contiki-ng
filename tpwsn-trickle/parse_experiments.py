#!/usr/bin/python3

import os
import re
import pickle
import pandas as pd
import numpy as np

from collections import namedtuple
from functools import reduce
from pathlib import Path

from run_experiments import experiment_size

exp_path = Path(os.getcwd(), 'experiments')
exp_re = re.compile(r'(?P<n>\d+)-(?P<t>\w+)-(?P<k>\d+)-(?P<imin>\d+)-(?P<imax>\d+)-(?P<d>\d+)-run(?P<r>\d)')
control_re = re.compile(r'control-(?P<n>\d+)-(?P<t>\w+)-(?P<k>\d+)-(?P<imin>\d+)-(?P<imax>\d+)-(?P<d>\d+)-run(?P<r>\d)')
all_experiments = filter(lambda x: os.path.isdir(str(Path(exp_path, x))), os.listdir(str(exp_path)))
experiments = filter(lambda x: 'control' not in x, all_experiments)
control_experiments = filter(lambda x: 'control' in x, all_experiments)

def parse_logfile(path):
    f = pd.read_csv(str(path), names=['time','msg'], header=None, delimiter=';')
    tx_count = 0
    token_val = -1
    
    for index, row in f.iterrows():
        text = row['msg']
        
        if 'Trickle TX' in text:
            tx_count += 1
        elif 'Current token' in text:
            token_val = int(re.match(r'.*Current\ token:\ (?P<n>\d+)', text).group('n'))
            
    return tx_count, token_val


Experiment = namedtuple('Experiment', ['d', 'k', 'imin', 'n', 't', 'imax'])
control_dict = {}
exp_dict = {}

for exp in control_experiments:
    print('Parsing control experiments: ' + exp)
    exp_dir = Path(exp_path, exp)
    params = control_re.match(exp).groupdict()
    params.pop('r', -1)
    
    experiment = Experiment(**params)
    
    files = filter(lambda x: "run" in x, os.listdir(str(exp_dir)))
    filename_re = re.compile(r'.*\_(?P<id>\d+)\.txt')
    
    experiment_dict = {}
 
    for f in files:
        node_id = filename_re.match(f).group('id')
        
        experiment_dict[node_id] = parse_logfile(Path(exp_dir, f))
    
    total_tx = 0
    for t in experiment_dict.values():
        total_tx += t[0]

    print(total_tx)
        
    if control_dict.get(experiment):
        control_dict[experiment].append(total_tx)
    else:
        control_dict[experiment] = [total_tx]


for exp in experiments:
    print('Parsing experiment: ' + exp)
    exp_dir = Path(exp_path, exp)
    params = exp_re.match(exp).groupdict()
    params.pop('r', -1)
    
    experiment = Experiment(**params)
    
    files = filter(lambda x: "run" in x, os.listdir(str(exp_dir)))
    filename_re = re.compile(r'.*\_(?P<id>\d+)\.txt')
    
    experiment_dict = {}
    
    for f in files:
        node_id = filename_re.match(f).group('id')
        
        experiment_dict[node_id] = parse_logfile(Path(exp_dir, f))
    
    total_tx = 0
    total_has_token = 0
    for t in experiment_dict.values():
        total_tx += t[0]
        
        if t[1] == 1:
            total_has_token += 1
    
    if exp_dict.get(experiment):
        exp_dict[experiment].append((total_tx, (total_has_token / 1.0 * (experiment_size**2)) * 100))
    else:
        exp_dict[experiment] = [(total_tx, (total_has_token / 1.0 * (experiment_size**2)) * 100)]
    
        
with open('experiment_data.pickle', 'wb') as handle:
    pickle.dump(exp_dict, handle, protocol=pickle.HIGHEST_PROTOCOL)

with open('control_data.pickle', 'wb') as handle:
    pickle.dump(control_dict, handle, protocol=pickle.HIGHEST_PROTOCOL)
