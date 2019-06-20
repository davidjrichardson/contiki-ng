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
import tarfile
import shutil

from collections import namedtuple, defaultdict
from lxml import etree
from multiprocessing import Pool, Manager
from pathlib import Path

hostname = socket.gethostname()

contiki_dir = Path('..')
experiment_dir = Path(contiki_dir, 'tpwsn-rmh/experiments')
experiment_done_dir = Path(experiment_dir, '.done')
abs_dir = Path(os.path.dirname(os.path.abspath(__file__)))

script_template = Path(contiki_dir, 'tpwsn-rmh/sim-script.js')
sim_template = Path(contiki_dir, 'tpwsn-rmh/sim_template.csc')

RmhExperiment = namedtuple('RmhExperiment', ['d', 'n', 't'])

mote_failure_probability = 200

if hostname == 'grace-01':
    num_threads = 24
else:
    num_threads = 12

memory_size = '-mx2048m'

# Experiment params
repeats = range(0, 20)

# Set the failure range per-compute node

if hostname == 'grace-04':
    experiment_max_fail_range = range(1, 7, 2)
elif hostname == 'grace-05':
    experiment_max_fail_range = range(8, 13, 2)
elif hostname == 'grace-06':
    experiment_max_fail_range = range(14, 18, 2)
else:
    experiment_max_fail_range = range(1, 19, 2)
# elif hostname == 'grace-04':
#     experiment_delay_range = range(16, 21, 1)
#     experiment_max_fail_range = range(1, 9, 1)
# elif hostname == 'grace-05':
#     experiment_delay_range = range(1, 6, 1)
#     experiment_max_fail_range = range(9, 17, 1)
# elif hostname == 'grace-06':
#     experiment_delay_range = range(6, 11, 1)
#     experiment_max_fail_range = range(9, 17, 1)
# elif hostname == 'grace-07':
#     experiment_delay_range = range(11, 16, 1)
#     experiment_max_fail_range = range(9, 17, 1)
# elif hostname == 'grace-08':
#     experiment_delay_range = range(16, 21, 1)
#     experiment_max_fail_range = range(9, 17, 1)
# else:
#     experiment_max_fail_range = range(1, 2, 1)

experiment_delay_range = range(1, 17, 2)
# Constant parameter space between nodes
experiment_fail_modes = ["random", "location"]

experiment_size = 21 # Number of motes along one axis (forms a square)
experiment_space = list(itertools.product(experiment_delay_range, experiment_fail_modes,
                                          experiment_max_fail_range, repeats))

control_recovery_range = [0]
control_fail_range = [0]
control_fail_mode = ["none"]

control_space = list(itertools.product(control_fail_range, control_fail_mode,
                                       control_recovery_range, repeats))
    

