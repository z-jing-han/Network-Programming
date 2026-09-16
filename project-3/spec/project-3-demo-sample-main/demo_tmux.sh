#!/bin/bash

PROJECT_PATH=$1
PORT1=$2

if [ -z "$PROJECT_PATH" ] || [ -z "$PORT1" ] ; then
  echo "Usage: $0 <project_path> <port1>"
  exit 1
fi

tmux set remain-on-exit on

make -C $PROJECT_PATH -j1
sleep 2.0

tmux split-window -v -l 80%

tmux select-pane -t 1
./demo_task.sh $PROJECT_PATH/np_multi_proc $PORT1 1 2

tmux kill-pane -t 0