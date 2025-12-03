#!/bin/bash

# Configuration - Set defaults here
ENABLE_FUSECACHE_AUTOSTART=true
user="<user>"
pass="<pass>"
orig_share="//<host>/<share>"
orig_path="<orig_path>"
mnt_path="<mnt_path>"

# Upload / download bandwidth limits in MB/s
upload_limit="3.0"
download_limit="5.0"

# Update package repository and install dependencies
sudo apt update
sudo apt install -y libfuse3-3 libfuse3-dev pkgconf build-essential samba smbclient cifs-utils git nano g++

# Clone fusecache repo if not already cloned
if [ ! -d "fusecache" ]; then
  git clone https://github.com/zwodev/fusecache.git
fi

# Compile fusecache
cd fusecache || exit
g++ -Wall fusecache.c CacheManager.cpp `pkg-config fuse3 --cflags --libs` -o fusecache
cd ..

# Enable 'user_allow_other' in /etc/fuse.conf if not already enabled
if ! grep -q '^user_allow_other' /etc/fuse.conf; then
  echo "Enabling user_allow_other in /etc/fuse.conf"
  echo "user_allow_other" | sudo tee -a /etc/fuse.conf
fi

# Ensure paths exist
sudo mkdir -p "$orig_path"
sudo chown $user:$user "$orig_path"
sudo mkdir -p "$mnt_path"
sudo chown $user:$user "$mnt_path"

# Add or update samba user password non-interactively
(echo "$pass"; echo "$pass") | sudo smbpasswd -s -a "$user" 2>/dev/null || sudo smbpasswd -s "$user" <<< "$pass"

# Create credentials file for permanent mount
credentials_file="/etc/smbcredentials.$user"
echo "username=$user" | sudo tee "$credentials_file"
echo "password=$pass" | sudo tee -a "$credentials_file"
sudo chmod 600 "$credentials_file"

# Add permanent mount entry to fstab if not already present
fstab_entry="$orig_share $orig_path cifs credentials=$credentials_file,cache=none,noperm,uid=$(id -u "$user"),gid=$(id -g "$user"),file_mode=0777,dir_mode=0777,_netdev,nofail 0 0"
fstab="/etc/fstab"

if ! grep -q "$orig_path" "$fstab"; then
  echo "Adding permanent mount entry to $fstab"
  echo "$fstab_entry" | sudo tee -a "$fstab"
else
  echo "fstab entry for $orig_path already exists"
fi

# Mount immediately to test
sudo systemctl daemon-reload
sudo mount -a

# Append the Samba share configuration to smb.conf if not already present
smb_conf="/etc/samba/smb.conf"
if ! sudo grep -q "\[CachedShare\]" "$smb_conf"; then
  echo "Adding CachedShare configuration to $smb_conf"
  sudo bash -c "cat >> $smb_conf" <<EOL

[CachedShare]
   valid users = $user
   path = $mnt_path
   read only = no
   writable = yes
   printable = no
   guest ok = no
   browsable = yes
   create mask = 0770
   directory mask = 0770
EOL
  sudo systemctl restart smbd
else
  echo "CachedShare already configured in smb.conf"
  sudo systemctl restart smbd
fi

# Auto-start fusecache service if enabled
if [ "$ENABLE_FUSECACHE_AUTOSTART" = "true" ]; then
  echo "Setting up fusecache systemd autostart..."
  
  service_file="/etc/systemd/system/fusecache.service"
  fusecache_path="$(pwd)/fusecache/fusecache"
  
  sudo bash -c "cat > $service_file" <<EOL
[Unit]
Description=Fusecache Service for $mnt_path
After=network-online.target remote-fs.target
Wants=network-online.target

[Service]
Type=simple
User=$user
WorkingDirectory=$(pwd)/fusecache
ExecStart=$fusecache_path -ulimit $upload_limit -dlimit $download_limit -o allow_other "$mnt_path"
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOL

  # Reload systemd, enable, and start service
  sudo systemctl daemon-reload
  sudo systemctl enable fusecache.service
  sudo systemctl start fusecache.service
  
  echo "fusecache systemd service installed, enabled, and started."
  echo "Status: sudo systemctl status fusecache.service"
else
  echo "fusecache autostart skipped (ENABLE_FUSECACHE_AUTOSTART=false)"
fi

echo "Automation complete! Mount is permanent via fstab."
echo "Verify mount: mount | grep $orig_path"
