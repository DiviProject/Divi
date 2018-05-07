#! /bin/bash
echo "--------------------------------------------------"
echo "Signing compiled tar.gz --------------------------"
echo "--------------------------------------------------"
if [ ! -f "$2" ]; then
	echo "Script uses 2 files: Full path and name for Signer's file, "
	echo "  and full path with file name of the tar.gz file to be signed."
	echo "  either no arguments were passed or they didn't exist."
	echo " EX: ./divisign.sh /shared/result/out/mwaser.sig /shared/result/out/divi-0.1.1-x86_64-linux-gnu.tar.gz"
	echo "---! Error !---"
	exit 1
else 
	SIGFILE=$1
	FILE2SIGN=$2
	if [[ ! $SIGFILE == *.sig ]]; then
		echo "First Argument is filename for Signature output file, [filename].sig"
		echo "---! Error !---"
		exit 1
	elif [[ ! $FILE2SIGN == *.tar.gz ]]; then
		echo "Second Argument must be path and name of file to be signed, [filename].tar.gz"
		echo "---! Error !---"
		exit 1
	else
		 gpg --output $SIGFILE --detach-sig $FILE2SIGN
		 gpg --verify $SIGFILE $FILE2SIGN
	fi	
fi

echo "Finished..."

