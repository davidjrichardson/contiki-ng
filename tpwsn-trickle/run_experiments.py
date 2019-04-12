import numpy as np
import pandas as pd
import os
import itertools
import functools
import html
import subprocess
import re
import math
import socket
import tqdm
import datetime
import pickle

from collections import namedtuple, defaultdict
from lxml import etree
from multiprocessing import Pool, Manager
from pathlib import Path

hostname = socket.gethostname()

contiki_dir = Path('..')
experiment_dir = Path(contiki_dir, 'tpwsn-trickle/experiments')
abs_dir = Path(os.path.dirname(os.path.abspath(__file__)))

script_template = Path(contiki_dir, 'tpwsn-trickle/sim-script.js')
sim_template = Path(contiki_dir, 'tpwsn-trickle/sim_template.csc')

Experiment = namedtuple('Experiment', ['d', 'k', 'imin', 'n', 't', 'imax'])

mote_failure_probability = 200

if hostname == 'grace-01':
    num_threads = 24
else:
    num_threads = 12

memory_size = '-mx2048m'

# Experiment params
repeats = range(0, 20)

# Set the failure range per-compute node
if hostname == 'grace-01':
    redundancy_range = range(1, 3)
    imin_range = [4, 8]
    imax_range = range(5, 12, 2)
elif hostname == 'grace-02':
    redundancy_range = range(1, 3)
    imin_range = [4, 8]
    imax_range = range(13, 18, 2)
elif hostname == 'grace-03':
    redundancy_range = range(1, 3)
    imin_range = [16, 32]
    imax_range = range(5, 12, 2)
elif hostname == 'grace-04':
    redundancy_range = range(1, 3)
    imin_range = [16, 32]
    imax_range = range(13, 18, 2)
elif hostname == 'grace-05':
    redundancy_range = range(3, 5)
    imin_range = [4, 8]
    imax_range = range(5, 12, 2)
elif hostname == 'grace-06':
    redundancy_range = range(3, 5)
    imin_range = [4, 8]
    imax_range = range(13, 18, 2)
elif hostname == 'grace-07':
    redundancy_range = range(3, 5)
    imin_range = [16, 32]
    imax_range = range(5, 12, 2)
elif hostname == 'grace-08':
    redundancy_range = range(3, 5)
    imin_range = [16, 32]
    imax_range = range(13, 18, 2)
else:
    redundancy_range = range(1, 5)
    imin_range = [4, 8, 16, 32]
    imax_range = range(5, 18, 2)

# Constant parameter space between nodes
experiment_fail_modes = ["random", "location"]
experiment_delay_range = range(1, 18, 2)
experiment_max_fail_range = range(1, 17, 2)

experiment_size = 15 # Number of motes along one axis (forms a square)
experiment_space = list(itertools.product(experiment_delay_range, experiment_fail_modes, redundancy_range, 
                                          imin_range, imax_range, experiment_max_fail_range, repeats))

control_recovery_range = [0]
control_fail_range = [0]
control_fail_mode = ["none"]

control_space = list(itertools.product(control_fail_range, control_fail_mode, redundancy_range, imin_range, 
                                       imax_range, control_recovery_range, repeats))
    

def parse_experiment(filename):
    with open(filename, "rt") as log_file:
        data_raw = map(str.strip, log_file.readlines())
        stats = {}

        msgs_sent_exp = re.compile(r'Messages sent: (?P<count>\d+)')
        total_crashes_exp = re.compile(r'Total crashes: (?P<crashes>\d+)')
        failed_motes_exp = re.compile(r'Motes currently failed \(\d+\): {(?P<motes>.+)}')
        reporting_correct_exp = re.compile(r'Motes reporting correctly: (?P<correct>\d+)')
        reporting_incorrectly_exp = re.compile(r'Motes reporting incorrectly: \[(?P<incorrect>.+)\]')
        coverage_exp = re.compile(r'Coverage: (?P<coverage>\d+)')
        mote_name_exp = re.compile(r'(?P<mote>Sky \d+)')

        for line in data_raw:
            if msgs_sent_exp.match(line):
                stats["messages"] = msgs_sent_exp.match(line).groupdict().get("count")
            elif total_crashes_exp.match(line):
                stats["total_crashes"] = total_crashes_exp.match(line).groupdict().get("crashes")
            elif failed_motes_exp.match(line):
                motes_raw = list(map(str.strip, failed_motes_exp.match(line).groupdict().get("motes").split(",")))
                stats["failed_at_end"] = list(map(lambda x: mote_name_exp.search(x).groupdict().get("mote"), motes_raw))
            elif reporting_correct_exp.match(line):
                stats["reporting_ok"] = reporting_correct_exp.match(line).groupdict().get("correct")
            elif reporting_incorrectly_exp.match(line):
                motes_raw = reporting_incorrectly_exp.match(line).groupdict().get("incorrect")
                stats["reporting_bad"] = list(map(str.strip, motes_raw.split(",")))
            elif coverage_exp.match(line):
                stats["coverage"] = coverage_exp.match(line).groupdict().get("coverage")

        if not stats.get("reporting_bad"):
            stats["reporting_bad"] = []
        if not stats.get("failed_at_end"):
            stats["failed_at_end"] = []

        return stats


