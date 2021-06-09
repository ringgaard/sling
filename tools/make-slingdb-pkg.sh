#!/bin/bash

# Build Ubuntu/Debian package for installing SLING DB.
VERS=3.0-1
PKGDIR=local/slingdb
echo "Build slingdb package in" PKGDIR

# Make directories
echo "Make directories"
mkdir -p $PKGDIR/DEBIAN
mkdir -p $PKGDIR/usr/local/bin
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

# Make systemd service unit.
echo "Make systemd service unit"
cat << EOF > $PKGDIR/etc/systemd/system/slingdb.service
[Unit]
Description=SLINGDB persistent key-value store
After=network.target

[Service]
User=root
WorkingDirectory=/var/lib/slingdb
ExecStart=/usr/local/bin/slingdb --dbdir /var/lib/slingdb --addr 127.0.0.1 --port 7070 --auto_mount --flushlog --shortlog
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

# Build package.
echo "Build package"
dpkg-deb --build $PKGDIR
mv $PKGDIR.deb data/e/dist/

