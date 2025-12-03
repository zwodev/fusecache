#!/bin/bash

if [ $# -ne 1 ]; then
  echo "Usage: $0 <source_directory>"
  exit 1
fi

src_dir="$1"

if [ ! -d "$src_dir" ]; then
  echo "Error: Source directory does not exist: $src_dir"
  exit 1
fi

# Get file list and stats first
mapfile -t file_list < <(find "$src_dir" -type f -print0 | tr '\0' '\n')
file_count=${#file_list[@]}
total_size=$(du -sb "$src_dir" | awk '{print $1}')

echo "Pre-caching $file_count files totaling ${total_size} bytes from $src_dir"
echo "Starting file-by-file processing..."

current_file=0
for file in "${file_list[@]}"; do
  current_file=$((current_file + 1))
  filename=$(basename "$file")
  
  # Show current file before reading (clear line, update in place)
  printf "\r\033[K[%d/%d] Now caching: %s" "$current_file" "$file_count" "$filename"
  
  # Read file to cache (non-blocking display)
  cat "$file" > /dev/null 2>/dev/null
done

printf "\r\033[KPre-caching complete! All %d files processed successfully.\n" "$file_count"
