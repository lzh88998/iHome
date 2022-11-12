# iHome
Intelligent Home Controlling System

To use this in production, user need to modify the TCP keep alive configuration of system. In Linux, the following lines need to be added to /etc/sysctl.conf:

*net.ipv4.tcp_keepalive_time=5*

*net.ipv4.tcp_keepalive_intvl=5*

*net.ipv4.tcp_keepalive_probes=1*

Also depending on different distribution of Linux, it might need to config the following item to avoid FIN_WAIT1 when killing processes

*net.ipv4.tcp_max_orphans=0*

*net.ipv4.tcp_orphans_retries=1*

TODO:

May need to add copy service scripts to /usr/lib/systemd/system/.

May need to add services?

May need to add crontab script

**/30 * * * * /src/iHome/script/weather_forcast.sh*
