# A simple script to run the experiments on DCS machines
# Unset the python path on grace/etc
if [[ $(hostname) == *"grace"* ]]; then
    echo "Unsetting PYTHONPATH"
    unset PYTHONPATH
fi

nohup /usr/bin/python3.6 run_experiments.py