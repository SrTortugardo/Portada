#!/bin/bash

while true; do
    info=$(cmus-remote -Q)

    file=$(echo "$info" | grep '^file ' | cut -d' ' -f2-)
    pos=$(echo "$info" | grep '^position ' | cut -d' ' -f2)

    lrc="${file%.*}.lrc"

    clear

    if [[ -f "$lrc" ]]; then
        awk -v pos="$pos" '
        function t(s){
            split(s,a,":")
            return a[1]*60 + a[2]
        }

        match($0, /\[([0-9]+:[0-9.]+)\]/, m){
            time=t(m[1])
            text=substr($0, RSTART+RLENGTH)
            if(time<=pos){
                last=text
            }
        }

        END{
            print last
        }' "$lrc"

    else
        echo "no hay .lrc"
    fi

    sleep 0.3
done
