import subprocess
import fileinput
import sys

EMBUILD_PATH = sys.argv[1]
CONFIG_FILE = sys.argv[2]
EXAMPLE_SELECTION_FILE = sys.argv[3]
LOGDIR = sys.argv[4]
TEST_LIST = []
test_pass = False

def run_embuild(testName):
    with open(EXAMPLE_SELECTION_FILE,'w') as file:
        file.write("#define "+testName+"\n")

    status = subprocess.run(EMBUILD_PATH +
                            " -rebuild -config \"Debug\" " + CONFIG_FILE,
                            shell=True, check=False,
                            universal_newlines=True)

    return (status.returncode == 0)

original_file_content = None

# Get a list of all the lines in the file that need to be edited.
with open(EXAMPLE_SELECTION_FILE) as file:
    original_file_content = file.read()
    for line in original_file_content.split('\n'):
        #if '//#define TEST_READING_DEV_ID' in line:
        if '//#define TEST_' in line:
            # Ignore the Frame Filtering tests
            if '//#define TEST_FRAME_FILTERING_' not in line:
                TEST_LIST.append(line)

for test in TEST_LIST:
    testName = test.split()[-1]
    print("- Building " + testName)
    # Build the example
    test_pass = run_embuild(testName)
    if test_pass is False:
        print("!! Build FAILED for " + testName)
        break
    print("--> Build OK")

with open(EXAMPLE_SELECTION_FILE, 'w') as file:
    file.write(original_file_content)

if test_pass is False:
    exit("At least one test failed! see logs for details... ")