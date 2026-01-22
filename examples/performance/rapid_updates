#!/bin/sh
# Rapid updates test
echo -e "\033[2J" # Clear screen
echo "Rapid updates test (100 updates):"
for i in {1..100}; do
	echo -e "\033[2;1HUpdate $i/100"
	echo -e "\033[3;1HProgress: [$(printf '%*s' $((i / 2)) | tr ' ' '=')>$(printf '%*s' $((50 - i / 2)) | tr ' ' ' ')]"
	echo -e "\033[4;1HTime: $(date +%H:%M:%S.%N)"
	# Small delay
	sleep 0.01
done
echo -e "\033[6;1HTest complete"
