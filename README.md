
# HTTP Proxy Server with Caching

This project is a **multi-threaded HTTP/1.1 proxy server written in C**, developed to practice systems programming, raw socket communication, concurrency with threads, and file-based caching mechanisms. The proxy listens for client HTTP GET requests, forwards them to the requested host (port 80), and caches responses locally using MD5-based hashing. The server also supports access control via a configurable blocklist.

It handles multiple simultaneous client connections using POSIX threads and enforces cache expiration rules based on a timeout value supplied at runtime.

---

## Features

### Multi-Threaded Request Handling
- Each client connection is handled in a dedicated `pthread`
- Shared cache access is protected with mutex locks

### Caching System
- Cached content is stored in the `./cache` directory
- Each response is saved as a file named with the MD5 hash of `hostname + path`
- Timeout-based cache expiration (defined at runtime)
- File read/write with safe concurrent access

### Blacklist Filtering
- Blocks requests to specific IPs or hostnames defined in a `blocklist` file
- Returns `403 Forbidden` if a blocked resource is requested

### HTTP/1.1 Protocol Support
- Supports only `GET` requests
- Parses HTTP/1.0 and HTTP/1.1 requests
- Returns proper HTTP error codes:  
  `400 Bad Request`, `403 Forbidden`, `404 Not Found`, `405 Method Not Allowed`, `505 HTTP Version Not Supported`

### DNS Caching
- Resolved IPs are reused in memory for the duration of the proxy runtime
- Uses `gethostbyname()` for hostname resolution

---

## Technologies Used

- **C (POSIX)** — Systems programming and thread control
- **Sockets API** — TCP communication with clients and web servers
- **Pthreads** — Concurrent handling of client requests
- **OpenSSL (MD5)** — Used for hashing cache keys
- **UNIX system calls** — For file I/O, networking, and error handling

---

## Build & Run

 Compile using `gcc`:

```bash
gcc -o webproxy proxy.c -pthread -lssl -lcrypto
```

Run the proxy:

```bash
./webproxy <port> [timeout]
```

- `<port>`: Port to listen on for incoming HTTP requests
- `[timeout]`: (Optional) Time in seconds before cached pages expire. If not provided, cache never expires.


---

## Usage (Browser Test)

1. Open your browser settings.
2. Set the HTTP proxy to `127.0.0.1` and the port you chose above.
3. Visit a basic HTTP-only site like `http://netsys.cs.colorado.edu`.
4. Repeat the request to observe cache hits (files stored in `./cache/`).

> **Note:** HTTPS (`CONNECT` requests or port 443) and persistent connections are not supported.

---

## Project Structure

```
proxy-server/
├── cache/          # Folder to store cached responses (created at runtime)
├── proxy.c         # Core proxy server implementation
└── blocklist       # List of blocked domains/IPs (one per line)
```

---

## Limitations

- Only `GET` requests are supported; others return 405
- Does not support HTTPS (port 443) or persistent connections
- Cache files are not deleted automatically after expiration
