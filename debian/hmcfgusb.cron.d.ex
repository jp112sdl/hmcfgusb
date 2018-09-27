#
# Regular cron jobs for the hmcfgusb package
#
0 4	* * *	root	[ -x /usr/bin/hmcfgusb_maintenance ] && /usr/bin/hmcfgusb_maintenance
