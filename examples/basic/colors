#!/bin/sh
# 8 basic colors
for i in {30..37}; do
	printf "\033[${i}mColor ${i}\033[0m\r\n"
done

# 8 bright colors
for i in {90..97}; do
	printf "\033[${i}mBright color ${i}\033[0m\r\n"
done

# 256-color palette
printf "\r\n256-color palette:\r\n"
for i in {0..255}; do
	printf "\033[48;5;${i}m%3d\033[0m " $i
	if [ $((($i + 1) % 16)) -eq 0 ]; then
		printf "\r\n"
	fi
done

# Truecolor (24-bit RGB)
printf "\r\nTruecolor test:\r\n"
for r in {0..255..51}; do
	for g in {0..255..51}; do
		for b in {0..255..51}; do
			printf "\033[48;2;${r};${g};${b}m \033[0m"
		done
	done
	printf "\r\n"
done
