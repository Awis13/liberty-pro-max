#!/bin/sh
# Function and jump-table export of GTAIV.exe from its auto-analysed Ghidra
# project (read only: the project is shared with the RE scripts in
# ~/labs/gtaiv-re), into $R/symbols/gtaiv/{functions,jumptables}.tsv.
export JAVA_HOME=/home/awis/labs/ghidra/jdk-21.0.12.1+1
export PATH=$JAVA_HOME/bin:$PATH
export GHIDRA_HEADLESS_MAXMEM=16G
H=/home/awis/labs/ghidra/ghidra_12.1.4_PUBLIC/support/analyzeHeadless
R=/home/awis/labs/gtaiv
S=$R/repo/vendor/x86recomp/tools/ghidra
mkdir -p $R/symbols/gtaiv $R/logs
echo "== export $(date +%T)"
$H /home/awis/labs/ghidra/projects gtaiv-1.0.8.0 -process GTAIV-1.0.8.0.exe \
  -noanalysis -readOnly -scriptPath $S \
  -postScript ExportFunctions.java $R/symbols/gtaiv/functions.tsv \
  -postScript ExportJumpTables.java $R/symbols/gtaiv/jumptables.tsv \
  -log $R/logs/ghidra-gtaiv.log > $R/logs/ghidra-gtaiv.out 2>&1
echo "rc=$? $(grep -h -E "ExportFunctions:|ExportJumpTables:" $R/logs/ghidra-gtaiv.out | tr "\n" " ")"
# Switch tables Ghidra left without targets, read from the image.
rm -f $R/symbols/gtaiv/jumptables.tsv.ghidra
python3 -I $R/repo/tools/complete_jumptables.py $R/game/GTAIV.exe $R/symbols/gtaiv/jumptables.tsv
