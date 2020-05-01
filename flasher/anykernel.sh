# AnyKernel3 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
# begin properties
properties() { '
kernel.string=
do.devicecheck=1
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=0
device.name1=X00T
device.name2=X00TD
device.name3=X00TDA
device.name4=
device.name5=
supported.versions=
supported.patchlevels=
'; } # end properties

# shell variables
block=/dev/block/platform/soc/c0c4000.sdhci/by-name/boot;
is_slot_device=0;
ramdisk_compression=auto;


## AnyKernel methods (DO NOT CHANGE)
# import patching functions/variables - see for reference
. tools/ak3-core.sh;

## AnyKernel install
dump_boot;

# begin userspace changes

# mount vendor
mount -o rw,remount -t auto /vendor >/dev/null;

# backup files
if [ ! -f /vendor/bin/init.qcom.post_boot.sh.bkp ]; then
	cp -rpf /vendor/bin/init.qcom.post_boot.sh /vendor/bin/init.qcom.post_boot.sh.bkp;
fi
if [ ! -f /vendor/etc/thermal-engine.conf.bkp ]; then
	cp -rpf /vendor/etc/thermal-engine.conf /vendor/etc/thermal-engine.conf.bkp;
fi
if [ ! -f /vendor/etc/perf/perfboostsconfig.xml.bkp ]; then
	cp -rpf /vendor/etc/perf/perfboostsconfig.xml /vendor/etc/perf/perfboostsconfig.xml.bkp;
fi

# replace files
cp -rpf $patch/init.qcom.post_boot.sh /vendor/bin/init.qcom.post_boot.sh;
cp -rpf $patch/thermal-engine.conf /vendor/etc/thermal-engine.conf;
cp -rpf $patch/perfboostsconfig.xml /vendor/etc/perf/perfboostsconfig.xml;
cp -rpf $patch/perf-profile0.conf /vendor/etc/perf/perf-profile0.conf;

# set up permissions
chmod 0644 vendor/etc/perf/perf-profile0.conf;
chmod 0777 /vendor/bin/init.qcom.post_boot.sh;
chmod 0777 /vendor/etc/thermal-engine.conf;
chmod 0644 /vendor/etc/perf/perfboostsconfig.xml;

# end userspace changes

write_boot;
## end install

