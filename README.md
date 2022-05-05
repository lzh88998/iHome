# iHome
Intelligent Home Controlling System

To use this in production, user need to modify the TCP keep alive configuration of system. In Linux, the following lines need to be added to /etc/sysctl.conf:

*net.ipv4.tcp_keepalive_time=5*

*net.ipv4.tcp_keepalive_intvl=5*

*net.ipv4.tcp_keepalive_probes=1*

