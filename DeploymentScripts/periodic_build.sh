case "$1" in 
start)
   /root/DeploymentScripts/derp_build.sh && wait && $0 stop &
   echo $!>/var/run/hit.pid
   
   ;;
stop)
   kill `cat /var/run/hit.pid` &> /dev/null
   rm /var/run/hit.pid
   echo periodic_build.sh is stopping...
   ;;
restart)
   $0 stop
   $0 start
   ;;
status)
   if [ -e /var/run/hit.pid ]; then
      echo periodic_build.sh is running, pid=`cat /var/run/hit.pid`
   else
      echo periodic_build.sh is NOT running
      exit 1
   fi
   ;;
*)
   echo "Usage: $0 {start|stop|status|restart}"
esac

exit 0 
