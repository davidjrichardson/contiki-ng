# A simple script to run the experiments on DCS machines
# Unset the python path on grace/etc
if [[ $(hostname) == *"grace"* ]]; then
    echo "Unsetting PYTHONPATH"
    unset PYTHONPATH
fi
export PYTHONHASHSEED=1234

if [[ $(hostname) == *"grace"* ]]; then
    /usr/bin/python3.6 run_experiments.py
else
    python3 run_experiments.py
fi