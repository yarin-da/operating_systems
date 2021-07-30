#!/bin/bash

# if too many args - ignore every arg after the first 3
if [ $# -lt 3 ];
then
	echo 'Not enough parameters'
	exit 1
fi

# set args into readable variables
folder=$1
filetype=$2
word=$3

# list all files in folder
folder_files=$(ls "$folder")
# filter to the type the user requested
filtered_files=$(echo "$folder_files" | grep ".${filetype}$")
# strip file extensions and trailing slashes
stripped_files=$(echo "$filtered_files" | sed s/\.[^.]*$// | sed 's:/*$::')
# sort the files (with versioning)
sorted_files=$(echo "$stripped_files" | sort -V)

# precede each file with the folder path and add back the extension
fullname_files=$(
	echo "$sorted_files" |
	# filter any empty lines (NF condition)
	awk -v dir="$folder" -v ext="$filetype" 'NF {print dir"/"$0"."ext}'
)

for file in $fullname_files;
do
	cat $file | grep -iw "$word"
done
