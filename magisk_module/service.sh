@@
-        component_isolate_execute "DAEMON" "$MODDIR/system/bin/ivanna_daemon" "--socket" "@omega_daemon_socket" "--realtime"
+        # Start daemon with both abstract socket and TCP loopback fallback for robustness
+        component_isolate_execute "DAEMON" "$MODDIR/system/bin/ivanna_daemon" "--socket" "@omega_daemon_socket" "--tcp-port" "12121" "--realtime"
         
