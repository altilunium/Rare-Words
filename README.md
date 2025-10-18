# Rare-Words

**RareWords — A Lightweight Native Windows App for Collecting Words Data**

RareWords is a minimal, high-performance **C++/Win32 desktop application** built to collect, organize, and browse *rare or interesting words* discovered during language study. It uses an embedded **SQLite** database for fast, reliable local storage — no dependencies, no servers, no telemetry.

### Features

* Native Windows GUI (built with pure Win32 API) — fast startup, minimal memory footprint
* CRUD operations for managing your word collection
* Fields for **word**, **definition**, **example**, **etymology**, and **tags**
* Built-in **SQLite** database integration (no external setup required)
* Editable and deletable entries via simple list-based interface

### Tech Stack

* **Language:** C++17
* **Framework:** Win32 API
* **Database:** SQLite3 (embedded, single-file database)


### Getting Started

1. Clone this repository.
2. Open `RareWords.sln` in Visual Studio.
3. Make sure `sqlite3.c` and `sqlite3.h` are in the same directory as `rare.cpp`.
4. Set the subsystem to **Windows (/SUBSYSTEM:WINDOWS)** in project properties.
5. Build and run.

### Why

Language learners, writers, and linguistics enthusiasts often encounter fascinating words they want to remember — but not every note-taking app is designed for that. RareWords focuses purely on *speed, simplicity, and permanence*.



