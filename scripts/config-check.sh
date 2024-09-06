#!/bin/bash

set -e

defconfig=${1} ; shift
fragments=("${@}")

echo -n "Checking that config fragment was honored... "

IFS=$'\n\r'
status=0
for config in $(grep -h "^\(# \)\{0,1\}CONFIG_" "${fragments[@]}" /dev/null)
do
    if ! grep -x -e "${config}" "${defconfig}" >/dev/null
    then
        if [ ${status} -eq 0 ]
        then
            status=1
            echo
        fi
        echo "Missing: ${config}"
    fi
done

if [ ${status} -eq 0 ]
then
    echo "Passed."
fi

exit ${status}
