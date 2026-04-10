# PhantomD — Memory Hex Dumper

**PhantomD** is a lightweight Windows memory inspection tool built with the Win32 API and GDI+. It allows you to attach to a running process, explore its memory regions, and visualize raw bytes in a structured hex dump interface with real-time decoding.

---

## Features

* **Process Memory Inspection** 

  * Attach to running processes
  * Enumerate and browse memory regions

* **Hex Dump Viewer**

  * Structured hex + ASCII display
  * Smooth scrolling and navigation

* **Byte Inspector**

  * Interpret selected bytes as:

    * Integers (`INT8` → `INT64`)
    * Unsigned integers
    * Floating point (`float`, `double`)
    * ASCII text

* **Jump to Address**

  * Direct navigation to any memory address (`Ctrl+G`)

* **Bookmarks**

  * Save important memory rows

* **Live Refresh**

  * Reload memory regions on demand (`F5`)

* **Clipboard Support**

  * Copy full hex dump (`Ctrl+C`)

---

## Platform

* **OS:** Windows 10 / 11 (x64)
* **API:** Win32 + GDI+
* **Language:** C (C11)

---

## Build Instructions

### Using MinGW / w64devkit

```bash
make
```

### Using MSVC

```bash
nmake /f Makefile TOOLCHAIN=msvc
```

---

## Project Structure

```
PhantomD/
├── .gitignore
├── Makefile
│ 
│
├── include/              # Header files
│   ├── gui.h
│   ├── hexdump.h
│   ├── memory.h
│   ├── process.h
│   ├── resource.h
│   └── utils.h
│
├── res/                  # Resources (icons, Win32 resources)
│   ├── phantomd.ico
│   └── resource.rc
│
└── src/                  # Implementation files
    ├── gui.c
    ├── hexdump.c
    ├── main.c
    ├── memory.c
    ├── process.c
    └── utils.c
```

---

## Usage

1. Build the project
2. Run the executable
3. Select a target process
4. Browse memory regions
5. Inspect and analyze memory contents

---


## Disclaimer

This tool is intended for **educational and debugging purposes only**.
Use responsibly and ensure compliance with applicable laws and software terms.

---
