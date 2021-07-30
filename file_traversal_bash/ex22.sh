#!/bin/bash

# $1 = current folder
# $2 = file type (extension)
# $3 = word
# $4 = minimum line length
function recursive_search() {
	# run through the current directory's files
	filtered=$(
		# call ex21 script
		./ex21.sh "$1" "$2" "$3" | 
		# apply awk on its output to filter lines with wc < $4
		awk -v var="$4" 'NF>=var'
	)

	# don't echo empty strings
	[ ! -z "$filtered" ] && echo "$filtered"

	# run through the folder's content
	for curr in "$1"/*;
	do
		# if it's a directory
		if [ -d "$curr" ];
		then
			# keep traversing recursively over the subfolders
			recursive_search "$curr" "$2" "$3" "$4"
		fi
	done
}

# in the case of too many arguments
# focus on the first 4 args and ignore the rest
if [ $# -lt 4 ]
then
	echo 'Not enough parameters'
	exit 1
fi

# call our recursive function to traverse the folder and its subfolders
recursive_search "$1" "$2" "$3" "$4"