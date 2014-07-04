LD_LIBRARY_PATH=/opt/xorg/lib ./X -config /root/xorg.conf.new   -modulepath /opt/xorg/lib/xorg/modules -logverbose 10 -verbose 10 -ignoreABI :1 -ac 2>&1 | less
