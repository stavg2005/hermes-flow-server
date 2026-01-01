
# HermesFlow Server

HermesFlow is an asynchronous audio processing server written in C++23. It provides a real-time engine for defining and executing audio graphs (mixing, effects, and file inputs) dynamically.

The server uses **Boost.Asio** and **Boost.Beast** for network I/O, **io_uring** for asynchronous disk operations on Linux, and integrates with **S3/MinIO** for asset retrieval.

## Features

* **Dynamic Audio Graph:** Processing pipelines are defined at runtime using a JSON structure.
* **Real-Time Architecture:** 20ms processing frames with soft-clipping protection for mixer summation.
* **Networking:**
    * **HTTP/1.1 API:** For session creation and control.
    * **WebSocket:** For streaming real-time session statistics.
    * **RTP:** Zero-copy UDP streaming (A-Law/G.711a) to clients.
* **Storage:** Automatic on-demand fetching of audio assets from S3-compatible storage.

## Prerequisites

* **Compiler:** Clang 21+ (C++23 support required).
* **Build System:** CMake 3.27+ and Ninja.
* **Libraries:**
    * **Linux:** OpenSSL, liburing (optional).
    * **Windows:** OpenSSL must be installed via **vcpkg**. You must set the `VCPKG_ROOT` environment variable to your vcpkg installation path.
* **Infrastructure:** MinIO or AWS S3.

## Build and Run

### Docker (Recommended)

The provided Docker Compose configuration brings up the server and a local MinIO instance.

```bash
docker-compose up --build

```

* **API Endpoint:** `http://localhost:5000`
* **MinIO Console:** `http://localhost:9001`

### Manual Build

1. **Configure:**
**Linux:**
```bash
cmake --preset linux-clang

```


**Windows:**
Ensure `VCPKG_ROOT` is set in your environment variables before running this command.
```bash
cmake --preset windows-clang-vcpkg

```


2. **Build:**
```bash
cmake --build build --parallel

```


3. **Run:**
Ensure `config.toml` is present in the working directory.
```bash
./build/Server

```



## Configuration

Server settings are defined in `config.toml`.

```toml
[server]
address = "127.0.0.1"
port = 5000
threads = 4

[s3]
host = "127.0.0.1"
port = "9000"
bucket = "audio-files"
# AWS credentials...

```

## API Reference

### 1. Start Session

`POST /transmit/`

Initializes an audio graph. The payload defines the nodes (inputs, effects, outputs) and the edges (connections).

**Request Body:**

```json
{
  "flow": {
    "start_node": { "id": "mixer1" },
    "nodes": [
      { "id": "file1", "type": "fileInput", "data": { "fileName": "input.wav" } },
      { "id": "gain1", "type": "fileOptions", "data": { "gain": 0.8 } },
      { "id": "mixer1", "type": "mixer", "data": {} },
      { "id": "out", "type": "clients", "data": { "clients": [{ "ip": "127.0.0.1", "port": 4000 }] } }
    ],
    "edges": [
      { "source": "gain1", "target": "file1" },
      { "source": "file1", "target": "mixer1" },
      { "source": "mixer1", "target": "out" }
    ]
  }
}

```

### 2. Monitor Session

`GET /connect/?id={sessionID}`

Upgrades the connection to a WebSocket. The server pushes JSON status updates every 100ms containing progress and byte counters.

### 3. Stop Session

`POST /stop/?id={sessionID}`

Terminates the processing loop and cleans up associated resources.

## Graph Node Types

| Type | Description |
| --- | --- |
| `fileInput` | Streams a WAV file from S3/Disk. |
| `fileOptions` | Configuration node (e.g., Gain) applied to a target input. |
| `mixer` | Sums multiple audio sources. |
| `delay` | Inserts silence. |
| `clients` | Specifies RTP destinations (IP/Port). |

```

```
