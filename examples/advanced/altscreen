#!/bin/sh
# Alternate screen buffer test
printf "Normal screen - line 1\r\n"
printf "Normal screen - line 2\r\n"
printf "\033[?1049h" # Enable alternate screen
printf "\r\n"
printf "Alternate screen - line 1\r\n"
printf "Alternate screen - line 2\r\n"
printf "Press Enter to return to normal screen...\r\n"
# In automated testing, we don't wait for user input
# printf "\033[?1049l"  # Disable alternate screen
# printf "\r\n"
# printf "Back to normal screen\r\n"
