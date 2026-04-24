# Multi-Container Runtime 

## Overview

This project implements a lightweight container runtime in C, inspired by core ideas behind Docker. It supports launching and managing multiple containers using Linux system calls and namespaces, along with a kernel module for monitoring memory usage.

The system follows a **supervisor-based architecture**, where a long-running process manages container lifecycle and communicates with user commands through inter-process communication (IPC).

---

## Features Implemented

### 1. Container Runtime

* Containers are created using `clone()`
* Namespaces used:

  * PID namespace (process isolation)
  * Mount namespace (filesystem isolation)
  * UTS namespace (hostname isolation)
* Filesystem isolation using `chroot()`
* `/proc` mounted inside containers for process visibility

---

### 2. Supervisor Architecture

* A long-running **supervisor process** manages all containers
* CLI commands communicate with the supervisor
* Ensures centralized control and lifecycle management

---

### 3. Inter-Process Communication (IPC)

* Implemented using **UNIX domain sockets**
* Socket path: `/tmp/mini_runtime.sock`
* Communication flow:

  * CLI → sends request (`start`, `run`)
  * Supervisor → receives and executes container

---

### 4. CLI Commands

| Command                     | Description                   |
| --------------------------- | ----------------------------- |
| `supervisor`                | Starts the supervisor process |
| `start <id> <rootfs> <cmd>` | Starts a container            |
| `run <id> <rootfs> <cmd>`   | Same as start                 |
| `ps`                        | Lists containers (metadata)   |
| `stop <id>`                 | Stops a container             |
| `killall`                   | Stops all containers          |

---

### 5. Metadata Tracking

* Container details stored in `containers.txt`
* Tracks:

  * Container ID
  * Command executed
* Used by `ps` command to display running containers

---

### 6. Kernel Module Integration

A kernel module (`monitor.c`) monitors container memory usage.

#### Features:

* Tracks memory usage per container
* Uses `ioctl` interface for communication
* Enforces:

  * **Soft limit** → logs warning
  * **Hard limit** → kills process

#### Example:

* Soft limit: 40 MB
* Hard limit: 64 MB

---

### 7. Test Workloads

| Program      | Purpose                      |
| ------------ | ---------------------------- |
| `memory_hog` | Simulates high memory usage  |
| `cpu_hog`    | Simulates CPU-intensive load |
| `io_pulse`   | Simulates I/O operations     |

---

## Build Instructions

```bash
cd boilerplate
make
```

---

## Running the System

### 1. Load kernel module

```bash
sudo insmod monitor.ko
```

---

### 2. Start supervisor

```bash
sudo ./engine supervisor ../rootfs-base
```
<img width="733" height="182" alt="supervisor" src="https://github.com/user-attachments/assets/85732e8a-8e18-4c5b-a8b6-c3a209087067" />


---

### 3. Run containers (new terminal)

```bash
sudo ./engine start alpha ../rootfs-base "/bin/ls"
sudo ./engine start beta ../rootfs-base "/bin/ps"
sudo ./engine start gamma ../rootfs-base "/bin/ps"
```
<img width="730" height="95" alt="alpha" src="https://github.com/user-attachments/assets/9d0ac247-b839-48c6-99e2-1c0e65d4128d" />


---

### 4. View containers

```bash
./engine ps
```
<img width="740" height="485" alt="dmesg" src="https://github.com/user-attachments/assets/1f2c5437-aacc-483b-9117-71d9143a7b20" />

---

### 5. Stop containers

```bash
sudo ./engine stop alpha
sudo ./engine stop beta
sudo ./engine stop gamma
```

---

### 6. Kill all containers

```bash
sudo ./engine killall
```

---

### 7. Check kernel logs

```bash
dmesg | tail
```
<img width="740" height="485" alt="dmesg" src="https://github.com/user-attachments/assets/d9b295bc-f10c-4695-9036-0bfd84328699" />

---

## Design Decisions

* Used **UNIX sockets** instead of pipes for flexible IPC
* Used **single-threaded supervisor** for simplicity and stability
* Metadata stored in file instead of in-memory structure for persistence
* Simplified logging system (not fully implemented)

---

## Limitations

* Logging system (bounded buffer + thread) not fully implemented
* Metadata tracking is file-based, not fully synchronized
* No advanced scheduling or resource isolation beyond memory monitoring

---

## Key Concepts Demonstrated

* Linux namespaces (`clone`)
* Filesystem isolation (`chroot`)
* Inter-process communication (UNIX sockets)
* Kernel module development
* Memory monitoring and enforcement
* Process lifecycle management

---

## Conclusion

This project demonstrates a working prototype of a container runtime system, combining user-space process management with kernel-space monitoring. It highlights core OS concepts such as isolation, IPC, threading and resource control.

