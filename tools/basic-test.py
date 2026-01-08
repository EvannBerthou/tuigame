from pathlib import Path
import os, sys
import subprocess

failed_only = False
stop_first_fail = False
specific_test_case = None

args = list(reversed(sys.argv))
while args:
    arg = args.pop()
    if arg == '--failed-only':
        failed_only = True
    elif arg == '--stop-first-fail':
        stop_first_fail = True
    elif arg == '--test':
        specific_test_case = args.pop()


root = 'tests/basic/'

success = 0
failed = 0

tests = sorted(os.listdir(root))
if specific_test_case :
    if specific_test_case in tests:
        tests = [specific_test_case]
    else:
        print(f"Unknown test {specific_test_case}")
        print("Available tests:")
        for test in tests:
            print(f"- {test}")
        exit(1)

for test in tests:
    if test[0] == '#':
        continue
    path = os.path.join(root, test)
    content = Path(path).read_text()

    expected_ouput = []
    program = []
    lines = content.split('\n')
    out = expected_ouput
    for line in lines:
        if line == '---':
            out = program
            continue
        out.append(line)

    full_program = '\n'.join(program)
    try:
        p = subprocess.run(['./build/basic', '-'], input=full_program, capture_output=True, text=True, timeout=1)
        test_result = p.stdout.strip()
    except:
        test_result = "Test case Timeout"

    expected = '\n'.join(expected_ouput)
    if test_result != expected:
        failed += 1
        print(f"\033[91mTest case {test} : Test failed\033[0m")
        print("Expected:")
        print(expected)
        print("But recieved:")
        print(test_result)
        if stop_first_fail:
            print("Program:")
            print(full_program)
            break
    elif not failed_only or specific_test_case:
        print(f"\033[94mTest case {test} : Test success\033[0m")
        success += 1
print(f"\033[94mSuccess={success}\033[0m")
print(f"\033[91mFailed={failed}\033[0m")
