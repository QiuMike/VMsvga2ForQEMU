# VMSvga2 for QEMU 

*******************************
I have already make some patchset(implemented some features VMWare official driver requires) to make QEMU natively support VMware Tools Driver, which already signed, and
Easy to install(Just get the darwin.iso from VMware)

I will push the patchset to QEMU upstream if anyone interested, pls let me know.
*******************************

This Kext soure is 
based on https://github.com/mirror/VMsvga2
         https://github.com/FMX/VMSvga2ForHighSierra


In order to try macOS Catalina with QEMU/KVM hypervisor, I modified VMsvga2 source code to work in guest vm which runs macOS Catalina.

Prebuilt version are in build folder.

It just works.

I tested in QEMU vervsion 6.0 from upstream.
the OSX base on project https://github.com/kholia/OSX-KVM.git

Usage:
[install]
sudo ./install.sh

[uninstall]
sudo ./install.sh

Additional info:
Sign the extension yourself.

If do not have sign Certs,
you need tell kernel to load non-certs modules.

Step 1: Found out the EFI partion, my case is disk1s1

  % diskutil list

  /dev/disk0 (internal):
     #:                       TYPE NAME                    SIZE       IDENTIFIER
     0:      GUID_partition_scheme                         402.7 MB   disk0
     1:                        EFI EFI                     152.6 MB   disk0s1
     2:           Linux Filesystem                         247.0 MB   disk0s2

  /dev/disk1 (internal):
     #:                       TYPE NAME                    SIZE       IDENTIFIER
     0:      GUID_partition_scheme                         68.7 GB    disk1
     1:                        EFI EFI                     209.7 MB   disk1s1
     2:                 Apple_APFS Container disk2         68.4 GB    disk1s2

  /dev/disk2 (synthesized):
     #:                       TYPE NAME                    SIZE       IDENTIFIER
     0:      APFS Container Scheme -                      +68.4 GB    disk2
                                 Physical Store disk1s2
     1:                APFS Volume macOS - Data            39.6 GB    disk2s1
     2:                APFS Volume Preboot                 83.9 MB    disk2s2
     3:                APFS Volume Recovery                529.0 MB   disk2s3
     4:                APFS Volume VM                      1.1 GB     disk2s4
     5:                APFS Volume macOS                   11.2 GB    disk2s5

  /dev/disk3 (internal, physical):
     #:                       TYPE NAME                    SIZE       IDENTIFIER
     0:                                                   *9922901320141763330 B  disk3


Step 2: make dir in /Volumes/ folder

% sudo mkdir /Volumes/EFI

Step 3: mount the EFI partition

% sudo mount -t msdos /dev/disk1s1 /Volumes/EFI

Step 4: add params to config.plist, kext-dev-mode=1 to make non-signed kexts loadable

% vim /Volumes/EFI/EFI/OC/config.plist

                        <dict>
                                <key>SystemAudioVolume</key>
                                <data>Rg==</data>
                                <key>boot-args</key>
                                <string>-v keepsyms=1 tlbto_us=0 vti=9 kext-dev-mode=1</string>
                                <key>run-efi-updater</key>
                                <string>No</string>
                                <key>csr-active-config</key>
                                <data>ZwAAAA==</data>
                                <key>prev-lang:kbd</key>
                                <data>ZW4tVVM6MA==</data>
                                <key>ForceDisplayRotationInEFI</key>
                                <integer>0</integer>
                        </dict>



Step 5: save the config.plist, and reboot your OSX


Another way could be put the kext to EFI/OC/Kexts, and modify the config.plist, I have not tested yet.

if you wanna to have qemu native support version pls contact me via qdy220091330@gmail.com
