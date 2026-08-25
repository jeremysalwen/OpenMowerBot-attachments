#!/usr/bin/env python3
"""
Script to parse ROS launch file and generate a YAML file where the parameter name
defines the structure and the value is the name of the environment variable.

Example output:
ll:
  services:
    power:
      battery_empty_voltage: OM_BATTERY_EMPTY_VOLTAGE
"""

import re
import xml.etree.ElementTree as ET
from collections import defaultdict
import yaml


def nested_dict():
    """Create a nested defaultdict for building the parameter tree."""
    return defaultdict(nested_dict)


def set_nested_dict(d, keys, value):
    """Set a value in a nested dictionary using a list of keys."""
    for key in keys[:-1]:
        d = d[key]
    d[keys[-1]] = value


def convert_to_dict(d):
    """Convert nested defaultdict to regular dict for YAML serialization."""
    if isinstance(d, defaultdict):
        d = {k: convert_to_dict(v) for k, v in d.items()}
    return d


def extract_env_var(value_str):
    """Extract environment variable name from ROS param value string."""
    # Match $(env VARNAME) or $(optenv VARNAME default_value)
    env_match = re.search(r'\$\((env|optenv)\s+([A-Z_][A-Z0-9_]*)', value_str)
    if env_match:
        return env_match.group(2)
    return None


def parse_launch_file(launch_file_path):
    """Parse the ROS launch file and extract parameter to environment variable mappings."""
    tree = ET.parse(launch_file_path)
    root = tree.getroot()

    param_dict = nested_dict()

    # Find all <param> elements
    for param in root.findall('.//param'):
        name = param.get('name')
        value = param.get('value')

        if name and value:
            env_var = extract_env_var(value)
            if env_var:
                # Split the parameter name by '/' to create nested structure
                param_parts = name.split('/')
                set_nested_dict(param_dict, param_parts, env_var)

    return convert_to_dict(param_dict)


def generate_yaml(input_launch_file, output_yaml_file):
    """Generate YAML file from launch file parameters."""
    param_dict = parse_launch_file(input_launch_file)

    # Generate YAML string first
    yaml_str = yaml.dump(param_dict, default_flow_style=False, sort_keys=False)

    # Add blank lines when indentation decreases
    lines = yaml_str.split('\n')
    formatted_lines = []
    prev_indent = 0

    for line in lines:
        if not line.strip():  # Skip empty lines
            continue

        # Calculate current indentation level
        current_indent = len(line) - len(line.lstrip())

        # Add blank line if indentation decreased (moving to a higher level)
        if current_indent < prev_indent and formatted_lines:
            formatted_lines.append('')

        formatted_lines.append(line)
        prev_indent = current_indent

    with open(output_yaml_file, 'w') as f:
        f.write('\n'.join(formatted_lines) + '\n')

    print(f"Generated YAML file: {output_yaml_file}")
    print(f"Total parameters mapped: {count_params(param_dict)}")


def count_params(d):
    """Count the number of leaf parameters in nested dictionary."""
    count = 0
    for k, v in d.items():
        if isinstance(v, dict):
            count += count_params(v)
        else:
            count += 1
    return count


if __name__ == '__main__':
    import sys

    if len(sys.argv) < 2:
        # Default files
        input_file = '/opt/open_mower_ros/src/open_mower/launch/include/_params.launch'
        output_file = '/opt/open_mower_ros/env_params.yaml'
    elif len(sys.argv) == 2:
        input_file = sys.argv[1]
        output_file = 'env_params.yaml'
    else:
        input_file = sys.argv[1]
        output_file = sys.argv[2]

    try:
        generate_yaml(input_file, output_file)
        print(f"\nUsage: {sys.argv[0]} [input_launch_file] [output_yaml_file]")
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