def parse_experiment(filename):
    with open(filename, "rt") as log_file:
        data_raw = map(str.strip, log_file.readlines())
        stats = {}

        msgs_sent_exp = re.compile(r'Messages sent: (?P<count>\d+)')
        announces_sent_exp = re.compile(r'Announcements sent: (?P<count>\d+)')
        total_crashes_exp = re.compile(r'Total crashes: (?P<crashes>\d+)')
        failed_motes_exp = re.compile(r'Motes currently failed \(\d+\): {(?P<motes>.+)}')
        reporting_correct_exp = re.compile(r'Motes reporting correctly: (?P<correct>\d+)')
        reporting_incorrectly_exp = re.compile(r'Motes reporting incorrectly: \[(?P<incorrect>.+)\]')
        coverage_exp = re.compile(r'Coverage: (?P<coverage>\d+)')
        mote_name_exp = re.compile(r'(?P<mote>Sky \d+)')

        for line in data_raw:
            if msgs_sent_exp.match(line):
                stats["messages"] = msgs_sent_exp.match(line).groupdict().get("count")
            elif announces_sent_exp.match(line):
                stats["announcements"] = announces_sent_exp.match(line).groupdict().get("count")
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
    motes, mode, recovery, run = params

    param_string = """var runNumber = {run};
var maxFailureCount = {motes};
var failureMode = "{mode}";
var moteRecoveryDelay = {recovery};
var moteFailureProbability = {mote_failure_probability};
var simulationStopTick = {stop_tick};""".format(run=run, motes=motes, recovery=recovery, 
                                                mode=mode, stop_tick=stop_tick,
                                                mote_failure_probability=mote_failure_probability)
            
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
    motes, mode, recovery, run = experiment

    os.chdir(str(abs_dir))
    sim_seed = 12345678 + run
    param_dirname = "control-{delay}-{max_fail}-{mode}-run{run}".format(delay=recovery,
            max_fail=motes, mode=mode, run=run)
    param_dir = Path(experiment_dir, param_dirname)
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
    experiment_key = RmhExperiment(d=recovery, n=motes, t=mode)
    tick_time = parse_control('COOJA.testlog')
    control_data = parse_experiment('COOJA.testlog')
    control_data['end_tick'] = tick_time

    # Change out to the parent dir, tar the experiment, and remove the folder
    os.chdir('..')
    experiment_tarball = param_dirname + '.tar.gz'
    with tarfile.open(experiment_tarball, 'w:gz') as tar:
        tar.add(param_dirname, arcname=os.path.basename(param_dir))
    shutil.rmtree(param_dirname, ignore_errors=True)

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
    motes, mode, recovery, run = experiment

    experiment_key = RmhExperiment(d=recovery, n=motes, t=mode)
    param_dirname = "{delay}-{max_fail}-{mode}-run{run}".format(delay=recovery, 
            max_fail=motes, mode=mode, run=run)

    done_file = Path(experiment_done_dir, '{hash}.done'.format(hash=hash(param_dirname.encode())))
    param_dir = Path(experiment_dir, param_dirname)
    experiment_tarball = Path(experiment_dir, param_dirname + '.tar.gz')
    sim_file = Path(param_dir, 'sim.csc')
    param_file = Path(param_dir, 'params.js')
    cooja_out = Path(param_dir, "experiment.log")

    os.chdir(str(abs_dir))
    if done_file.exists():
        # Extract the tarball
        with tarfile.open(str(experiment_tarball), 'r:gz') as tar:
            os.chdir(str(experiment_dir))
            tar.extractall()
            os.chdir(str(abs_dir))

        # Get the data out of the results files
        os.chdir(str(param_dir))
        results = parse_experiment("COOJA.testlog")

        # Remove the directory (again)
        os.chdir(str(abs_dir))
        shutil.rmtree(str(param_dir), ignore_errors=True)
    else:
        # Actually run the experiment
        sim_seed = 123456789 + run

        # Get the duration of the simulation
        exp_tuple = RmhExperiment(d='', n='', t='')
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

        results = parse_experiment("COOJA.testlog")

        # Change out to the parent dir, tar the experiment, and remove the folder
        os.chdir(str(abs_dir))
        with tarfile.open(str(experiment_tarball), 'w:gz') as tar:
            tar.add(param_dir, arcname=os.path.basename(param_dir))
        shutil.rmtree(str(param_dir), ignore_errors=True)

        # Mark the experiment as done
        done_file.touch()

    return experiment_key, results


# Run the control experiments and then run the 
if __name__ == "__main__":
    # Make the experiment directory
    if not os.path.exists(str(experiment_dir)):
        os.makedirs(str(experiment_dir))

    # Make the experiment 'done' directory
    if not os.path.exists(str(experiment_done_dir)):
        os.makedirs(str(experiment_done_dir))

    print("Running {n} experiments (size is {m}x{m} grid) on {host}".format(n=len(experiment_space), 
                                                                            m=experiment_size, host=hostname))
    print("""Experiment parameters are:
        - Maximum failures: {range},
        - Mote recovery delay: {recovery}\n\n""".format(range=list(experiment_max_fail_range), 
                                                        recovery=list(experiment_delay_range)))
    start_time = datetime.datetime.now()

    # Multiprocessing shared memory/locks etc
    pool_manager = Manager()
    control_times = pool_manager.dict()
    experiment_func = functools.partial(run_experiment, control_times=control_times)

    control_file = 'control_data.pickle'

    # Check if the control file exists - if it does then we can skip re-running the experiments
    os.chdir(str(abs_dir))

    if os.path.exists(control_file):
        print("Loading control experiment data from pickle")
        with open(control_file, 'rb') as handle:
            control_results = pickle.load(handle)

            # Control_results is key -> list
            # list contains dicts with end_tick
            # Want to map key -> list(dict[end_tick]) into key -> list(end_tick)
            for key, values in control_results.items():
                filtered = filter(lambda x: x['end_tick'], values)
                control_times[key] = list(map(lambda x: x['end_tick'], filtered))
    else:
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

        with open(control_file, 'wb') as handle:
            pickle.dump(control_results, handle, protocol=pickle.HIGHEST_PROTOCOL)

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
        
    with open('results_data-{host}.pickle'.format(host=hostname), 'wb') as handle:
        pickle.dump(experimental_results, handle, protocol=pickle.HIGHEST_PROTOCOL)

    end_time = datetime.datetime.now()
    total_time = end_time - start_time

    print("Experiments ran for {time}".format(time=total_time))
