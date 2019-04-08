# A simple script to run the experiments on DCS machines
# Unset the python path on grace/etc
if [[ $(hostname) == *"grace"* ]]; then
    unset PYTHON_PATH
fi

python3 run_experiments.py