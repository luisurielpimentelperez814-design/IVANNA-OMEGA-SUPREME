@@
-    if (connect(sock, (struct sockaddr*)&addr, len)!= 0) {
-        close(sock);
-        bool expected = false;
-        if (warned.compare_exchange_strong(expected, true)) {
-            __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "daemon no disponible, durmiendo");
-        }
-        return nullptr;
-    }
-
-    char buf[1];
-    char cmsgbuf[CMSG_SPACE(sizeof(int))];
-    struct iovec iov = {.iov_base = buf,.iov_len = sizeof(buf)};
-    struct msghdr msg = {};
-    msg.msg_iov = &iov;
-    msg.msg_iovlen = 1;
-    msg.msg_control = cmsgbuf;
-    msg.msg_controllen = sizeof(cmsgbuf);
-
-    if (recvmsg(sock, &msg, 0) <= 0) { close(sock); return nullptr; }
-
-    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
-    if (!cmsg || cmsg->cmsg_len!= CMSG_LEN(sizeof(int))) { close(sock); return nullptr; }
-
-    int fd = *(int*)CMSG_DATA(cmsg);
-    cached = (OmegaSharedState*) mmap(NULL, sizeof(OmegaSharedState), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
-    close(sock);
-    close(fd);
-
-    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "puente memfd mapeado OK");
-    return (cached == MAP_FAILED)? nullptr : cached;
+    if (connect(sock, (struct sockaddr*)&addr, len)!= 0) {
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
+    }
+
+    char buf[1];
+    char cmsgbuf[CMSG_SPACE(sizeof(int))];
+    struct iovec iov = {.iov_base = buf,.iov_len = sizeof(buf)};
+    struct msghdr msg = {};
+    msg.msg_iov = &iov;
+    msg.msg_iovlen = 1;
+    msg.msg_control = cmsgbuf;
+    msg.msg_controllen = sizeof(cmsgbuf);
+
+    if (recvmsg(sock, &msg, 0) <= 0) { close(sock); return nullptr; }
+
+    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
+    if (!cmsg || cmsg->cmsg_len!= CMSG_LEN(sizeof(int))) { close(sock); return nullptr; }
+
+    int fd = *(int*)CMSG_DATA(cmsg);
+    cached = (OmegaSharedState*) mmap(NULL, sizeof(OmegaSharedState), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
+    close(sock);
+    close(fd);
+
+    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "puente memfd mapeado OK");
+    return (cached == MAP_FAILED)? nullptr : cached;
***
