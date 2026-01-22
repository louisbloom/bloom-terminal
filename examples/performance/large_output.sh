#!/bin/sh
# Large output test
echo -e "\033[2J" # Clear screen
echo "Generating large output (1000 lines)..."
for i in {1..1000}; do
	if [ $((i % 2)) -eq 0 ]; then
		echo -e "\033[32mLine $i: Even line with green text\033[0m"
	else
		echo -e "\033[34mLine $i: Odd line with blue text\033[0m"
	fi
done
echo "Large output test complete"
