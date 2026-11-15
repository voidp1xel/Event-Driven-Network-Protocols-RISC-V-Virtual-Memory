<div align="center">

# ⚙️ Event-Driven Network Protocols & RISC-V Virtual Memory

**Low-level systems and network programming projects engineered from scratch in C (C23) and RISC-V OS internals with zero third-party dependencies.**

![C23](https://img.shields.io/badge/Language-C23_%26_RISC--V_ASM-00599C?style=for-the-badge&logo=c&logoColor=white)
![Kernel](https://img.shields.io/badge/Kernel-xv6--riscv_(Sv39_MMU)-FCC624?style=for-the-badge&logo=linux&logoColor=black)
![Networking](https://img.shields.io/badge/Networking-POSIX_poll(2)_%7C_TCP_%7C_UDP-FF6600?style=for-the-badge)
![License](https://img.shields.io/badge/License-MIT-44cc11?style=for-the-badge)

</div>

Low-level systems and network programming projects built from scratch in **C (C23)** and **RISC-V OS internals** with zero third-party libraries—spanning **virtual memory & trap handling (`xv6-riscv`)**, **peer-to-peer LAN multiplayer networking with a custom reliable UDP protocol (`Mastermind`)**, and a **non-blocking HTTP/1.1 CLI client (`Tempest`)**.

---

## Project Overview

| Component | Domain | Core Systems Concepts & APIs |
| :--- | :--- | :--- |
| **[`networking/mastermind`](networking/mastermind)** | Peer-to-Peer LAN Networking | `poll(2)` single-threaded event loop, UDP broadcast discovery (`:9000`), TCP (`8000–8099`), custom 6-byte reliable fragmented-UDP protocol, 9-state FSM, `termios` raw/non-canonical terminal I/O |
| **[`networking/tempest`](networking/tempest)** | Non-Blocking HTTP/1.1 Client | `getaddrinfo(3)`, `O_NONBLOCK` async `connect(2)` + `SO_ERROR`, `poll(2)` with shared 10 s monotonic deadline, RFC-compliant percent-encoding, `Transfer-Encoding: chunked` decoding |
| **[`xv6`](xv6)** | RISC-V OS Kernel | Sv39 Copy-on-Write (CoW) `fork()`, atomic physical page reference counting, `scause == 15` store-page-fault & kernel `copyout()` resolution, `sigalarm` / `sigreturn` preemptive user-level traps |

---

## Repository Structure

```text
.
├── Makefile                    # Root build & test runner (make / make xv6-test / make clean)
├── LICENSE                     # MIT License
├── networking/
│   ├── Makefile                # Build for Tempest & Mastermind
│   ├── mastermind/             # Peer-to-peer LAN Mastermind (TCP & Reliable UDP)
│   └── tempest/                # Non-blocking HTTP/1.1 weather client
└── xv6/                        # RISC-V xv6 kernel with CoW fork & sigalarm
```

### Quick Start (From Repository Root)

```bash
# Build both POSIX networking binaries (mastermind & tempest)
make

# Run automated xv6-riscv kernel test suites (cowtest & alarmtest)
make xv6-test

# Clean all build artifacts across networking/ and xv6/
make clean
```

---

## 1. Mastermind — LAN Multiplayer Game (`networking/mastermind`)

A two-player terminal **Mastermind** game over a local network featuring automatic UDP peer discovery, a single-threaded `poll(2)` event loop, and two selectable transport backends: standard **TCP** or a custom **reliable fragmented-UDP protocol (`--cost-cutting`)**.

### Build & Run

```bash
cd networking
make all
cd mastermind

# Standard TCP mode
./mastermind

# Reliable fragmented-UDP mode (both players must use the same mode)
./mastermind --cost-cutting

# Enable timestamped protocol logging to log.txt
./mastermind --log
```

### How to Play

1. **Enter Your Name:** On startup, enter a player name (max 31 characters). The client immediately begins broadcasting presence packets on the LAN.
2. **Lobby & Discovery:** Active peers on the same network appear in the **Players Online** table with an assigned numeric `ID`.
   - Challenge a player: `challenge <id>` (e.g., `challenge 1`)
   - Exit the application: `quit`
3. **Accepting / Rejecting:** The challenged player sees `Incoming challenge! Accept? (yes/no):` and types `yes` (becoming the **Codebreaker**) or `no`.
4. **Setting the Secret Code:** The challenger becomes the **Mastermind** and enters a secret **5-digit code** (e.g., `48219`).
5. **12-Round Gameplay Loop:**
   - The **Codebreaker** submits a 5-digit guess (up to **12 attempts**).
   - The **Mastermind** inspects the guess and enters 5 characters of feedback:
     - `x` (Green) — Right digit in the right position
     - `o` (Yellow) — Right digit in the wrong position
     - `-` (Red) — Digit not in the code
   - The ANSI-colored 12-row board updates after each turn.
6. **Game Over:** The game ends when the Codebreaker cracks the code (`xxxxx`) or exhausts all 12 attempts. The Mastermind reveals the secret sequence, and pressing `Enter` returns both players to the lobby.

### Architecture & Protocol Design

```text
+-------------------+       UDP :9000 ("MIND" + port + name)       +-------------------+
|   Peer A (Lobby)  | <------------------------------------------> |   Peer B (Lobby)  |
+-------------------+                                              +-------------------+
         |                                                                  |
         |  TCP (ports 8000-8099)  OR  Reliable 6B UDP (--cost-cutting)     |
         +------------------------------------------------------------------+
                 challenge / yes / no / start / <guess> / <feedback> / ping
```

#### Wire Packet Formats (`--cost-cutting` Reliable UDP)

| Packet Type | Size | Wire Layout | Purpose |
| :--- | :--- | :--- | :--- |
| **Discovery** | 38 B | `["MIND" (4B) \| port (2B) \| name (32B)]` | Broadcast every 2 s on `INADDR_BROADCAST:9000` |
| **DATA** | 6 B | `['D', msg_id, seq, total, b0, b1]` | Carries up to 2 payload bytes per datagram |
| **ACK** | 3 B | `['A', msg_id, seq]` | Per-chunk acknowledgement; triggers `100 ms` retransmit on loss |
| **PING** | 1 B | `['P']` | 1 s keepalive heartbeat (`3 s` dead-peer timeout) |

#### Module Breakdown

| Module | Responsibility | Implementation Highlights |
| :--- | :--- | :--- |
| **`mastermind.c`** | Event loop & terminal I/O | Multiplexes `stdin`, UDP discovery (`:9000`), game server socket (`8000–8099`), and active peer socket in a 20 ms `poll(2)` loop. Configures `termios` non-canonical mode (`~(ICANON \| ECHO)`) with `atexit` restoration so user input is buffered independently of screen redraws. |
| **`udp_utils.c`** | LAN peer discovery | Broadcasts a packed 38-byte `discovery_payload` every 2 seconds on `INADDR_BROADCAST:9000`. Filters self-broadcasts by matching port and testing local interface ownership via `bind()`. |
| **`lobby_utils.c`** | Roster state & rendering | Tracks up to 50 peers, prunes peers silent for >= 5 s, and performs **event-driven redraws** only when peers join, leave, or rename—preserving the user's partially typed command on the `> ` prompt. |
| **`tcp_utils.c`** | TCP transport | Binds the first available port in `8000–8099`, handles partial `send()` loops via `send_all()`, and uses `1 s` `ping` heartbeats with a `5 s` inactivity timeout. |
| **`cost_cutting.c`** | Reliable fragmented UDP | Splits messages into fixed **6-byte wire packets** and sends **3-byte ACKs**. Supports out-of-order chunk reassembly, `100 ms` per-chunk retransmission, and `1 s` heartbeats (`3 s` dead-peer timeout). |
| **`state_controller.c`** | 9-state FSM & framing | Drives transitions across `STATE_LOBBY`, `STATE_CHALLENGED`, `STATE_WAITING_ACCEPT`, `STATE_SETUP`, `STATE_PLAYING_CODEBREAKER`, `STATE_WAITING_OPPONENT`, `STATE_SENDING_FEEDBACK`, `STATE_GAME_OVER`, and `STATE_DISCONNECTED`. |
| **`game_logic.c` / `board_ui.c`** | Validation & ANSI UI | Validates 5-digit codes and `x`/`o`/`-` feedback strings, and renders the 12-attempt colorized terminal board. |

---

## 2. Tempest — Non-Blocking HTTP/1.1 Weather Client (`networking/tempest`)

A lightweight, zero-dependency HTTP/1.1 CLI client that fetches terminal-formatted weather forecasts from `wttr.is` under a strict 10-second wall-clock deadline.

### Build & Run

```bash
cd networking/tempest
make

# Standard output (strips HTTP headers, prints body)
./tempest London
./tempest "New York"

# Raw HTTP trace (prefixes outgoing request with "> " and response headers with "< ")
./tempest London --raw
```

### Implementation Highlights

- **RFC-Compliant URL Encoding (`http_utils.c`):** `encode_city()` percent-encodes (`%XX`) all characters outside `[A-Za-z0-9\-_.~]`, supporting multi-word queries such as `"New York"`.
- **Non-Blocking Connect & Shared 10 s Deadline (`network_utils.c`):**
  - Uses `start_timer(10)` to record a monotonic millisecond deadline (`deadline_ms`) instead of signal-based `SIGALRM`.
  - `connect_to_server()` resolves DNS via `getaddrinfo()`, sets `O_NONBLOCK` via `fcntl()`, initiates an asynchronous `connect()`, waits for `POLLOUT` in `wait_for_socket()` (bounded by remaining deadline), and inspects `getsockopt(..., SO_ERROR, ...)` to confirm connection status.
  - `send_all()` and `handle_response()` share the same remaining time budget.
- **HTTP/1.1 Response & Chunked Transfer Decoding (`http_utils.c`):**
  - Verifies `HTTP/1.1 200` status (printing `tempest: invalid location` and exiting with status `1` otherwise; `--raw` prints the full request/response trace before exiting `1`).
  - Performs case-insensitive header inspection for `Transfer-Encoding: chunked` and decodes `<hex size>\r\n<data>\r\n` frames cleanly.

---

## 3. xv6-riscv Kernel Extensions (`xv6`)

Kernel-level enhancements to **xv6-riscv** implementing **Copy-on-Write (CoW) `fork()`** and **preemptive user-level timer alarms (`sigalarm` / `sigreturn`)**.

### Build & Verification

**Prerequisites:** `riscv64-unknown-elf-gcc` (or `riscv64-elf-gcc`), `qemu-system-riscv64`, `python3`.

```bash
cd xv6

# Run automated test suites
python3 test-xv6.py cowtest
python3 test-xv6.py alarmtest

# Or boot xv6 interactively in QEMU
make qemu
# Inside xv6 shell:
# $ cowtest
# $ alarmtest
# $ usertests -q
```

Verified test suites:
- `cowtest` (`simple`, `three`, `file`): `ALL COW TESTS PASSED`
- `alarmtest` (`test0`–`test3`): `OK`

### Kernel Implementation Details

#### A. Copy-on-Write (CoW) `fork()`
- **Software PTE Flag (`kernel/riscv.h`):** Uses RISC-V Sv39 software-reserved bit 8 (`PTE_C = 1L << 8`) to distinguish a CoW-demoted writable page (`PTE_W=0, PTE_C=1`) from a genuinely read-only page such as program text (`PTE_W=0, PTE_C=0`).
- **Atomic Physical Page Reference Counting (`kernel/kalloc.c`):** Maintains `ref_count[PHYSTOP / PGSIZE]` protected by `ref_lock`. `kalloc()` initializes the count to `1`, `kaddref()` increments it when pages are shared, and `kfree()` decrements it—returning the physical page to `kmem.freelist` only when `ref_count` reaches `0`.
- **Lazy Page Sharing (`kernel/vm.c` — `uvmcopy`):** Instead of allocating and copying every page on `fork()`, `uvmcopy()` clears `PTE_W` and sets `PTE_C` on writable pages in both parent and child page tables, maps the child to the same physical addresses, and calls `kaddref()`.
- **Page Fault & Kernel Write Resolution (`kernel/trap.c`, `kernel/vm.c`):**
  - **User Store Fault (`scause == 15`):** `usertrap()` invokes `cow_alloc()`, which allocates a fresh physical page, copies the 4 KB contents, updates the faulting PTE with `PTE_W` restored and `PTE_C` cleared, and drops a reference on the old shared page via `kfree()`.
  - **Kernel `copyout()`:** Because kernel `memmove()` writes directly to physical addresses and bypasses user PTE checks, `copyout()` explicitly checks `PTE_C` and calls `cow_alloc()` before writing into user buffers.

#### B. User-Level Trap Alarms (`sigalarm` & `sigreturn`)
- **Per-Process Alarm State (`kernel/proc.h`, `kernel/proc.c`):** Stores `alarm_interval`, `alarm_ticks`, `alarm_handler`, `alarm_in_progress`, and a dedicated `alarm_trapframe` page allocated in `allocproc()` and freed in `freeproc()`.
- **System Calls (`kernel/sysproc.c`):**
  - `sigalarm(ticks, handler)` registers the interval and user-space handler virtual address (`sigalarm(0, 0)` cancels the alarm).
  - `sigreturn()` restores `*p->trapframe = *p->alarm_trapframe`, clears `alarm_in_progress`, and returns `p->trapframe->a0` so `syscall()`'s return-value writeback does not clobber the interrupted program's `a0` register.
- **Timer Delivery (`kernel/trap.c`):** On timer interrupts (`which_dev == 2`), `usertrap()` increments `alarm_ticks`; when `alarm_ticks >= alarm_interval` and `!alarm_in_progress`, it snapshots `*p->trapframe` into `alarm_trapframe`, marks `alarm_in_progress = 1` (preventing re-entrant handler calls), resets `alarm_ticks = 0`, and sets `p->trapframe->epc = p->alarm_handler`.

---

## Key Engineering Challenges Solved

1. **Eliminating Input Loss in a Multiplexed Terminal Lobby (`mastermind`):**
   Periodic full-screen redraws (`\x1b[H\x1b[2J`) in canonical terminal mode wiped out half-typed `challenge <id>` commands. Solved by switching interactive `stdin` to `termios` non-canonical mode, isolating an in-memory `input_buf`, and making lobby redraws strictly event-driven while reprinting the active input buffer.
2. **Reliable Ordered Delivery Over Unreliable 6-Byte UDP Datagrams (`cost_cutting.c`):**
   Built a selective-ACK framing protocol on raw UDP with sequence numbers, out-of-order chunk buffering, duplicate-ACK suppression, `100 ms` retransmission timers, and heartbeat-based peer-loss detection.
3. **Handling Kernel-Initiated Writes to CoW Pages (`xv6/kernel/vm.c`):**
   Standard CoW fault handling via `scause == 15` only catches user-space stores. Resolving `PTE_C` inside `copyout()` prevents syscalls such as `read()` or pipe reads from mutating shared physical pages across forked processes.
