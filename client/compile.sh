#!/bin/bash

# Compile client.c in the Client directory
gcc -o client client.c 
echo "Compiled client.c to client"

# Navigate to the Server directory
cd ../server || exit

# Compile smain.c
gcc -o smain smain.c
echo "Compiled smain.c to smain"

# Compile spdf.c
gcc -o spdf spdf.c
echo "Compiled spdf.c to spdf"

# Compile stext.c
gcc -o stext stext.c
echo "Compiled stext.c to stext"

# Return to the Client directory
cd ../client
