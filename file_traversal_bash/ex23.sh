#!/bin/bash

# set flags with default values
HOST=false
SYSTEM=false

PRINT_ALL=true

NAME=false
VERSION=false
PRETTY_NAME=false
HOME_URL=false
SUPPORT_URL=false

STATIC_HOSTNAME=false
ICON_NAME=false
MACHINE_ID=false
BOOT_ID=false
VIRTUALIZATION=false
KERNEL=false
ARCHITECTURE=false

for arg in "$@";
do
	# if not a flag
	if ! [[ $arg =~ "--".* ]];
	then
		# set the proper flags is we get 'system' or 'host' arguments
		if [ $arg == "system" ];
		then
			SYSTEM=true
		elif [ $arg == "host" ];
		then 
			HOST=true
		else
			# if we get a non-flag argument that is not 'host' or 'system'
			# it must be an invalid input
			echo "Invalid input"
			exit 1
		fi
	fi
done

if [ $HOST == true ] && [ $SYSTEM == true ];
then
	# user typed both 'host' and 'system'
	echo 'Invalid input'
	exit 1
elif [ $HOST == false ] && [ $SYSTEM == false ];
then
	# user didn't type 'host' or 'system'
	echo 'Invalid input'
	exit 1
fi

# Loop through arguments and process them
for arg in "$@";
do
	# for each flag
	# 	make sure the matching arg (system/host) was requested
	#   make sure the line wasn't printed before (flag is false)
	#   fetch (and trim) the relevant line from the file
	#   set the arg's flag to true (so we won't print it again)
	#   set PRINT_ALL to false (i.e. don't print whole files)
	case $arg in
		--static_hostname)
		[ $HOST == true ] && 
		[ $STATIC_HOSTNAME == false ] && 
		awk -F": " 'toupper($0)~/^\s+STATIC HOSTNAME.*/ {print $2}' ./hostnamectl &&
		STATIC_HOSTNAME=true &&
		PRINT_ALL=false
		shift
		;;
		--icon_name)
		[ $HOST == true ] && 
		[ $ICON_NAME == false ] && 
		awk -F": " 'toupper($0)~/^\s+ICON NAME.*/ {print $2}' ./hostnamectl &&
		ICON_NAME=true && 
		PRINT_ALL=false
		shift
		;;
		--machine_id)
		[ $HOST == true ] && 
		[ $MACHINE_ID == false ] && 
		awk -F": " 'toupper($0)~/^\s+MACHINE ID.*/ {print $2}' ./hostnamectl &&
		MACHINE_ID=true &&
		PRINT_ALL=false
		shift
		;;
		--boot_id)
		[ $HOST == true ] && 
		[ $BOOT_ID == false ] && 
		awk -F": " 'toupper($0)~/^\s+BOOT ID.*/ {print $2}' ./hostnamectl &&
		BOOT_ID=true &&
		PRINT_ALL=false
		shift
		;;
		--virtualization)
		[ $HOST == true ] && 
		[ $VIRTUALIZATION == false ] && 
		awk -F": " 'toupper($0)~/^\s+VIRTUALIZATION.*/ {print $2}' ./hostnamectl &&
		VIRTUALIZATION=true &&
		PRINT_ALL=false
		shift
		;;
		--kernel)
		[ $HOST == true ] && 
		[ $KERNEL == false ] && 
		awk -F": " 'toupper($0)~/^\s+KERNEL.*/ {print $2}' ./hostnamectl &&
		KERNEL=true && 
		PRINT_ALL=false
		shift
		;;
		--architecture)
		[ $HOST == true ] && 
		[ $ARCHITECTURE == false ] && 
		awk -F": " 'toupper($0)~/^\s+ARCHITECTURE.*/ {print $2}' ./hostnamectl &&
		ARCHITECTURE=true &&
		PRINT_ALL=false
		shift
		;;
		--name)
		[ $SYSTEM == true ] && 
		[ $NAME == false ] && 
		awk -F"=" '$1~/^NAME$/ {print $2}' ./os-release | sed s/\"//g &&
		NAME=true &&
		PRINT_ALL=false
		shift
		;;
		--version)
		[ $SYSTEM == true ] && 
		[ $VERSION == false ] && 
		awk -F"=" '$1~/^VERSION$/ {print $2}' ./os-release | sed s/\"//g &&
		VERSION=true &&
		PRINT_ALL=false
		shift
		;;
		--pretty_name)
		[ $SYSTEM == true ] && 
		[ $PRETTY_NAME == false ] && 
		awk -F"=" '$1~/^PRETTY_NAME$/ {print $2}' ./os-release | sed s/\"//g &&
		PRETTY_NAME=true &&
		PRINT_ALL=false
		shift
		;;
		--home_url)
		[ $SYSTEM == true ] && 
		[ $HOME_URL == false ] && 
		awk -F"=" '$1~/^HOME_URL$/ {print $2}' ./os-release | sed s/\"//g &&
		HOME_URL=true &&
		PRINT_ALL=false
		shift
		;;
		--support_url)
		[ $SYSTEM == true ] && 
		[ $SUPPORT_URL == false ] && 
		awk -F"=" '$1~/^SUPPORT_URL$/ {print $2}' ./os-release | sed s/\"//g &&
		SUPPORT_URL=true &&
		PRINT_ALL=false
		shift
		;;
		*)
			shift
		;;
	esac
done

# print the whole files if no proper flags were requested by the user
if [ $PRINT_ALL == true ];
then
	[ $SYSTEM == true ] && cat ./os-release
	[ $HOST == true ] && cat ./hostnamectl
fi
