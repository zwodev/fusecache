# fusecache

## About
fusecache is a simple application that syncs two directories over a potentially slow internet connection. It is based on libfuse. It has seperate read and write caches. The read cache is only filled on demand. The write cache is synced back using rsync. Bandwidth limits can be set individually for up- and downstream channels.

This software has been developed for use with the render management software [Royal Render](https://royalrender.de).

WARNING: This tool is currently WIP and should not be used in production yet.

## Install Dependencies
``` sudo apt install libfuse3-3 libfuse3-dev pkgconf build-essential```

## Compiling
``` g++ -Wall fusecache.c CacheManager.cpp `pkg-config fuse3 --cflags --libs` -o fusecache ```

## Directory Structure
fusecache creates 3 sub-directories when run the first time:

### ./mnt
This is where libfuse mounts the virtual filesystem. Can be shared using SMB, NFS, etc.

### ./orig
This is where you can mount your source directory. This could be located on your local SMB server for example.

### ./cache
This directory contains the read and write file caches.

## Usage
``` ./fusecache -ulimit 2.4 -dlimit 5.7 ```

You can specify limits to prevent bandwidth exhaustion:
* -ulimit (upload bandwidth limit)
* -dlimit (specifies the download bandwidth limit)