#!/usr/local/bin/python3

import os
import itertools
import shutil
import subprocess

from multiprocessing import Pool

from pathlib import Path

contiki_dir = Path("../..")
experiment_dir = Path(contiki_dir, "tpwsn-trickle/experiments")
sim_template = Path(experiment_dir, "7x7.csc")

abs_dir = Path("/Users/david/Projects/contiki-ng/tpwsn-trickle/experiments")

os.chdir(str(experiment_dir))

fail_range = range(1,11)
fail_mode = ["location"]

redundancy_range = [2] #range(2,4)
imin_range = [16] #[8, 16, 32]
imax_range = [10] #range(8, 11)
recovery_range = [5000, 10000, 15000, 20000]

repeats = 10

experiment_params = itertools.product(fail_range, fail_mode, redundancy_range, imin_range, imax_range, recovery_range)

def run_sim(experiment):
    motes, mode, k, imin, imax, recovery = experiment
        
    for run in range(0, repeats):
        os.chdir(str(abs_dir)) 

        param_dir = Path(experiment_dir, "{0}-{1}-{2}-{3}-{4}-{6}-run{5}".format(motes, mode, k, imin, imax, run, recovery))
        sim_file = Path(param_dir, "7x7.csc")

        if not param_dir.exists():
            param_dir.mkdir(parents=True)

        with open(str(sim_template), "rt") as template:
            with open(str(sim_file), "wt") as sim:
                for line in template:
                    l = line.replace("%run%", str(run))
                    l = l.replace("%fails%", str(motes))
                    l = l.replace("%mode%", str(mode))
                    l = l.replace("%imin%", str(imin))
                    l = l.replace("%imax%", str(imax))
                    l = l.replace("%k%", str(k))
                    l = l.replace("%r%", str(recovery))

                    sim.write(l)

        os.chdir(str(param_dir))
        subprocess.call(["java", "-mx512m", "-jar", "../../../tools/cooja/dist/cooja.jar", "-nogui=7x7.csc", "-contiki=../../.."])


if __name__ == "__main__":
    with Pool(4) as p:
        p.map(run_sim, list(experiment_params))
