#!/bin/bash

# This machine has no system pip/tornado; client.py needs tornado to run.
# Prepend the project-local venv (built for exactly this) so it's picked up
# by every pane tmux spawns below -- harmless no-op if the venv isn't there.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_BIN="$SCRIPT_DIR/../../../code-review/.venv/bin"
[ -d "$VENV_BIN" ] && export PATH="$(cd "$VENV_BIN" && pwd):$PATH"

PROJECT_PATH=$1
PORT1=$2

if [ -z "$PROJECT_PATH" ] || [ -z "$PORT1" ] ; then
  printf "Usage: $0 <project_path> <port1>\n       $0 <server_path> <port>\n"
  exit 1
fi

if [ -z "$TMUX" ]; then
  if tmux ls | grep -q np_demo; then
    tmux kill-session -t np_demo
  fi
  if [ -d "$PROJECT_PATH" ]; then
    tmux new-session -s np_demo -n np_demo_sample "cd $PWD; ./demo_tmux.sh $PROJECT_PATH $PORT1"
  else
    tmux new-session -s np_demo -n np_demo_sample \
    "tmux split-window -v -l 95%; tmux split-window -v -l 55%; cd $PWD; ./demo_task.sh $PROJECT_PATH $PORT1 2 3"
  fi
else
  tmux new-window -n np_demo_sample
  if [ -d "$PROJECT_PATH" ]; then
    ./demo_tmux.sh $PROJECT_PATH $PORT1
  else
    ./demo_task.sh $PROJECT_PATH $PORT1
  fi
fi
