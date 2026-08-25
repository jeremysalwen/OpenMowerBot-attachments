#!/usr/bin/env python3
"""
Script to generate a YAML configuration file from environment variables.

Reads valid (non-commented) environment variables from mower_config.txt,
then uses env_params.yaml as a template to create a new YAML file containing
only the keys that have corresponding environment variables defined, with
placeholder values replaced by actual values.
"""

import re
import yaml
from pathlib import Path
from typing import Dict, Any, Set


def parse_env_variables(config_file: str) -> Dict[str, str]:
    """
    Parse environment variables from mower_config.txt.

    Only extracts uncommented export statements.
    Returns a dictionary mapping variable names to their values.
    """
    env_vars = {}
    export_pattern = re.compile(r'^\s*export\s+([A-Z_][A-Z0-9_]*)=(.*)$')

    with open(config_file, 'r') as f:
        for line in f:
            # Skip comments and empty lines
            stripped = line.strip()
            if not stripped or stripped.startswith('#'):
                continue

            match = export_pattern.match(line)
            if match:
                var_name = match.group(1)
                var_value = match.group(2)

                # Remove quotes if present
                var_value = var_value.strip()
                if var_value.startswith('"') and var_value.endswith('"'):
                    var_value = var_value[1:-1]
                elif var_value.startswith("'") and var_value.endswith("'"):
                    var_value = var_value[1:-1]

                env_vars[var_name] = var_value

    return env_vars


def convert_value(value_str: str) -> Any:
    """
    Convert string value to appropriate Python type.
    """
    value_str = value_str.strip()

    # Boolean conversion
    if value_str.lower() == 'true':
        return True
    elif value_str.lower() == 'false':
        return False

    # Try to convert to number
    try:
        if '.' in value_str:
            return float(value_str)
        else:
            return int(value_str)
    except ValueError:
        pass

    # Return as string
    return value_str


def filter_yaml_structure(data: Any, env_vars: Set[str]) -> Any:
    """
    Recursively filter YAML structure to include only entries where
    the value (or nested values) reference environment variables that exist.

    Returns the filtered structure or None if nothing should be included.
    """
    if isinstance(data, dict):
        result = {}
        for key, value in data.items():
            if isinstance(value, str) and value in env_vars:
                # This is a leaf node with an env var reference
                result[key] = value
            elif isinstance(value, (dict, list)):
                # Recursively filter nested structures
                filtered = filter_yaml_structure(value, env_vars)
                if filtered:  # Only include if something was found
                    result[key] = filtered
        return result if result else None

    elif isinstance(data, list):
        result = []
        for item in data:
            if isinstance(item, str) and item in env_vars:
                result.append(item)
            elif isinstance(item, (dict, list)):
                filtered = filter_yaml_structure(item, env_vars)
                if filtered:
                    result.append(filtered)
        return result if result else None

    elif isinstance(data, str) and data in env_vars:
        return data

    return None


def replace_env_vars(data: Any, env_vars: Dict[str, str]) -> Any:
    """
    Recursively replace environment variable placeholders with actual values.
    """
    if isinstance(data, dict):
        result = {}
        for key, value in data.items():
            result[key] = replace_env_vars(value, env_vars)
        return result

    elif isinstance(data, list):
        return [replace_env_vars(item, env_vars) for item in data]

    elif isinstance(data, str):
        if data in env_vars:
            return convert_value(env_vars[data])
        return data

    return data


def main():
    """Main function to generate the configuration YAML file."""
    # File paths (relative to current working directory)
    config_file = Path('mower_config.txt')
    template_file = Path('env_params.yaml')
    output_file = Path('config.yaml')

    # Parse environment variables from mower_config.txt
    print(f"Reading environment variables from {config_file}...")
    env_vars = parse_env_variables(config_file)
    print(f"Found {len(env_vars)} environment variables")

    # Load the template YAML
    print(f"\nReading template from {template_file}...")
    with open(template_file, 'r') as f:
        template_data = yaml.safe_load(f)

    # Filter the template to only include keys with defined env vars
    print("Filtering template structure...")
    env_var_names = set(env_vars.keys())
    filtered_data = filter_yaml_structure(template_data, env_var_names)

    # Replace environment variable placeholders with actual values
    print("Replacing placeholders with actual values...")
    final_data = replace_env_vars(filtered_data, env_vars)

    # Write the output YAML file
    print(f"\nWriting output to {output_file}...")

    # Generate YAML string first
    yaml_str = yaml.dump(final_data, default_flow_style=False, sort_keys=False)

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

    with open(output_file, 'w') as f:
        f.write('\n'.join(formatted_lines) + '\n')

    print(f"✓ Successfully generated {output_file}")

    # Print summary
    print("\n" + "="*60)
    print("Summary:")
    print("="*60)

    def count_params(data, prefix=""):
        """Count and display parameters."""
        count = 0
        if isinstance(data, dict):
            for key, value in data.items():
                current = f"{prefix}.{key}" if prefix else key
                if isinstance(value, dict):
                    count += count_params(value, current)
                else:
                    count += 1
                    if not isinstance(value, dict):
                        print(f"  {current}: {value}")
        return count

    total = count_params(final_data)
    print(f"\nTotal parameters: {total}")


if __name__ == '__main__':
    main()
