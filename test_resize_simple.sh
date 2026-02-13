#!/bin/bash
# Simple test to verify resize doesn't crash
echo "Testing basic resize functionality..."
cd /home/daniel/projects/plotix/build/examples

# Test that basic_line starts without crashing
echo "1. Testing basic_line starts..."
timeout 3s ./basic_line >/dev/null 2>&1
if [ $? -eq 124 ]; then
    echo "   ✓ basic_line runs successfully (timeout expected)"
else
    echo "   ✗ basic_line failed with exit code $?"
fi

# Test that window_resize_test starts without crashing
echo "2. Testing window_resize_test starts..."
timeout 3s ./window_resize_test >/dev/null 2>&1
if [ $? -eq 124 ]; then
    echo "   ✓ window_resize_test runs successfully (timeout expected)"
else
    echo "   ✗ window_resize_test failed with exit code $?"
fi

echo "Resize test completed."
