#!/bin/sh
# Cursor visibility and shape test
echo -e "Cursor visible"
sleep 1
echo -e "\033[?25l" # Hide cursor
echo -e "Cursor hidden"
sleep 1
echo -e "\033[?25h" # Show cursor
echo -e "Cursor visible again"
sleep 1

# Cursor shape (not all terminals support this)
echo -e "\033[0 q" # Default cursor
echo -e "Default cursor shape"
sleep 1
echo -e "\033[1 q" # Blinking block
echo -e "Blinking block cursor"
sleep 1
echo -e "\033[3 q" # Blinking underline
echo -e "Blinking underline cursor"
sleep 1
echo -e "\033[5 q" # Blinking bar
echo -e "Blinking bar cursor"
sleep 1
echo -e "\033[0 q" # Back to default"
