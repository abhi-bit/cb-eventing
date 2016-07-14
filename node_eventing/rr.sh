#!/bin/bash
file=$1
while IFS= read -r line
do
  echo $line | nc localhost 3001
done <"$file"

