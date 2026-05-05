#!/bin/bash

# Script to convert COLMAP reconstruction to PLY point cloud
# Usage: ./convert_to_ply.sh [input_path] [output_file]

# Default paths
INPUT_PATH="${1:-$HOME/sanuwave/colmap.build/sparse/0}"
OUTPUT_FILE="${2:-$HOME/sanuwave/colmap.build/model.ply}"

# Check if input path exists
if [ ! -d "$INPUT_PATH" ]; then
    echo "Error: Input path does not exist: $INPUT_PATH"
    exit 1
fi

# Check if required files exist
if [ ! -f "$INPUT_PATH/cameras.bin" ] || [ ! -f "$INPUT_PATH/images.bin" ] || [ ! -f "$INPUT_PATH/points3D.bin" ]; then
    echo "Error: Missing required COLMAP files in $INPUT_PATH"
    echo "Required files: cameras.bin, images.bin, points3D.bin"
    exit 1
fi

echo "Converting COLMAP reconstruction to PLY..."
echo "Input path: $INPUT_PATH"
echo "Output file: $OUTPUT_FILE"

# Run COLMAP model converter
colmap model_converter \
    --input_path "$INPUT_PATH" \
    --output_path "$OUTPUT_FILE" \
    --output_type PLY

# Check if conversion was successful
if [ $? -eq 0 ] && [ -f "$OUTPUT_FILE" ]; then
    echo "Success! PLY file created: $OUTPUT_FILE"
    
    # Get file size
    FILE_SIZE=$(du -h "$OUTPUT_FILE" | cut -f1)
    echo "File size: $FILE_SIZE"
    
    # Count number of points (rough estimate from file size)
    NUM_LINES=$(wc -l < "$OUTPUT_FILE")
    echo "Approximate number of points: $((NUM_LINES - 15))"
else
    echo "Error: Conversion failed"
    exit 1
fi