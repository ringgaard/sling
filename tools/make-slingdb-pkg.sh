#!/bin/bash

# Build Ubuntu/Debian package for installing SLING DB.
VERS=3.0-1
PKGDIR=local/slingdb
echo "Build slingdb package in" PKGDIR

# Make directories
echo "Make directories"
mkdir -p $PKGDIR/DEBIAN
mkdir -p $PKGDIR/usr/local/bin
mkdir -p $PKGDIR/etc/slingdb
mkdir -p $PKGDIR/etc/systemd/system
mkdir -p $PKGDIR/var/lib/slingdb

# Copy files.
echo "Copy files"
cp -f bazel-bin/sling/db/slingdb $PKGDIR/usr/local/bin/

# Make Debian control file.
echo "Make Debian control file"
cat << EOF > $PKGDIR/DEBIAN/control
Package: slingdb
Version: $VERS
Section: misc
Priority: optional
Architecture: amd64
Build-Depends: dh-systemd (>=1.5), debhelper-compat (>= 12)
Maintainer: Michael Ringgaard <michael@ringgaard.com>
Description: SLINGDB persistent key-value store
EOF

# Make Debian rules file.
echo "Make Debian rules file"
cat << EOF > $PKGDIR/DEBIAN/rules
%:
	dh $@ --with systemd
EOF

# Make Debian post-install script.
echo "Make Debian post-install script"
cat << EOF > $PKGDIR/DEBIAN/postinst
#!/bin/sh
set -e

#DEBHELPER#

echo "Enable SLINGDB service to start on boot"
systemctl enable slingdb
echo "Start SLINGDB service"
systemctl start slingdb

exit 0
EOF
chmod +x $PKGDIR/DEBIAN/postinst

# Make Debian pre-removal script.
echo "Make Debian pre-removal script"
cat << EOF > $PKGDIR/DEBIAN/prerm
#!/bin/sh
set -e

#DEBHELPER#

echo "Stop SLINGDB service"
systemctl stop slingdb

exit 0
EOF
chmod +x $PKGDIR/DEBIAN/prerm

# Make slingdb config file.
cat << EOF > $PKGDIR/etc/slingdb/slingdb.conf
#
# SLINGDB configuration
#

# Database directory.
dbdir=/var/lib/slingdb

# PID file for monitoring.
pidfile=/var/run/slingdb.pid

# Network address and port for service. Comment out the addr line to
# listen on all network interfaces.
addr=127.0.0.1
port=7070

# Number of network worker threads for handling requests.
workers=16

# Automatically mount all databases in database directory on startup.
auto_mount=true

# Recover inconsistent databases on startup.
recover=true
EOF

# Make systemd service unit.
echo "Make systemd service unit"
cat << EOF > $PKGDIR/etc/systemd/system/slingdb.service
[Unit]
Description=SLINGDB persistent key-value store
After=network.target

[Service]
User=root
WorkingDirectory=/var/lib/slingdb
ExecStart=/usr/local/bin/slingdb --config /etc/slingdb/slingdb.conf --flushlog --shortlog
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

# Build package.
echo "Build package"
dpkg-deb --build $PKGDIR
mv $PKGDIR.deb data/e/dist/

