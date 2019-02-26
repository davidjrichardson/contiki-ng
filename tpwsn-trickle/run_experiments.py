#!/usr/local/bin/python3

import numpy as np
import os
import itertools
import html
import subprocess

from lxml import etree
from multiprocessing import Pool
from pathlib import Path

contiki_dir = Path('..')
experiment_dir = Path(contiki_dir, 'tpwsn-trickle/experiments')
abs_dir = Path("/Users/david/Projects/contiki-ng/tpwsn-trickle")

script_template = Path(contiki_dir, 'tpwsn-trickle/sim-script.template.js')
sim_template = Path(contiki_dir, 'tpwsn-trickle/7x7.csc')

os.chdir(str(experiment_dir))

# Experiment params
repeats = range(0, 10)
redundancy_range = [2] #range(2,4)
imin_range = [16] #[8, 16, 32]
imax_range = [10] #range(8, 11)

experiment_recovery_range = range(1, 16)
experiment_fail_modes = ["random", "location"]
experiment_recovery_range = range(1, 16)

experiment_size = 7 # Number of motes along one axis (forms a square)
experiment_space = list(itertools.product(experiment_recovery_range, experiment_fail_modes, redundancy_range, 
                                          imin_range, imax_range, experiment_recovery_range, repeats))

control_recovery_range = [0]
control_fail_range = [0]
control_fail_mode = ["random"]

control_space = list(itertools.product(control_fail_range, control_fail_mode, redundancy_range, imin_range, 
                                       imax_range, control_recovery_range, repeats))


def render_js(params, stop, script_file):
    motes, mode, k, imin, imax, recovery, run = params
    script = open(script_file, 'r').readlines()
    
    script_modified = map(lambda x: x.replace("%run%", str(run)), script)
    script_modified = map(lambda x: x.replace("%failures%", str(motes)), script_modified)
    script_modified = map(lambda x: x.replace("%delay%", str(recovery)), script_modified)
    script_modified = map(lambda x: x.replace("%tick%", str(stop)), script_modified)
    script_modified = map(lambda x: x.replace("%imin%", str(imin)), script_modified)
    script_modified = map(lambda x: x.replace("%imax%", str(imax)), script_modified)
    script_modified = map(lambda x: x.replace("%k%", str(k)), script_modified)
    script_modified = map(lambda x: x.replace("%mode%", str(mode)), script_modified)
    script_modified = map(lambda x: x.replace("%timeout%", str(stop + (5*1e6))), script_modified)
    
    return ''.join(script_modified)
    
    
def render_sim(params, size, seed, run):
  mote_range = range(0, size**2)
  # soup = Soup(open(str(sim_template), 'r').read(), 'xml')
  template_str = open(str(sim_template), 'rb').read()
  root = etree.fromstring(template_str)
  sim = root.find('simulation')
  plugin = root.find('plugin').find('plugin_config')

  # Set the random seed of the simulation
  sim.find('randomseed').text = str(seed)

  # Add the simulation script in
  plugin.find('script').text = render_js(params, 0, str(script_template))

  # Add the motes back to the sim
  for mote in mote_range:
      mote_x = 40.0 * (mote % 7)
      mote_y = 40.0 * (mote // 7)
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


def run_experiment(experiment):
    motes, mode, k, imin, imax, recovery, run = experiment

    os.chdir(str(abs_dir))
    sim_seed = 12345678 + run
    param_dir = Path(experiment_dir, "control-{0}-{1}-{2}-{3}-{4}-{6}-run{5}".format(motes, mode, k, imin, 
                      imax, run, recovery))
    sim_file = Path(param_dir, 'sim.csc')

    if not param_dir.exists():
        param_dir.mkdir(parents=True)

    with open(str(sim_file), 'wt') as sim:
        sim.write(render_sim(experiment, experiment_size, sim_seed, run))

    os.chdir(str(param_dir))
    subprocess.call(["java", "-mx512m", "-jar", "../../../tools/cooja/dist/cooja.jar", 
                    "-nogui=sim.csc", "-contiki=../../.."])


# Run the control experiments and then run the 
if __name__ == "__main__":
  print("Running control experiment(s)")
  with Pool(8) as p:
      p.map(run_experiment, control_space)
      # TODO: Parse the sim output for control experiments
      # TODO: Render sims for n fails (with new runtime)
      # TODO: Run the sims