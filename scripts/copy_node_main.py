#!/usr/bin/env python3

Import("env")
import shutil
import os

def copy_node_main(source, target, env):
    """Copy node main file to src/main.cpp for build"""
    src_path = "src/application/app_node/main_node.cpp"
    dst_path = "src/main.cpp"
    
    if os.path.exists(src_path):
        print(f"Copying {src_path} -> {dst_path}")
        shutil.copy2(src_path, dst_path)
    else:
        print(f"Warning: {src_path} not found!")

# Register the callback
env.AddPreAction("buildprog", copy_node_main)