def render_js(params, stop_tick=0):
    motes, mode, k, imin, imax, recovery, run = params

    param_string = """var runNumber = {run};
var maxFailureCount = {motes};
var trickleIMin = {imin};
var trickleIMax = {imax};
var trickleRedundancyConst = {k};
var failureMode = "{mode}";
var moteRecoveryDelay = {recovery};
var moteFailureProbability = {mote_failure_probability};
var simulationStopTick = {stop_tick};""".format(run=run, motes=motes, recovery=recovery, 
                                                imin=imin, imax=imax, k=k, mode=mode,
                                                mote_failure_probability=mote_failure_probability, 
                                                stop_tick=stop_tick)
            
    return param_string
    
    
def render_sim(size, seed):
    mote_range = range(0, size**2)
    # soup = Soup(open(str(sim_template), 'r').read(), 'xml')
    template_str = open(str(sim_template), 'rb').read()
    root = etree.fromstring(template_str)
    sim = root.find('simulation')

    # Set the random seed of the simulation
    sim.find('randomseed').text = str(seed)

    # Add the simulation script in
    plugin = root.find('plugin').find('plugin_config')
    script_path = str(script_template)
    sim_script = ''.join(open(script_path, 'r').readlines())
    plugin.find('script').text = sim_script

    # Add the motes back to the sim
    for mote in mote_range:
        mote_x = 40.0 * (mote % size)
        mote_y = 40.0 * (mote // size)
        mote_z = 0.0
        
        mote_str = """<mote>
        <breakpoints />
        <interface_config>
            org.contikios.cooja.interfaces.Position
            <x>{x}</x>
            <y>{y}</y>
            <z>{z}</z>
        </interface_config>
        <interface_config>
            org.contikios.cooja.mspmote.interfaces.MspClock
            <deviation>1.0</deviation>
        </interface_config>
        <interface_config>
            org.contikios.cooja.mspmote.interfaces.MspMoteID
            <id>{id}</id>
        </interface_config>
        <motetype_identifier>sky1</motetype_identifier>
        </mote>""".format(x=mote_x, y=mote_y, z=mote_z, id=mote+1)
        sim.append(etree.fromstring(mote_str))

    return etree.tostring(root, pretty_print=True).decode('utf-8')


def run_control(experiment):
    motes, mode, k, imin, imax, recovery, run = experiment

    os.chdir(str(abs_dir))
    sim_seed = 12345678 + run
    param_dir = Path(experiment_dir, "control-{delay}-{k}-{imin}-{imax}-{max_fail}-{mode}-run{run}".format(delay=recovery, k=k, 
            imin=imin, imax=imax, max_fail=motes, mode=mode, run=run))
    sim_file = Path(param_dir, 'sim.csc')
    param_file = Path(param_dir, 'params.js')
    cooja_out = Path(param_dir, "experiment.log")

    if not param_dir.exists():
        param_dir.mkdir(parents=True)

    with open(str(sim_file), 'wt') as sim:
        sim.write(render_sim(experiment_size, sim_seed))
    
    with open(str(param_file), 'wt') as param:
        param.write(render_js(experiment))

    # Create a log file for this experiment
    logfile_handle = open(str(cooja_out), "w")

    os.chdir(str(param_dir))
    subprocess.call(["java", memory_size, "-jar", "../../../tools/cooja/dist/cooja.jar", 
                    "-nogui=sim.csc", "-contiki=../../.."], stdout=logfile_handle)

    # Create the key for the control data dict
    experiment_key = Experiment(d=recovery, k=k, imin=imin, imax=imax, n=motes, t=mode)
    tick_time = parse_control('COOJA.testlog')
    control_data = parse_experiment('COOJA.testlog')

    return experiment_key, tick_time, control_data


def parse_control(file_path):
    # Parse the COOJA.testlog file
    logfile = open(file_path, 'r').readlines()
    end_tick_re = re.compile(r'^(?P<tick>\d+)\ all\ motes\ converged,\ closing\ sim$')
    end_tick = None

    for line in logfile:
        match = end_tick_re.match(line)

        if match:
            end_tick = int(match.group('tick'))

    return end_tick


def filter_experiments(experiments, mask):
    # Mask is a namedtuple that has None for fields that are to be ignored
    mask_set = set(filter(lambda x: x[1], mask._asdict().items()))
    return list(filter(lambda x: mask_set <= set(x._asdict().items()), experiments))


def run_experiment(experiment, control_times):
    motes, mode, k, imin, imax, recovery, run = experiment

    os.chdir(str(abs_dir))
    sim_seed = 123456789 + run
    param_dir = Path(experiment_dir, "{delay}-{k}-{imin}-{imax}-{max_fail}-{mode}-run{run}".format(delay=recovery, k=k, 
            imin=imin, imax=imax, max_fail=motes, mode=mode, run=run))
    sim_file = Path(param_dir, 'sim.csc')
    param_file = Path(param_dir, 'params.js')
    cooja_out = Path(param_dir, "experiment.log")

    # Get the duration of the simulation
    exp_tuple = Experiment(d='', k=k, imin=imin, imax=imax, n='', t='')
    tick_key = filter_experiments(control_times.keys(), exp_tuple)

    if not tick_key:
        print("Cannot run experiment " + str(exp_tuple) + " because there is no control data")
        return
        
    tick = int(math.ceil(np.average(control_times[tick_key[0]])))

    if not param_dir.exists():
        param_dir.mkdir(parents=True)

    with open(str(sim_file), 'wt') as sim:
        sim.write(render_sim(experiment_size, sim_seed))
    
    with open(str(param_file), 'wt') as param:
        param.write(render_js(experiment, tick))

    # Create a log file for this experiment
    logfile_handle = open(str(cooja_out), "w")

    os.chdir(str(param_dir))
    subprocess.call(["java", memory_size, "-jar", "../../../tools/cooja/dist/cooja.jar", 
                    "-nogui=sim.csc", "-contiki=../../.."], stdout=logfile_handle)

    experiment_key = Experiment(d=recovery, k=k, imin=imin, imax=imax, n=motes, t=mode)
    results = parse_experiment("COOJA.testlog")

    return experiment_key, results


# Run the control experiments and then run the 
if __name__ == "__main__":
    # Make the experiment directory
    if not os.path.exists(str(experiment_dir)):
        os.makedirs(str(experiment_dir))

    print("Running {n} experiments (size is {m}x{m} grid) on {host}".format(n=len(experiment_space), 
                                                                            m=experiment_size, host=hostname))
    print("""Experiment parameters are:
        - Redundancy const. range (k): {range},
        - imin range: {imin},
        - imax range: {imax}\n\n""".format(range=list(redundancy_range), imin=list(imin_range), 
                                           imax=list(imax_range)))
    start_time = datetime.datetime.now()

    # Multiprocessing shared memory/locks etc
    pool_manager = Manager()
    control_times = pool_manager.dict()
    experiment_func = functools.partial(run_experiment, control_times=control_times)

    # Run the control experiments
    print("Running control experiment(s)")
    os.chdir(str(experiment_dir))
    with Pool(num_threads) as p:
        control_values = list(tqdm.tqdm(p.imap(run_control, control_space), total=len(control_space)))

    # Process the control experiments to get the run times
    print("Getting runtime(s) from the control experiments")
    os.chdir(str(abs_dir))

    # Collate the control results + execution times
    control_results = defaultdict(list)
    for key, tick, result in control_values:
        # If the experiment didn't terminate then discard
        if tick:
            control_times[key] = control_times.setdefault(key, []) + [tick]
            control_results[key].append(result)

    # Run the test sims
    print("Running experiments")
    os.chdir(str(experiment_dir))
    with Pool(num_threads) as p:
        results = list(tqdm.tqdm(p.imap(experiment_func, experiment_space), total=len(experiment_space)))

    # Collate the experiment results
    experimental_results = defaultdict(list)
    for key, result in results:
        experimental_results[key].append(result)

    # Save the data
    os.chdir(str(abs_dir))
        
    with open('experiment_data-{host}.pickle'.format(host=hostname), 'wb') as handle:
        pickle.dump(experimental_results, handle, protocol=pickle.HIGHEST_PROTOCOL)

    with open('control_data-{host}.pickle'.format(host=hostname), 'wb') as handle:
        pickle.dump(control_results, handle, protocol=pickle.HIGHEST_PROTOCOL)
        
    end_time = datetime.datetime.now()
    total_time = end_time - start_time

    print("Experiments ran for {time}".format(time=total_time))
