# Copyright Kishan Sambhi (2023)
# Released into public domain for all students to use
# Runs just the tests that we can run without priority donation or the BSD scheduler
TASKS="alarm-priority priority-change priority-condvar priority-fifo priority-preempt priority-sema priority-donate-one priority-donate-multiple priority-donate-multiple2 priority-donate-nest priority-donate-sema priority-preservation"

if [[ ! "$PWD" =~ src/threads$ ]]; then
    echo "Error: Directory does not end in src/threads"
    exit 1
fi

make

for task in $TASKS; do
    make build/tests/threads/$task.result
done