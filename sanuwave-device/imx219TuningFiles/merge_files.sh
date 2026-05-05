python3 << 'EOF'
import json

base_dir = '/home/rickfrank/Downloads/FromArducam'

with open(f'{base_dir}/imx219_noir.json') as f:
    noir = json.load(f)

with open(f'{base_dir}/imx219_af.json') as f:
    af = json.load(f)

# Extract rpi.af block
af_block = next(a for a in af['algorithms'] if 'rpi.af' in a)

# Append to noir algorithms
noir['algorithms'].append(af_block)

output = f'{base_dir}/imx219_noir_af.json'
with open(output, 'w') as f:
    json.dump(noir, f, indent=2)

print(f"Done - written to {output}")

# Confirm it's there
merged = next((a for a in noir['algorithms'] if 'rpi.af' in a), None)
print(f"rpi.af block present in output: {merged is not None}")
EOF
