@@
-    for (int i=1;i<argc;i++) {
-        std::string arg=argv[i];
-        if (arg=="--socket" && i+1<argc) socket_path=argv[++i];
-        else if (arg=="--rate" && i+1<argc) { try{rate=std::stoi(argv[++i]);}catch(...){rate=48000;} }
-        else if (arg=="--buffer" && i+1<argc) { try{buffer=std::stoi(argv[++i]);}catch(...){buffer=64;} }
-        else if (arg=="--realtime") realtime=true;
-    }
+    int tcp_port = 0;
+    for (int i=1;i<argc;i++) {
+        std::string arg=argv[i];
+        if (arg=="--socket" && i+1<argc) socket_path=argv[++i];
+        else if (arg=="--rate" && i+1<argc) { try{rate=std::stoi(argv[++i]);}catch(...){rate=48000;} }
+        else if (arg=="--buffer" && i+1<argc) { try{buffer=std::stoi(argv[++i]);}catch(...){buffer=64;} }
+        else if (arg=="--realtime") realtime=true;
+        else if (arg=="--tcp-port" && i+1<argc) { try{ tcp_port = std::stoi(argv[++i]); } catch(...) { tcp_port = 0; } }
+    }
@@
-    while (g_running) {
+    while (g_running) {
+        // Polling the main unix socket and handling incoming clients happens in the main loop.
+        // Additionally, if a TCP fallback port was provided, start a separate listener thread
+        // once (detached) to accept connections on 127.0.0.1:tcp_port and dispatch JSON commands.
+        static std::atomic<bool> tcp_listener_started{false};
+        if (tcp_port > 0 && !tcp_listener_started.exchange(true)) {
+            try {
+                std::thread([tcp_port, &commandServer]() {
+                    int s = socket(AF_INET, SOCK_STREAM, 0);
+                    if (s < 0) return;
+                    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
+                    struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
+                    addr.sin_family = AF_INET;
+                    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
+                    addr.sin_port = htons((uint16_t)tcp_port);
+                    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(s); return; }
+                    if (listen(s, 8) < 0) { close(s); return; }
+                    log_message(std::string("TCP fallback listener active on 127.0.0.1:") + std::to_string(tcp_port));
+                    while (g_running) {
+                        struct sockaddr_in caddr; socklen_t clen = sizeof(caddr);
+                        int cfd = accept(s, (struct sockaddr*)&caddr, &clen);
+                        if (cfd < 0) { if (errno==EINTR) continue; break; }
+                        std::thread([cfd, &commandServer]() {
+                            // Simple TCP handler: read until EOF or a full JSON object/newline and dispatch
+                            std::string pending; pending.reserve(8192);
+                            char buf[8192];
+                            while (true) {
+                                ssize_t n = recv(cfd, buf, sizeof(buf), 0);
+                                if (n <= 0) break;
+                                pending.append(buf, (size_t)n);
+                                // try to extract JSON object
+                                size_t start = pending.find_first_not_of(" \t\r\n");
+                                if (start==std::string::npos) { pending.clear(); continue; }
+                                pending.erase(0, start);
+                                if (pending.empty()) continue;
+                                if (pending[0] != '{') {
+                                    // treat as single-line command
+                                    size_t nl = pending.find('\n');
+                                    std::string line = (nl==std::string::npos) ? pending : pending.substr(0,nl);
+                                    char reply[4096] = {};
+                                    int rlen = commandServer.handleTextCommand(line.c_str(), reply, sizeof(reply));
+                                    if (rlen>0) send(cfd, reply, rlen, MSG_NOSIGNAL);
+                                    if (nl==std::string::npos) pending.clear(); else pending.erase(0,nl+1);
+                                    continue;
+                                }
+                                // find matching braces
+                                int depth=0; bool inStr=false, esc=false; size_t endPos = std::string::npos;
+                                for (size_t i=0;i<pending.size();++i) {
+                                    char ch = pending[i];
+                                    if (inStr) { if (esc) esc=false; else if (ch=='\\') esc=true; else if (ch=='\"') inStr=false; continue; }
+                                    if (ch=='\"') { inStr=true; continue; }
+                                    if (ch=='{') ++depth; else if (ch=='}' && --depth==0) { endPos = i+1; break; }
+                                }
+                                if (endPos==std::string::npos) { if (pending.size()>65536) pending.clear(); continue; }
+                                std::string js = pending.substr(0,endPos);
+                                pending.erase(0,endPos);
+                                char reply[8192] = {};
+                                int rlen = commandServer.handleJsonCommand(js.c_str(), reply, sizeof(reply));
+                                if (rlen>0) send(cfd, reply, rlen, MSG_NOSIGNAL);
+                            }
+                            close(cfd);
+                        }).detach();
+                    }
+                    close(s);
+                }).detach();
+            } catch (...) { /* swallow thread creation failures */ }
+        }
***
