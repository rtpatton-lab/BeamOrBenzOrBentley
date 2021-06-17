import os 
import sys
import glob
import timeit

# runs tests, stores output w/ commit hash in results.csv
def run_test(filename):
    os.system("./solution " + filename + " | python3 evaluate.py " + filename)

def run_all_tests(file_=None):
    if not file_:
        filenames = sorted(glob.glob("./test_cases/*.txt"))
    else: 
        filenames = [file_]

    for filename in filenames: 
        print("testing " + str(filename))
        elapsed = timeit.timeit('run_test(\"' + filename + '\")', 'from __main__ import run_test', number=1)
        print(" Time :" + str(elapsed))

if __name__ == "__main__":
    run_all_tests(sys.argv[1] if len(sys.argv) == 2 else None)

