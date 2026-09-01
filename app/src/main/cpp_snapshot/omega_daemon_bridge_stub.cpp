@@
     if (connect(sock, (struct sockaddr*)&addr, len)!= 0) {
-        close(sock);
-        bool expected = false;
-        if (warned.compare_exchange_strong(expected, true)) {
-            __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "daemon no disponible, durmiendo");
-        }
-        return nullptr;
+        // Try TCP loopback fallback to query daemon health (non-memfd path)
+        close(sock);
+        bool expected = false;
+        if (warned.compare_exchange_strong(expected, true)) {
+            __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "daemon unix-socket no disponible, intentando fallback TCP");
+        }
+        // Attempt TCP connection to localhost:12121 (best-effort)
+        int tsock = socket(AF_INET, SOCK_STREAM, 0);
+        if (tsock < 0) return nullptr;
+        struct sockaddr_in taddr; memset(&taddr,0,sizeof(taddr));
+        taddr.sin_family = AF_INET; taddr.sin_port = htons(12121);
+        taddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
+        if (connect(tsock, (struct sockaddr*)&taddr, sizeof(taddr)) == 0) {
+            const char* q = "{\"action\":\"GET_HEALTH\"}\n";
+            send(tsock, q, strlen(q), 0);
+            char buf[65536]; ssize_t n = recv(tsock, buf, sizeof(buf)-1, 0);
+            if (n>0) {
+                buf[n]=0;
+                __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "daemon TCP fallback reply: %s", buf);
+            }
+            close(tsock);
+        } else {
+            close(tsock);
+        }
+        return nullptr;
     }
***
