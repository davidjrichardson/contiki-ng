import numpy as np
import os
import itertools
import html
import subprocess
import re
import math
import socket

from collections import namedtuple
from lxml import etree
from multiprocessing import Pool
from pathlib import Path

contiki_dir = Path('..')
experiment_dir = Path(contiki_dir, 'tpwsn-trickle/experiments')
abs_dir = Path(os.path.dirname(os.path.abspath(__file__)))

script_template = Path(contiki_dir, 'tpwsn-trickle/sim-script.js')
sim_template = Path(contiki_dir, 'tpwsn-trickle/sim_template.csc')

Experiment = namedtuple('Experiment', ['d', 'k', 'imin', 'n', 't', 'imax'])

mote_failure_probability = 200

hostname = socket.gethostname()

if hostname is 'grace-01':
    num_threads = 24
else:
    num_threads = 12

memory_size = '-mx2048m'

# Experiment params
repeats = range(0, 20)

# Set the failure range per-compute node
if hostname is 'grace-01':
    redundancy_range = range(1,3)
    imin_range = [4, 8]
    imax_range = range(5, 11)
elif hostname is 'grace-02':
    redundancy_range = range(1,3)
    imin_range = [4, 8]
    imax_range = range(11, 17)
elif hostname is 'grace-03':
    redundancy_range = range(1,3)
    imin_range = [16, 32]
    imax_range = range(5, 11)
elif hostname is 'grace-04':
    redundancy_range = range(1,3)
    imin_range = [16, 32]
    imax_range = range(11, 17)
elif hostname is 'grace-05':
    redundancy_range = range(3,5)
    imin_range = [4, 8]
    imax_range = range(5, 11)
elif hostname is 'grace-06':
    redundancy_range = range(3,5)
    imin_range = [4, 8]
    imax_range = range(11, 17)
elif hostname is 'grace-07':
    redundancy_range = range(3,5)
    imin_range = [16, 32]
    imax_range = range(5, 11)
elif hostname is 'grace-08':
    redundancy_range = range(3,5)
    imin_range = [16, 32]
    imax_range = range(11, 17)
else:
    redundancy_range = range(1,5)
    imin_range = [4, 8, 16, 32]
    imax_range = range(5, 17)

# Constant parameter space between nodes
experiment_fail_modes = ["random", "location"]
experiment_fail_range = range(1, 17)
experiment_recovery_range = range(1, 16)

experiment_size = 15 # Number of motes along one axis (forms a square)
experiment_space = list(itertools.product(experiment_fail_range, experiment_fail_modes, redundancy_range, 
                                          imin_range, imax_range, experiment_recovery_range, repeats))

control_recovery_range = [0]
control_fail_range = [0]
control_fail_mode = ["none"]

control_space = list(itertools.product(control_fail_range, control_fail_mode, redundancy_range, imin_range, 
                                       imax_range, control_recovery_range, repeats))
control_times = {}


def render_js(params, stop_tick=0):
    motes, mode, k, imin, imax, recovery, run = params

    param_string = """var runNumber = {run};
    var maxFailureCount = {motes};
    var trickleIMin = {imin};
    var trickleIMax = {imax};
    var trickleRedundancyConst = {k};
    var failureMode = "{mode}";
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
    param_dir = Path(experiment_dir, "control-{0}-{1}-{2}-{3}-{4}-{6}-run{5}".format(motes, mode, k, imin, 
                      imax, run, recovery))
    sim_file = Path(param_dir, 'sim.csc')
    param_file = Path(param_dir, 'params.js')

    if not param_dir.exists():
        param_dir.mkdir(parents=True)

    with open(str(sim_file), 'wt') as sim:
        sim.write(render_sim(experiment_size, sim_seed))
    
    with open(str(param_file), 'wt') as param:
        param.write(render_js(experiment))

    os.chdir(str(param_dir))
    subprocess.call(["java", memory_size, "-jar", "../../../tools/cooja/dist/cooja.jar", 
                    "-nogui=sim.csc", "-contiki=../../.."])


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


def run_experiment(experiment):
    motes, mode, k, imin, imax, recovery, run = experiment

    os.chdir(str(abs_dir))
    sim_seed = 123456789 + run
    param_dir = Path(experiment_dir, "{0}-{1}-{2}-{3}-{4}-{6}-run{5}".format(motes, mode, k, imin, 
                      imax, run, recovery))
    sim_file = Path(param_dir, 'sim.csc')
    param_file = Path(param_dir, 'params.js')

    # Get the duration of the simulation
    exp_tuple = Experiment(d='', k=str(k), imin=str(imin), imax=str(imax), n='', t='')
    tick_key = filter_experiments(control_times.keys(), exp_tuple)

    if not tick_key:
        print("Cannot run experiment " + str(experiment) + " because there is no control data")
        return
        
    tick = int(math.ceil(np.average(control_times[tick_key[0]])))

    if not param_dir.exists():
        param_dir.mkdir(parents=True)

    with open(str(sim_file), 'wt') as sim:
        sim.write(render_sim(experiment_size, sim_seed))
    
    with open(str(param_file), 'wt') as param:
        param.write(render_js(experiment, tick))

    os.chdir(str(param_dir))
    subprocess.call(["java", memory_size, "-jar", "../../../tools/cooja/dist/cooja.jar", 
                    "-nogui=sim.csc", "-contiki=../../.."])


# Run the control experiments and then run the 
if __name__ == "__main__":
    # Make the experiment directory
    if not os.path.exists(str(experiment_dir)):
        os.makedirs(str(experiment_dir))

    print("Running {n} experiments on {host}".format(n=len(experiment_space), host=hostname))

    # Run the control experiments
    print("Running control experiment(s)")
    os.chdir(str(experiment_dir))
    with Pool(num_threads) as p:
        p.map(run_control, control_space)

    # Process the control experiments to get the run times
    print("Getting runtime(s) from the control experiments")
    os.chdir(str(abs_dir))
    
    exp_re = re.compile(r'(?P<n>\d+)-(?P<t>\w+)-(?P<k>\d+)-(?P<imin>\d+)-(?P<imax>\d+)-(?P<d>\d+)-run(?P<r>\d)')
    control_re = re.compile(r'control-(?P<n>\d+)-(?P<t>\w+)-(?P<k>\d+)-(?P<imin>\d+)-(?P<imax>\d+)-(?P<d>\d+)-run(?P<r>\d)')
    control_experiments = list(filter(lambda x: os.path.isdir(str(Path(experiment_dir, x))) and 'control' in x, 
                                      os.listdir(str(experiment_dir))))
  
    for control in control_experiments:
        control_dir = Path(experiment_dir, control)
        params = control_re.match(control).groupdict()
        params.pop('r', -1)
        
        experiment = Experiment(**params)
        tick_time = parse_control(str(Path(control_dir, 'COOJA.testlog')))

        if control_times.get(experiment):
            control_times[experiment].append(tick_time)
        else:
            control_times[experiment] = [tick_time]

    # Run the test sims
    print("Running experiments")
    os.chdir(str(experiment_dir))
    with Pool(num_threads) as p:
        p.map(run_experiment, experiment_space)
